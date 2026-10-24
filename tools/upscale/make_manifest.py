#!/usr/bin/env python3
"""make_manifest.py — ef 高清化工具链：资源规模统计 / 分类清单生成器（只读）

读取 assets/manifest/entries_*.jsonl（解包时生成的 PNG 头信息），
按「文件名族 + 尺寸」把 3,758 张 PNG 归入互斥类别，输出：

  * 每类文件数 / 原始像素 / 放大后像素 / 预计输出体积 / 预计 GPU 时间
  * 每类文件清单（供 run_realesrgan.sh 使用）
  * 特殊尺寸与 alpha 情况汇总

本脚本只读 manifest 与 assets/raw 的 stat，不解码图片、不写 assets/。
默认 --dry-run 只打印汇总；--emit-worklists 才写清单文件。

用法：
  python3 tools/upscale/make_manifest.py                      # 打印汇总
  python3 tools/upscale/make_manifest.py --stats-out work/upscaler/asset-stats.json
  python3 tools/upscale/make_manifest.py --emit-worklists work/upscaler/worklists
  python3 tools/upscale/make_manifest.py --verify             # 同时校验磁盘文件存在

分类规则（互斥，按优先级从上到下）：
  1. mask_procedural : 宽度<=4 的 1x601/1x611 渐变条、WHITE/BLACK、TRANS*、
                       fade*、snow*、or_* 前景条 —— 应程序化重绘，不超分
  2. thumb           : sys/thumb/* 缩略图（400x80 / 107x80 等）
                      —— 应由高清 CG 降采样生成，不单独超分
  3. scroll_bg       : bg_scl*（3200x600 等滚动背景）
  4. cg              : ev* / evh* / ed* 事件 CG、结局 CG
  5. tachie          : st_* / stm_* 立绘（含 bg 归档里的 st_* 810x620 合成层）
  6. bg              : bg_/bgs_/bgw_/bgc_/kei_/vista_/ago_/prologue_/terop_ 背景
  7. ui              : sys 归档其余 UI 图（菜单/存档/图鉴/滑块…）
  8. other           : 兜底

体积与时间估算模型来自 work/upscaler/ 的实测（见 docs/UPSCALE-TOOLCHAIN.md）：
  * animevideov3 在 GTX 960M 上：x2≈3.7 / x3≈3.9 / x4≈4.6 秒每百万输入像素
    （批量模式，已扣除模型加载固定开销）
  * x4plus-anime：≈13.5 秒每百万输入像素（4x）
  * PNG 输出 B/px：cg≈1.30、tachie≈1.08、bg≈1.00、scroll_bg≈0.55、
    ui≈1.60、thumb≈1.95（内容相关，误差约 ±30%）
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter, OrderedDict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MANIFEST_DIR = os.path.join(ROOT, "assets", "manifest")
RAW_DIR = os.path.join(ROOT, "assets", "raw")
TAGS = ["bg", "st", "sys", "r18_bg", "r18_st", "r18_sys"]

# 估算常量：输入像素 -> GPU 秒（批量，扣除模型加载）
SEC_PER_MPX = {
    "realesr-animevideov3": {2: 3.7, 3: 3.9, 4: 4.6},
    "realesrgan-x4plus-anime": {4: 13.5},
    "realesrgan-x4plus": {4: 22.0},  # 估算：x4plus 参数量约为 anime_6B 的 2 倍
}
# 输出 PNG 每像素字节数（4x 实测；2x/3x 近似相同）
BYTES_PER_PX = {
    "cg": 1.30,
    "tachie": 1.08,
    "bg": 1.00,
    "scroll_bg": 0.55,
    "ui": 1.60,
    "thumb": 1.95,
    "mask_procedural": 0.10,
    "other": 1.10,
}
# 是否建议交给超分模型（False => 程序化重绘/降采样生成）
UPSCALE_POLICY = {
    "mask_procedural": False,
    "thumb": False,
    "scroll_bg": True,
    "cg": True,
    "tachie": True,
    "bg": True,
    "ui": "partial",  # 装饰性图形可超分，含文字 UI 建议引擎重绘
    "other": True,
}

MASK_RE = re.compile(r"^(fade|snow|or_|TRANS|WHITE|BLACK)", re.I)


def load_rows() -> list[dict]:
    rows: list[dict] = []
    for tag in TAGS:
        path = os.path.join(MANIFEST_DIR, f"entries_{tag}.jsonl")
        if not os.path.exists(path):
            print(f"WARN: 缺少 manifest {path}", file=sys.stderr)
            continue
        with open(path, "r", encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                r = json.loads(line)
                r.setdefault("tag", tag)
                rows.append(r)
    return rows


def classify(r: dict) -> str:
    name = r.get("name", "").replace("\\", "/")
    base = name.split("/")[-1]
    w, h = r.get("width") or 0, r.get("height") or 0
    tag = r.get("tag", "")

    if w and h and (w <= 4 or h <= 4):
        return "mask_procedural"
    if MASK_RE.match(base):
        return "mask_procedural"
    if "thumb" in name.lower():
        return "thumb"
    if base.startswith("bg_scl"):
        return "scroll_bg"
    if re.match(r"^(ev|evh|ed)", base, re.I):
        return "cg"
    if re.match(r"^stm?_", base, re.I):
        return "tachie"
    if re.match(r"^(bg_|bgs_|bgw_|bgc_|kei_|vista_|ago_|prologue_|terop_)", base, re.I):
        return "bg"
    if tag in ("sys", "r18_sys"):
        return "ui"
    return "other"


def dedupe(rows: list[dict]) -> tuple[list[dict], int, int]:
    """按「归档内相对路径」去重；r18_* 归档与基础归档同相对路径时 r18 覆盖生效。

    注意：不能用纯 basename 去重 —— sys/thumb/ 下 407 张缩略图与 bg/ 下同名
    全尺寸 CG 只是名字复用（内容不同），必须保留为两类资源。

    返回 (rows, overrides, same_basename_groups)；same_basename_groups 只用于
    报告「同名不同目录」的冲突规模，不参与去重。
    """
    chosen: "OrderedDict[str, dict]" = OrderedDict()
    overrides = 0
    seen_basename: dict[str, list[dict]] = {}
    for r in rows:
        if r.get("kind") != "png" or not r.get("width"):
            continue
        name = r["name"].replace("\\", "/")
        arch = r["tag"][4:] if r["tag"].startswith("r18_") else r["tag"]
        key = f"{arch}/{name}"
        base = name.split("/")[-1]
        seen_basename.setdefault(base, []).append(r)
        if key in chosen:
            old = chosen[key]
            if r["tag"].startswith("r18") and not old["tag"].startswith("r18"):
                chosen[key] = r
                overrides += 1
            continue
        chosen[key] = r
    same_name_groups = sum(1 for v in seen_basename.values() if len(v) > 1)
    return list(chosen.values()), overrides, same_name_groups


def fmt_int(n: float) -> str:
    return f"{int(round(n)):,}"


def fmt_bytes(n: float) -> str:
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024:
            return f"{n:.1f}{unit}"
        n /= 1024
    return f"{n:.1f}PB"


def summarize(rows: list[dict], scale: int, model: str) -> dict:
    per_class: dict[str, dict] = {}
    for r in rows:
        cls = classify(r)
        d = per_class.setdefault(
            cls,
            {
                "class": cls,
                "files": 0,
                "raw_bytes": 0,
                "raw_px": 0,
                "alpha_files": 0,
                "sizes": Counter(),
                "upscale_policy": UPSCALE_POLICY.get(cls, True),
            },
        )
        d["files"] += 1
        d["raw_bytes"] += r.get("raw_size", 0) or 0
        d["raw_px"] += r["width"] * r["height"]
        d["alpha_files"] += 1 if r.get("has_alpha") else 0
        d["sizes"][(r["width"], r["height"])] += 1

    total = {"files": 0, "raw_bytes": 0, "raw_px": 0, "out_px": 0, "out_bytes": 0.0, "gpu_s": 0.0}
    out = []
    for cls, d in sorted(per_class.items(), key=lambda kv: -kv[1]["raw_px"]):
        upscaled = UPSCALE_POLICY.get(cls, True) is not False
        out_px = d["raw_px"] * scale * scale
        bpp = BYTES_PER_PX.get(cls, 1.1)
        out_bytes = out_px * bpp if upscaled else d["raw_bytes"]
        gpu_s = (d["raw_px"] / 1e6) * SEC_PER_MPX.get(model, {scale: 4.6}).get(scale, 4.6) if upscaled else 0.0
        top = [f"{w}x{h}" for (w, h), _ in d["sizes"].most_common(3)]
        item = {
            "class": cls,
            "files": d["files"],
            "raw_bytes": d["raw_bytes"],
            "raw_px": d["raw_px"],
            "alpha_files": d["alpha_files"],
            "out_px": out_px,
            "out_bytes": int(out_bytes),
            "gpu_seconds": round(gpu_s, 1),
            "upscale_policy": UPSCALE_POLICY.get(cls, True),
            "top_sizes": top,
            "distinct_sizes": len(d["sizes"]),
        }
        out.append(item)
        total["files"] += d["files"]
        total["raw_bytes"] += d["raw_bytes"]
        total["raw_px"] += d["raw_px"]
        total["out_px"] += out_px if upscaled else d["raw_px"]
        total["out_bytes"] += out_bytes
        total["gpu_s"] += gpu_s

    return {
        "model": model,
        "scale": scale,
        "classes": out,
        "total": {
            "files": total["files"],
            "raw_bytes": total["raw_bytes"],
            "raw_px": total["raw_px"],
            "out_px": total["out_px"],
            "out_bytes": int(total["out_bytes"]),
            "gpu_seconds": round(total["gpu_s"], 1),
            "gpu_hours": round(total["gpu_s"] / 3600, 2),
        },
    }


def print_report(rows: list[dict], overrides: int, same_name_groups: int) -> None:
    print(f"manifest PNG 条目（含 r18 覆盖）: {len(rows)}")
    print(f"r18 覆盖生效条目: {overrides}（同相对路径，r18 版替换基础版）")
    print(f"同名不同目录条目组: {same_name_groups}（大多为 thumb 缩略图与全尺寸 CG 同名复用，非重复）")
    total_px = sum(r["width"] * r["height"] for r in rows)
    total_b = sum(r.get("raw_size", 0) or 0 for r in rows)
    print(f"去重后唯一 PNG: {len(rows)}，原始总像素 {fmt_int(total_px)} ({total_px/1e6:.1f} Mpx)，原始字节 {fmt_bytes(total_b)}")
    print()
    hdr = f"{'class':16s} {'files':>6s} {'raw_Mpx':>9s} {'raw_MB':>8s} {'alpha':>6s} {'4x_out_Mpx':>11s} {'4x_out_GB':>10s} {'4x_gpu_h':>9s}  policy / top sizes"
    print(hdr)
    print("-" * len(hdr))
    s4 = summarize(rows, 4, "realesr-animevideov3")
    for c in s4["classes"]:
        policy = c["upscale_policy"]
        pol = "upscale" if policy is True else ("redraw/downsample" if policy is False else "partial")
        print(
            f"{c['class']:16s} {c['files']:6d} {c['raw_px']/1e6:9.2f} {c['raw_bytes']/1048576:8.2f} "
            f"{c['alpha_files']:6d} {c['out_px']/1e6:11.2f} {c['out_bytes']/2**30:10.2f} {c['gpu_seconds']/3600:9.2f}  "
            f"{pol:20s} {', '.join(c['top_sizes'])}"
        )
    t = s4["total"]
    print("-" * len(hdr))
    print(f"{'TOTAL(4x)':16s} {t['files']:6d} {t['raw_px']/1e6:9.2f} {t['raw_bytes']/1048576:8.2f} {'':>6s} {t['out_px']/1e6:11.2f} {t['out_bytes']/2**30:10.2f} {t['gpu_hours']:9.2f}")
    print()
    for scale in (2, 3, 4):
        s = summarize(rows, scale, "realesr-animevideov3")
        tt = s["total"]
        print(f"  倍率 {scale}x：输出 {tt['out_px']/1e6:.1f} Mpx ≈ {fmt_bytes(tt['out_bytes'])}，"
              f"预计 GPU {tt['gpu_hours']:.2f} h（animevideov3）")
    s_x4pa = summarize(rows, 4, "realesrgan-x4plus-anime")
    print(f"  4x + realesrgan-x4plus-anime：预计 GPU {s_x4pa['total']['gpu_hours']:.2f} h")
    print()
    # 特殊尺寸 / alpha
    alpha_rows = [r for r in rows if r.get("has_alpha")]
    print(f"含 alpha 通道 PNG: {len(alpha_rows)} / {len(rows)}")
    print("宽或高异常（<=4px）:", sum(1 for r in rows if min(r['width'], r['height']) <= 4))
    print("宽>800:", sum(1 for r in rows if r['width'] > 800), " 高>600:", sum(1 for r in rows if r['height'] > 600),
          " 总计>800x600 像素:", sum(1 for r in rows if r['width'] * r['height'] > 480000))
    print("最大像素 top5:", [(r['name'], r['width'], r['height']) for r in sorted(rows, key=lambda r: -(r['width']*r['height']))[:5]])


def write_worklists(rows: list[dict], out_dir: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    by_class: dict[str, list[dict]] = {}
    for r in rows:
        by_class.setdefault(classify(r), []).append(r)
    for cls, items in sorted(by_class.items()):
        path = os.path.join(out_dir, f"{cls}.txt")
        with open(path, "w", encoding="utf-8") as fh:
            for r in sorted(items, key=lambda x: x["rel_path"]):
                fh.write(r["rel_path"].replace("\\", "/") + "\n")
        print(f"worklist {cls:16s} -> {path} ({len(items)} files)")


def main() -> int:
    ap = argparse.ArgumentParser(description="ef 高清化资源统计/清单生成（只读）")
    ap.add_argument("--stats-out", help="把统计 JSON 写到该路径")
    ap.add_argument("--emit-worklists", metavar="DIR", help="按类别输出 rel_path 清单到 DIR")
    ap.add_argument("--verify", action="store_true", help="校验 assets/raw 中文件真实存在")
    ap.add_argument("--dry-run", action="store_true", help="只做汇总（默认行为，清单不落盘）")
    args = ap.parse_args()

    rows_raw = load_rows()
    rows, overrides, same_name_groups = dedupe(rows_raw)
    print_report(rows, overrides, same_name_groups)

    stats = {f"{s}x": summarize(rows, s, "realesr-animevideov3") for s in (2, 3, 4)}
    stats["4x_x4plus_anime"] = summarize(rows, 4, "realesrgan-x4plus-anime")
    stats["_meta"] = {
        "generated_by": "tools/upscale/make_manifest.py",
        "manifest_dir": MANIFEST_DIR,
        "unique_png": len(rows),
        "r18_overrides": overrides,
        "same_basename_groups": same_name_groups,
        "sec_per_mpx_model": SEC_PER_MPX,
        "bytes_per_px": BYTES_PER_PX,
        "note": "体积/时间均为估算，±30%；实测依据见 work/upscaler/ 与 docs/UPSCALE-TOOLCHAIN.md",
    }

    if args.emit_worklists:
        write_worklists(rows, args.emit_worklists)

    if args.verify:
        missing = [r["rel_path"] for r in rows if not os.path.exists(os.path.join(RAW_DIR, r["rel_path"]))]
        print(f"verify: 缺失 {len(missing)} / {len(rows)}")
        for m in missing[:20]:
            print("  MISSING", m)

    if args.stats_out:
        os.makedirs(os.path.dirname(os.path.abspath(args.stats_out)), exist_ok=True)
        with open(args.stats_out, "w", encoding="utf-8") as fh:
            json.dump(stats, fh, ensure_ascii=False, indent=2)
        print(f"\n统计已写入 {args.stats_out}")
    elif not args.dry_run:
        print("\n（未指定 --stats-out，未写文件；加 --stats-out 输出 JSON）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
