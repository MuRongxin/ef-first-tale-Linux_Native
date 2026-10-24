#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""ef_client.py — ef - the first tale Linux 移植：img2_upscale 流水线的
输入/输出适配客户端。

设计原则：**只适配输入与输出，流水线本身不动**
  * 服务端（img2_upscale/server/）、模型、缩放倍率（4x / 8x）、
    Real-ESRGAN x4 + waifu2x x2 的推理顺序、lossless WebP 输出、
    HTTP 协议（/health、POST /v1/upscale、stages=4/8、multipart 双阶段响应）
    全部保持原样，本脚本一行都不改。
  * 输入：从本项目的 `assets/manifest/upscale-plan.json` 取任务，
    读取 `assets/raw/<tag>/<rel>.png`（6 个归档、递归子目录），
    跳过 thumbnail / mask / tint / .ani / .psd / 文本等非美术项。
  * 输出：写进本项目 `assets/hd/`，沿用流水线自己的命名语义
    `<tag>_4x/<rel>.webp`、`<tag>_8x/<rel>.webp`（= 服务端 lossless WebP 原样落盘）。

与 `/home/lrxin/文档/img2_upscale/client.py` 的差异（全部是 I/O）：
  1. 源目录：`bg/ sprite/ bg_cn/`（顶层、TGA/BMP）→ 项目 assets/raw（6 tag、递归、PNG）。
  2. 输出目录：`<源目录>_4x|_8x/<stem>.webp` → `<项目>/assets/hd/<tag>_4x|_8x/<归档内相对路径>.webp`。
  3. 任务来源：目录枚举 → upscale-plan.json（分类/方法/优先级/同内容别名）。
  4. 续跑判断：输出文件存在且尺寸 = 源×倍率即跳过（沿用原逻辑，WebP 尺寸校验）。
一句话：**协议、倍率、编码不变，只换“从哪读图、写到哪”。**

用法（需要可达的 GPU 服务端；服务端部署见 docs/UPSCALE-REMOTE-PIPELINE.md）：

    # 看计划（不联网）
    python3 tools/upscale/remote/ef_client.py --api http://127.0.0.1:6006/v1/upscale --dry-run

    # 抽 10 张 CG 试跑（4x）
    python3 tools/upscale/remote/ef_client.py --api http://.../v1/upscale \
        --scale 4 --categories cg --limit 10

    # 全量 8x（默认同时产出 4x 与 8x 两套树）
    python3 tools/upscale/remote/ef_client.py --api http://.../v1/upscale --scale 8
