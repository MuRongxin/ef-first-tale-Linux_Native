#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""fix_large.py — 对 7 张「特大图」用分块推理补齐 4×/8× 产物。

背景：RealESRGAN 在 P40 上是整图推理，x4 输出 > ~32 MPx 会 CUDA OOM；
另有 4 张源宽 > 4095，4× 输出超过 WebP 单边上限 16383。
本脚本在**客户端**把源图切成带重叠的小块（默认 768px + 96px overlap），
逐块请求服务端（stages=4 或 4,8），再按重叠区裁掉边缘后拼接成完整图：

  4× 输出 ≤ 16383 宽  → lossless WebP（同普通任务）
  4× 输出 >  16383 宽  → PNG（WebP 无法承载）
  8× 输出 ≤ 16383 宽  → lossless WebP
  8× 超上限/4× 超上限 → 8× 树写入 4× 内容（与流水线官方语义一致）

服务端、倍率、模型均不改；只走已有的 /v1/upscale 协议。
"""

from __future__ import annotations

import argparse
import io
import json
import mimetypes
import os
import sys
import time
import uuid
import urllib.request
from pathlib import Path

from PIL import Image

Image.MAX_IMAGE_PIXELS = None          # 这些图是有意的超大图，关闭 bomb 警告
WEBP_MAX_DIM = 16383

DATA = "----wa2fix" + uuid.uuid4().hex


def log(msg: str) -> None:
    print(time.strftime("%H:%M:%S ") + msg, flush=True)


def strip_known_ext(sub: str) -> str:
    low = sub.lower()
    for suf in (".png.tmp", ".png-", ".png", ".ani", ".psd", ".txt"):
        if low.endswith(suf):
            return sub[: -len(suf)]
    return sub


def out_path(root: Path, rel: str, tree: int, ext: str) -> Path:
    tag, _, sub = rel.partition("/")
    return root / f"{tag}_{tree}x" / (strip_known_ext(sub) + ext)


def multipart(payload: bytes, filename: str) -> tuple[bytes, str]:
    boundary = "----wa2fix" + uuid.uuid4().hex
    body = b"".join([
        b"--" + boundary.encode() + b"\r\n",
        b'Content-Disposition: form-data; name="file"; filename="'
        + filename.encode() + b'"\r\n',
        b"Content-Type: application/octet-stream\r\n\r\n",
        payload, b"\r\n--" + boundary.encode() + b"--\r\n",
    ])
    return body, "multipart/form-data; boundary=" + boundary


def parse_parts(body: bytes, ctype: str) -> dict[int, bytes]:
    if "boundary=" not in ctype:
        raise RuntimeError("not multipart: " + ctype)
    boundary = ctype.split("boundary=", 1)[1].strip().split(";")[0].strip('"')
    parts: dict[int, bytes] = {}
    for chunk in body.split(b"--" + boundary.encode())[1:]:
        if chunk.startswith(b"--"):
            break
        if chunk.startswith(b"\r\n"):
            chunk = chunk[2:]
        head, sep, data = chunk.partition(b"\r\n\r\n")
        if not sep:
            continue
        headers = {}
        for line in head.decode("utf-8", "replace").split("\r\n"):
            k, _, v = line.partition(":")
            if _:
                headers[k.strip().lower()] = v.strip()
        scale = headers.get("x-wa2-part-scale")
        if not scale:
            continue
        declared = headers.get("content-length", "")
        if declared.isdigit():
            data = data[: int(declared)]
        elif data.endswith(b"\r\n"):
            data = data[:-2]
        parts[int(scale)] = data
    return parts


def post(api: str, token: str, png: bytes, name: str, stages: str,
         timeout: float, retries: int = 3) -> dict[int, bytes]:
    body, ctype = multipart(png, name)
    url = api + ("&" if "?" in api else "?") + "stages=" + stages
    headers = {"Content-Type": ctype, "Content-Length": str(len(body))}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    last = ""
    for attempt in range(1, retries + 1):
        try:
            req = urllib.request.Request(url, data=body, headers=headers, method="POST")
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                data = resp.read()
                rctype = resp.headers.get("Content-Type", "")
                if "multipart/" in rctype:
                    parts = parse_parts(data, rctype)
                else:
                    fs = int(resp.headers.get("x-wa2-final-scale", 4))
                    parts = {fs: data}
                if not parts:
                    raise RuntimeError("empty response")
                return parts
        except Exception as exc:  # noqa: BLE001
            last = f"{type(exc).__name__}: {exc}"
            if attempt < retries:
                time.sleep(2 * attempt)
    raise RuntimeError(last)


def tile_grid(length: int, tile: int, overlap: int) -> list[tuple[int, int, int, int, int, int]]:
    """返回 [(core_start, core_end, crop_start, crop_end, left_crop, right_crop)]。"""
    out = []
    if length <= tile:
        return [(0, length, 0, length, 0, 0)]
    step = tile - overlap
    start = 0
    while True:
        core_end = min(start + tile, length)
        crop_start = max(0, start - overlap)
        crop_end = min(length, core_end + overlap)
        left_crop = start - crop_start
        right_crop = crop_end - core_end
        out.append((start, core_end, crop_start, crop_end, left_crop, right_crop))
        if core_end >= length:
            break
        start += step
    return out


def upscale_image(src: Path, rel: str, w: int, h: int, out_root: Path,
                  api: str, token: str, tile: int, overlap: int,
                  timeout: float) -> None:
    fits4 = max(w, h) * 4 <= WEBP_MAX_DIM
    fits8 = max(w, h) * 8 <= WEBP_MAX_DIM
    stages = "4,8" if fits8 else "4"
    log(f"--- {rel}  {w}x{h}  fits4={fits4} fits8={fits8}  stages={stages}")

    with Image.open(src) as im:
        img = im.convert("RGBA")
    xs = tile_grid(w, tile, overlap)
    ys = tile_grid(h, tile, overlap)
    log(f"    网格 {len(xs)}x{len(ys)} = {len(xs)*len(ys)} 块（tile={tile}, overlap={overlap}）")

    canvases: dict[int, Image.Image] = {}
    if fits8:
        canvases[4] = Image.new("RGBA", (w * 4, h * 4))
        canvases[8] = Image.new("RGBA", (w * 8, h * 8))
    elif fits4:
        canvases[4] = Image.new("RGBA", (w * 4, h * 4))
    else:
        canvases[4] = Image.new("RGBA", (w * 4, h * 4))

    t_start = time.time()
    n = 0
    for (y0, y1, cy0, cy1, _, _) in ys:
        for (x0, x1, cx0, cx1, _, _) in xs:
            n += 1
            crop = img.crop((cx0, cy0, cx1, cy1))
            buf = io.BytesIO()
            crop.save(buf, "PNG")
            parts = post(api, token, buf.getvalue(),
                         f"{rel.replace('/', '_')}_t{n}.png", stages, timeout)
            for scale, part in parts.items():
                with Image.open(io.BytesIO(part)) as t:
                    t = t.convert("RGBA")
                    sx = (x0 - cx0) * scale
                    sy = (y0 - cy0) * scale
                    ex = sx + (x1 - x0) * scale
                    ey = sy + (y1 - y0) * scale
                    canvases[scale].paste(t.crop((sx, sy, ex, ey)),
                                          (x0 * scale, y0 * scale))
            if n % 4 == 0 or n == len(xs) * len(ys):
                log(f"    块 {n}/{len(xs)*len(ys)}  {time.time()-t_start:.0f}s")

    # 写出 4x
    if fits4:
        p4 = out_path(out_root, rel, 4, ".webp")
        p4.parent.mkdir(parents=True, exist_ok=True)
        canvases[4].save(p4, "WEBP", lossless=True, method=4)
        ext4 = ".webp"
    else:
        p4 = out_path(out_root, rel, 4, ".png")
        p4.parent.mkdir(parents=True, exist_ok=True)
        canvases[4].save(p4, "PNG", compress_level=6)
        ext4 = ".png"
    log(f"    4x -> {p4.relative_to(out_root)} {canvases[4].size} "
        f"{p4.stat().st_size/1e6:.1f} MB")

    # 写出 8x
    if fits8:
        p8 = out_path(out_root, rel, 8, ".webp")
        canvases[8].save(p8, "WEBP", lossless=True, method=4)
        log(f"    8x -> {p8.relative_to(out_root)} {canvases[8].size} "
            f"{p8.stat().st_size/1e6:.1f} MB")
    else:
        # 官方语义：8x 超 WebP 上限 / 4x 已超限 → 8x 树放 4x 内容
        p8 = out_path(out_root, rel, 8, ext4)
        p8.parent.mkdir(parents=True, exist_ok=True)
        if p8.exists():
            p8.unlink()
        try:
            os.link(p4, p8)
        except OSError:
            import shutil
            shutil.copy2(p4, p8)
        log(f"    8x -> {p8.relative_to(out_root)} (4x 内容, 官方回退)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default="http://127.0.0.1:6006/v1/upscale")
    ap.add_argument("--token", default=os.environ.get("WA2_API_TOKEN", "wa2-local-5090"))
    ap.add_argument("--plan", default="assets/manifest/upscale-plan.json")
    ap.add_argument("--out-root", default="assets/hd")
    ap.add_argument("--list", default="")
    ap.add_argument("--only", default="")
    ap.add_argument("--tile", type=int, default=768)
    ap.add_argument("--overlap", type=int, default=96)
    ap.add_argument("--timeout", type=float, default=1200)
    args = ap.parse_args()

    root = Path.cwd()
    plan_path = (root / args.plan).resolve()
    if not plan_path.is_file():
        sys.exit(f"plan not found: {plan_path}")
    entries = {e["rel_path"]: e for e in json.load(
        open(plan_path, encoding="utf-8"))["entries"]}
    if args.only:
        rels = [args.only]
    elif args.list:
        rels = [ln.strip() for ln in open(root / args.list, encoding="utf-8")
                if ln.strip() and not ln.strip().startswith("#")]
    else:
        sys.exit("需要 --list 或 --only")
    out_root = (root / args.out_root).resolve()
    log(f"待补 {len(rels)} 张 -> {out_root}")
    for rel in rels:
        e = entries.get(rel)
        if not e:
            log(f"跳过（plan 中无此条目）: {rel}")
            continue
        t = time.time()
        upscale_image(root / "assets" / "raw" / rel, rel, e["w"], e["h"],
                      out_root, args.api, args.token, args.tile, args.overlap,
                      args.timeout)
        log(f"完成 {rel}  用时 {time.time()-t:.0f}s")
    log("全部完成")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