"""

from __future__ import annotations

import argparse
import csv
import http.client
import io
import json
import os
import ssl
import sys
import threading
import time
import uuid
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from urllib.parse import urlsplit

from PIL import Image

# ---------------------------------------------------------------------------
# 常量：与 img2_upscale 流水线保持一致
# ---------------------------------------------------------------------------
WEBP_MAX_DIM = 16383            # WebP(VP8L) 单边上限；服务端编码超过会失败
TOKEN_DEFAULT = "wa2-local-5090"
METHODS_DEFAULT = "upscale_art"  # UI 默认不做（烧死的文字应由移植引擎重绘）


# ---------------------------------------------------------------------------
# HTTP / multipart 协议（逐字移植自 img2_upscale/client.py，行为保持）
# ---------------------------------------------------------------------------
def log(message: str, log_file=None) -> None:
    line = time.strftime("%H:%M:%S ") + message
    print(line, flush=True)
    if log_file is not None:
        log_file.write(line + "\n")
        log_file.flush()


def normalize_api(api: str) -> str:
    parsed = urlsplit(api)
    if not parsed.scheme or not parsed.netloc:
        raise SystemExit(f"--api must be an absolute URL, got {api!r}")
    path = parsed.path or ""
    if "/v1/upscale" not in path:
        path = path.rstrip("/") + "/v1/upscale"
    return parsed._replace(path=path).geturl()


def health_url(api: str) -> str:
    parsed = urlsplit(api)
    return parsed._replace(path="/health", query="").geturl()


def http_request(api, method="GET", body=b"", headers=None, timeout=600,
                 fixed_ip="") -> tuple[int, dict, bytes]:
    parsed = urlsplit(api)
    if not parsed.hostname:
        raise SystemExit(f"bad API URL: {api!r}")
    port = parsed.port or (443 if parsed.scheme == "https" else 80)
    path = parsed.path or "/"
    if parsed.query:
        path += "?" + parsed.query
    connect_host = fixed_ip or parsed.hostname
    request_headers = dict(headers or {})
    if fixed_ip:
        request_headers["Host"] = parsed.hostname
    if parsed.scheme == "https":
        context = ssl._create_unverified_context()
        conn = http.client.HTTPSConnection(connect_host, port, timeout=timeout,
                                           context=context)
    else:
        conn = http.client.HTTPConnection(connect_host, port, timeout=timeout)
    try:
        conn.request(method, path, body=body, headers=request_headers)
        response = conn.getresponse()
        data = response.read()
        response_headers = {key.lower(): value
                            for key, value in response.getheaders()}
        return response.status, response_headers, data
    finally:
        conn.close()


def multipart_body(payload: bytes, filename: str) -> tuple[bytes, str]:
    boundary = "----wa2" + uuid.uuid4().hex
    body = b"".join([
        b"--" + boundary.encode("ascii") + b"\r\n",
        b'Content-Disposition: form-data; name="file"; filename="'
        + filename.encode("utf-8") + b'"\r\n',
        b"Content-Type: application/octet-stream\r\n\r\n",
        payload,
        b"\r\n--" + boundary.encode("ascii") + b"--\r\n",
    ])
    return body, f"multipart/form-data; boundary={boundary}"


def check_health(api: str, token: str, timeout: float, fixed_ip: str) -> dict:
    headers = {"Authorization": f"Bearer {token}"} if token else {}
    status, _, data = http_request(health_url(api), headers=headers,
                                   timeout=timeout, fixed_ip=fixed_ip)
    if status != 200:
        raise RuntimeError(f"health check HTTP {status}: {data[:300]!r}")
    return json.loads(data)


def parse_multipart_parts(body: bytes, content_type: str) -> dict[int, bytes]:
    """解析服务端 multipart/mixed 的 4x+8x 响应 → {scale: webp bytes}。"""
    marker = "boundary="
    if marker not in content_type:
        raise RuntimeError(f"multipart response without boundary: {content_type!r}")
    boundary = content_type.split(marker, 1)[1].strip().split(";")[0].strip().strip('"')
    delimiter = b"--" + boundary.encode("ascii")
    parts: dict[int, bytes] = {}
    for chunk in body.split(delimiter)[1:]:
        if chunk.startswith(b"--"):
            break
        if chunk.startswith(b"\r\n"):
            chunk = chunk[2:]
        header_blob, sep, data = chunk.partition(b"\r\n\r\n")
        if not sep:
            continue
        headers: dict[str, str] = {}
        for line in header_blob.decode("utf-8", "replace").split("\r\n"):
            key, _, value = line.partition(":")
            if _:
                headers[key.strip().lower()] = value.strip()
        scale = headers.get("x-wa2-part-scale")
        if not scale:
            continue
        declared = headers.get("content-length", "")
        if declared.isdigit():
            data = data[: int(declared)]
        elif data.endswith(b"\r\n"):
            data = data[:-2]
        parts[int(scale)] = data
    if not parts:
        raise RuntimeError("multipart response had no image parts")
    return parts


# ---------------------------------------------------------------------------
# 输入：ef 项目 → 任务列表（纯 I/O 适配）
# ---------------------------------------------------------------------------
def strip_known_ext(name: str) -> str:
    """去掉打包器扩展名：.png / .png- / .png.tmp / .ani / .psd / .txt。"""
    low = name.lower()
    for suf in (".png.tmp", ".png-", ".png", ".ani", ".psd", ".txt"):
        if low.endswith(suf):
            return name[: -len(suf)]
    if ".png" in low:
        return name[: low.find(".png")]
    return Path(name).stem


def out_path(out_root: Path, rel_path: str, scale: int, ext: str = ".webp") -> Path:
    """`assets/raw/<tag>/<rel>` → `assets/hd/<tag>_<N>x/<rel>.webp`。"""
    tag, _, sub = rel_path.partition("/")
    return out_root / f"{tag}_{scale}x" / (strip_known_ext(sub) + ext)


class Task:
    __slots__ = ("rel_path", "tag", "src", "entry", "w", "h", "aliases")

    def __init__(self, proj_root: Path, entry: dict):
        self.entry = entry
        self.rel_path = entry["rel_path"]
        self.tag = self.rel_path.split("/", 1)[0]
        self.src = proj_root / "assets" / "raw" / self.rel_path
        self.w = int(entry.get("w") or 0)
        self.h = int(entry.get("h") or 0)
        self.aliases = entry.get("aliases") or []

    def __repr__(self) -> str:
        return f"<Task {self.rel_path} {self.w}x{self.h}>"


def project_root_from_plan(plan_path: Path) -> Path:
    # <proj>/assets/manifest/upscale-plan.json
    return plan_path.resolve().parents[2]


def load_tasks(plan_path: Path, methods: set[str], categories: set[str],
               match: str, sort: str) -> list[Task]:
    data = json.loads(plan_path.read_text(encoding="utf-8"))
    proj = project_root_from_plan(plan_path)
    tasks: list[Task] = []
    for entry in data.get("entries", []):
        if entry.get("method") not in methods:
            continue
        if categories and entry.get("category") not in categories:
            continue
        if match and match not in entry.get("rel_path", ""):
            continue
        task = Task(proj, entry)
        if not task.src.is_file():
            continue
        tasks.append(task)
    if sort == "size-desc":
        tasks.sort(key=lambda t: t.w * t.h, reverse=True)
    elif sort == "size-asc":
        tasks.sort(key=lambda t: t.w * t.h)
    elif sort == "name":
        tasks.sort(key=lambda t: t.rel_path)
    return tasks


# ---------------------------------------------------------------------------
# 输出：ef 项目目录（沿用流水线的 4x/8x 目录语义与 WebP 原样落盘）
# ---------------------------------------------------------------------------
def output_plans(task: Task, out_root: Path, scale: int,
                 want4: bool) -> list[dict]:
    """按流水线语义给出该任务的输出文件（不改变倍率）。

    scale=4 : 只产出 4x。
    scale=8 : 产出 8x；默认同时产出 4x（--no-stage4 可关）。
              若 8x 尺寸超过 WebP 单边上限（源宽/高 > 2047），按官方语义
              只请求 4x，并把 4x 结果同时写入 `_8x` 目录（引擎按实际尺寸识别）。
    """
    w, h = task.w, task.h
    fits8 = max(w, h) * 8 <= WEBP_MAX_DIM
    plans: list[dict] = []
    if scale == 4:
        plans.append({"tree": 4, "path": out_path(out_root, task.rel_path, 4),
                      "dims": (w * 4, h * 4), "part": 4})
        return plans
    if fits8:
        if want4:
            plans.append({"tree": 4, "path": out_path(out_root, task.rel_path, 4),
                          "dims": (w * 4, h * 4), "part": 4})
        plans.append({"tree": 8, "path": out_path(out_root, task.rel_path, 8),
                      "dims": (w * 8, h * 8), "part": 8})
    else:
        # 官方 client.py 语义：8x 超 WebP 上限 → 只出 4x，并写进 8x 树
        if want4:
            plans.append({"tree": 4, "path": out_path(out_root, task.rel_path, 4),
                          "dims": (w * 4, h * 4), "part": 4})
        plans.append({"tree": 8, "path": out_path(out_root, task.rel_path, 8),
                      "dims": (w * 4, h * 4), "part": 4})
    return plans


def image_size_bytes(data: bytes) -> tuple[int, int]:
    with Image.open(io.BytesIO(data)) as im:
        return im.size


def file_matches(path: Path, dims: tuple[int, int]) -> bool:
    if not path.is_file():
        return False
    try:
        with Image.open(path) as im:
            return im.size == tuple(dims)
    except Exception:  # noqa: BLE001
        return False


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    partial = path.with_name(path.name + ".partial")
    partial.write_bytes(data)
    os.replace(partial, path)


def link_or_copy(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if file_matches(dst, image_size_bytes(src.read_bytes())):
        return
    if dst.exists():
        dst.unlink()
    try:
        os.link(src, dst)
    except OSError:
        import shutil
        shutil.copy2(src, dst)


def alias_outputs(task: Task, plans: list[dict], out_root: Path) -> list[tuple]:
    """同一内容在其它 tag/文件名下的输出路径（plan.aliases，可硬链接省盘）。"""
    result = []
    for alias in task.aliases:
        rel = alias.get("rel_path")
        if not rel:
            continue
        for plan in plans:
            result.append((plan["path"], out_path(out_root, rel, plan["tree"]),
                           tuple(plan["dims"])))
    return result


def plans_done(plans: list[dict]) -> bool:
    return all(file_matches(p["path"], p["dims"]) for p in plans)


# ---------------------------------------------------------------------------
# 单个请求 / 单个任务
# ---------------------------------------------------------------------------
def request_upscale(api: str, token: str, filename: str, payload: bytes,
                    stages: str, timeout: float, retries: int,
                    fixed_ip: str) -> dict:
    body, content_type = multipart_body(payload, filename)
    headers = {"Content-Type": content_type, "Content-Length": str(len(body))}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    url = api + ("&" if "?" in api else "?") + "stages=" + stages
    last_error = ""
    for attempt in range(1, retries + 1):
        t0 = time.perf_counter()
        try:
            status, response_headers, data = http_request(
                url, method="POST", body=body, headers=headers,
                timeout=timeout, fixed_ip=fixed_ip)
            total_s = time.perf_counter() - t0
            if status != 200:
                raise RuntimeError(f"HTTP {status}: {data[:300]!r}")
            content_type_header = response_headers.get("content-type", "")
            if "multipart/" in content_type_header:
                parts = parse_multipart_parts(data, content_type_header)
            else:
                final_scale = int(response_headers.get("x-wa2-final-scale", 4))
                parts = {final_scale: data}
            timings = {}
            raw_timings = response_headers.get("x-wa2-timings")
            if raw_timings:
                try:
                    timings = json.loads(raw_timings)
                except ValueError:
                    timings = {}
            return {"ok": True, "parts": parts, "timings": timings,
                    "attempt": attempt, "total_s": total_s,
                    "upload_mb": len(body) / 1e6,
                    "download_mb": len(data) / 1e6}
        except Exception as exc:  # noqa: BLE001 - 与官方客户端一致：任何错误都重试
            last_error = f"{type(exc).__name__}: {exc}"
            if attempt < retries:
                time.sleep(min(2 * attempt, 10))
    return {"ok": False, "error": last_error, "attempt": retries,
            "upload_mb": len(body) / 1e6}


def stages_for(task: Task, scale: int, want4: bool) -> str:
    fits8 = max(task.w, task.h) * 8 <= WEBP_MAX_DIM
    if scale == 4:
        return "4"
    if not fits8:
        return "4"                       # 8x 超限 → 只要 4x（官方语义）
    return "4,8" if want4 else "8"


def process_task(task: Task, cfg) -> dict:
    out_root = cfg.out_root
    plans = output_plans(task, out_root, cfg.scale, cfg.want4)
    if not plans:
        return {"ok": True, "skipped": True, "task": task,
                "reason": "no output planned"}
    aliases = alias_outputs(task, plans, out_root)
    if not cfg.force and plans_done(plans):
        for src, dst, dims in aliases:
            if not file_matches(dst, dims):
                link_or_copy(src, dst)
        return {"ok": True, "task": task, "noop": True}
    if not task.src.is_file():
        raise RuntimeError(f"source missing: {task.src}")

    result = request_upscale(cfg.api, cfg.token, task.src.name,
                             task.src.read_bytes(),
                             stages_for(task, cfg.scale, cfg.want4),
                             cfg.timeout, cfg.retries, cfg.api_ip)
    if not result["ok"]:
        return {"ok": False, "task": task, "error": result["error"],
                "attempt": result["attempt"],
                "upload_mb": result.get("upload_mb", 0.0)}

    written = []
    for plan in plans:
        part = result["parts"].get(plan["part"])
        if part is None:
            raise RuntimeError(f"missing {plan['part']}x part "
                               f"(got {sorted(result['parts'])})")
        size = image_size_bytes(part)
        if size != tuple(plan["dims"]):
            raise RuntimeError(f"part {plan['part']}x is {size}, "
                               f"expected {plan['dims']}")
        write_atomic(plan["path"], part)
        written.append(plan["path"])
    for src, dst, _dims in aliases:
        link_or_copy(src, dst)
    return {"ok": True, "task": task, "dsts": written,
            "upload_mb": result["upload_mb"],
            "download_mb": result["download_mb"],
            "total_s": result["total_s"], "timings": result["timings"]}


def simplify_timings(timings: dict) -> str:
    keys = ("x4", "x2", "stage4", "stage8", "encode", "encode4", "encode8",
            "wait_gpu", "decode")
    return " ".join(f"{k}={timings[k]:.2f}" for k in keys if k in timings)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(
        description="ef 移植：img2_upscale 流水线的输入/输出适配客户端（4x/8x 不变）",
        formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--api", default=os.environ.get("WA2_API", ""),
                        help="服务端 endpoint，如 http://127.0.0.1:6006/v1/upscale")
    parser.add_argument("--api-ip", default=os.environ.get("WA2_API_IP", ""),
                        help="DNS 不通时直连 IP（保留 Host 头，AutoDL 场景）")
    parser.add_argument("--token", default=os.environ.get("WA2_API_TOKEN", TOKEN_DEFAULT))
    parser.add_argument("--plan", default=os.environ.get("EF_UPSCALE_PLAN", ""),
                        help="默认 <cwd>/assets/manifest/upscale-plan.json")
    parser.add_argument("--out-root", default=os.environ.get("EF_HD_ROOT", ""),
                        help="默认 <proj>/assets/hd")
    parser.add_argument("--scale", type=int, choices=(4, 8), default=8,
                        help="4 = Real-ESRGAN x4；8 = x4 + waifu2x x2（默认同时产 4x/8x）")
    parser.add_argument("--no-stage4", action="store_true",
                        help="--scale 8 时不保存中间 4x 产物")
    parser.add_argument("--methods", default=METHODS_DEFAULT,
                        help="逗号分隔 plan method，默认 upscale_art")
    parser.add_argument("--categories", default="",
                        help="逗号分隔类别过滤，如 cg,tachie,bg")
    parser.add_argument("--match", default="")
    parser.add_argument("--sort", choices=["priority", "name", "size-desc", "size-asc"],
                        default="priority")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--concurrency", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--retries", type=int, default=3)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--retry-failed", default="")
    parser.add_argument("--timings-csv", default="")
    parser.add_argument("--log-file", default="")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-health-check", action="store_true")
    args = parser.parse_args()

    plan_path = Path(args.plan) if args.plan else \
        Path.cwd() / "assets" / "manifest" / "upscale-plan.json"
    plan_path = plan_path.expanduser().resolve()
    if not plan_path.is_file():
        parser.error(f"plan not found: {plan_path}（--plan 或 EF_UPSCALE_PLAN）")
    proj = project_root_from_plan(plan_path)
    out_root = Path(args.out_root).expanduser().resolve() if args.out_root \
        else proj / "assets" / "hd"
    methods = {x.strip() for x in args.methods.split(",") if x.strip()}
    categories = {x.strip() for x in args.categories.split(",") if x.strip()}
    want4 = not (args.scale == 8 and args.no_stage4)
    args.want4 = want4
    args.out_root = out_root
    args.api = normalize_api(args.api) if args.api else ""
    log_file = open(args.log_file, "a", encoding="utf-8") if args.log_file else None

    # 输入：任务列表
    tasks = load_tasks(plan_path, methods, categories, args.match, args.sort)
    if args.retry_failed:
        fails = [ln.strip() for ln in Path(args.retry_failed).read_text(
            encoding="utf-8").splitlines() if ln.strip()]
        known = {t.rel_path: t for t in tasks}
        tasks = [known[rel] for rel in fails if rel in known]
        log(f"retry-failed: {len(tasks)}/{len(fails)} known tasks", log_file)
        args.force = True
    if args.limit:
        tasks = tasks[: args.limit]

    # 输入过滤：4x 输出本身超过 WebP 单边上限的源（本项目 4 个超宽滚动背景）
    # 无法由该流水线输出 WebP，单独列出而不是送进必然失败的任务。
    oversized = [t.rel_path for t in tasks
                 if max(t.w, t.h) * 4 > WEBP_MAX_DIM]
    if oversized:
        tasks = [t for t in tasks if max(t.w, t.h) * 4 <= WEBP_MAX_DIM]
        report_dir = proj / "work" / "upscale-remote"
        report_dir.mkdir(parents=True, exist_ok=True)
        (report_dir / "skipped_4x_over_webp_limit.txt").write_text(
            "\n".join(oversized) + "\n", encoding="utf-8")

    pending, done, fallback8 = [], 0, []
    for task in tasks:
        plans = output_plans(task, out_root, args.scale, want4)
        if not plans:
            continue
        if args.scale == 8 and max(task.w, task.h) * 8 > WEBP_MAX_DIM:
            fallback8.append(task.rel_path)
        if not args.force and plans_done(plans):
            done += 1
        else:
            pending.append(task)

    log(f"plan: {plan_path}", log_file)
    log(f"out : {out_root}", log_file)
    log(f"scale={args.scale}x stage4={'yes' if want4 else 'no'} "
        f"concurrency={args.concurrency}", log_file)
    log(f"tasks: {len(tasks)} (pending {len(pending)}, already done {done}, "
        f"skipped >WebP-4x {len(oversized)})", log_file)
    src_px = sum(t.w * t.h for t in pending)
    log(f"pending source pixels: {src_px/1e6:.1f} Mpx; 4x {src_px*16/1e6:.0f} Mpx"
        + (f"; 8x {src_px*64/1e6:.0f} Mpx" if args.scale == 8 and want4 else ""),
        log_file)
    if fallback8:
        log(f"8x exceeds WebP limit (writes 4x into _8x tree): {len(fallback8)}",
            log_file)

    if not args.api:
        if args.dry_run:
            for task in pending[:15]:
                log(f"  {task.rel_path} -> " + ", ".join(
                    str(p["path"].relative_to(out_root))
                    for p in output_plans(task, out_root, args.scale, want4)),
                    log_file)
            log("[dry-run] 未提供 --api，仅列计划", log_file)
            return 0
        parser.error("--api is required（或设置 WA2_API）")

    log(f"server: {args.api}", log_file)
    if not args.no_health_check:
        try:
            health = check_health(args.api, args.token, min(args.timeout, 30),
                                  args.api_ip)
        except Exception as exc:  # noqa: BLE001
            log(f"health check failed: {exc}", log_file)
            return 2
        gpu = health.get("gpu") or {}
        log("server ok: device={} gpu={} models={} encoders={}".format(
            health.get("device"), gpu.get("name", "?"), health.get("models"),
            (health.get("encode") or {}).get("workers")), log_file)

    if not pending:
        log("nothing to do", log_file)
        return 0
    if args.dry_run:
        for task in pending[:15]:
            log(f"  {task.rel_path} -> " + ", ".join(
                str(p["path"].relative_to(out_root))
                for p in output_plans(task, out_root, args.scale, want4)),
                log_file)
        log("[dry-run] 未上传任何图片", log_file)
        return 0

    # 输出：与官方客户端一致的并发/续跑/失败列表/分阶段耗时
    failed_path = out_root / "upscale_failed.txt"
    lock = threading.Lock()
    progress = {"done": 0, "ok": 0, "failed": 0, "bytes": 0}
    failures: list[str] = []
    csv_handle = csv_writer = None
    if args.timings_csv:
        csv_handle = open(args.timings_csv, "a", newline="", encoding="utf-8")
        csv_writer = csv.writer(csv_handle)
        if csv_handle.tell() == 0:
            csv_writer.writerow(["time", "rel_path", "scale", "ok", "total_s",
                                 "upload_mb", "download_mb", "timings"])
    total = len(pending)
    start = time.perf_counter()

    def handle(record: dict) -> None:
        with lock:
            progress["done"] += 1
            elapsed = time.perf_counter() - start
            rate = progress["done"] / elapsed if elapsed > 0 else 0.0
            eta = (total - progress["done"]) / rate if rate > 0 else 0.0
            index = progress["done"]
        task = record["task"]
        if record["ok"]:
            with lock:
                progress["ok"] += 1
                progress["bytes"] += record.get("download_mb", 0.0)
            if record.get("noop"):
                return
            log(f"[{index}/{total}] {task.rel_path} "
                f"{record.get('total_s', 0):.1f}s ok "
                f"dl={record.get('download_mb', 0):.1f}MB "
                f"| srv {simplify_timings(record.get('timings') or {})} "
                f"| ETA {eta/60:.0f}m", log_file)
        else:
            with lock:
                progress["failed"] += 1
                failures.append(task.rel_path)
            log(f"[{index}/{total}] {task.rel_path} FAILED: "
                f"{record.get('error')}", log_file)
        if csv_writer is not None:
            csv_writer.writerow([
                time.strftime("%Y-%m-%d %H:%M:%S"), task.rel_path, args.scale,
                1 if record["ok"] else 0, f"{record.get('total_s', 0):.3f}",
                f"{record.get('upload_mb', 0):.3f}",
                f"{record.get('download_mb', 0):.3f}",
                json.dumps(record.get("timings") or {}, separators=(",", ":")),
            ])
            csv_handle.flush()

    try:
        pool = ThreadPoolExecutor(max_workers=args.concurrency)
        try:
            futures = {pool.submit(process_task, task, args): task
                       for task in pending}
            for future in as_completed(futures):
                try:
                    handle(future.result())
                except Exception as exc:  # noqa: BLE001
                    handle({"ok": False, "task": futures[future],
                            "error": f"{type(exc).__name__}: {exc}"})
        except KeyboardInterrupt:
            log("interrupted; waiting for in-flight requests ...", log_file)
            pool.shutdown(wait=True, cancel_futures=True)
        else:
            pool.shutdown(wait=True)
    finally:
        if csv_handle is not None:
            csv_handle.close()

    elapsed = time.perf_counter() - start
    with lock:
        ok, failed, downloaded = progress["ok"], progress["failed"], progress["bytes"]
    log(f"done: {ok} ok, {failed} failed, {downloaded:.1f}MB in "
        f"{elapsed/60:.1f}min ({ok / max(elapsed, 1e-9) * 3600:.0f} imgs/h)",
        log_file)
    out_root.mkdir(parents=True, exist_ok=True)
    if failures:
        failed_path.write_text("\n".join(failures) + "\n", encoding="utf-8")
        log(f"failure list: {failed_path}", log_file)
    elif failed_path.exists():
        failed_path.unlink()
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
