#!/usr/bin/env python3
"""compare_montage.py — 超分结果 QA：生成「原图 bicubic 放大 vs AI 超分」并排对比图

用途：批量跑完后抽查 10~30 张，人工确认没有色偏、鬼影、文字糊、透明边描边。
只读输入/输出目录，写 --out-dir（默认为 work/upscaler/qa/）。

用法：
  python3 tools/upscale/compare_montage.py \
      --src assets/raw --dst work/upscaler/out/4x --class cg --limit 20 \
      --out-dir work/upscaler/qa

输出：每张图 <stem>.compare.png（左=bicubic 放大，右=AI 超分）+ 汇总 qa-index.txt
"""
from __future__ import annotations

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def main() -> int:
    ap = argparse.ArgumentParser(description="超分 QA 对比图生成")
    ap.add_argument("--src", default=os.path.join(ROOT, "assets", "raw"))
    ap.add_argument("--dst", required=True, help="超分输出目录（类别目录的父目录，或类别目录本身）")
    ap.add_argument("--class", dest="cls", default="",
                    help="类别子目录名，如 cg/tachie/bg；留空则递归")
    ap.add_argument("--list-file", help="可选：只处理清单里的 rel_path（一行一个）")
    ap.add_argument("--limit", type=int, default=10)
    ap.add_argument("--crop", type=int, default=256, help="对比裁剪边长（原图坐标，0=整图缩略）")
    ap.add_argument("--out-dir", default=os.path.join(ROOT, "work", "upscaler", "qa"))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    try:
        from PIL import Image
    except Exception as exc:  # pragma: no cover
        sys.exit(f"ERROR: 需要 Pillow：{exc}")

    dst_root = os.path.join(args.dst, args.cls) if args.cls else args.dst
    if not os.path.isdir(dst_root):
        sys.exit(f"ERROR: 输出目录不存在 {dst_root}")

    rels: list[str] = []
    if args.list_file:
        rels = [ln.strip() for ln in open(args.list_file) if ln.strip()]
    else:
        for dirpath, _dirs, files in os.walk(dst_root):
            for f in files:
                if f.lower().endswith(".png"):
                    out_p = os.path.join(dirpath, f)
                    rel_p = os.path.relpath(out_p, dst_root).replace("\\", "/")
                    rels.append(rel_p)
        rels.sort()
    if args.limit:
        rels = rels[: args.limit]
    if not rels:
        sys.exit("ERROR: 没有可比对的输出文件")

    os.makedirs(args.out_dir, exist_ok=True)
    index = []
    for rel in rels:
        out_p = os.path.join(dst_root, rel)
        src_p = os.path.join(args.src, rel)
        if not os.path.exists(src_p):
            print("WARN: 源图缺失", src_p, file=sys.stderr)
            continue
        src = Image.open(src_p).convert("RGBA")
        dst = Image.open(out_p).convert("RGBA")
        scale = round(dst.width / max(1, src.width))
        if args.crop and src.width > args.crop and src.height > args.crop:
            x = (src.width - args.crop) // 2
            y = (src.height - args.crop) // 2
            src_c = src.crop((x, y, x + args.crop, y + args.crop))
            dst_c = dst.crop((x * scale, y * scale, (x + args.crop) * scale, (y + args.crop) * scale))
        else:
            src_c, dst_c = src, dst
        left = src_c.resize(dst_c.size, Image.BICUBIC)
        # 垫深灰背景看清 alpha 边缘
        def flat(im):
            bg = Image.new("RGBA", im.size, (32, 32, 32, 255))
            bg.alpha_composite(im)
            return bg.convert("RGB")

        left, right = flat(left), flat(dst_c)
        mont = Image.new("RGB", (left.width * 2 + 8, left.height), (16, 16, 16))
        mont.paste(left, (0, 0))
        mont.paste(right, (left.width + 8, 0))
        out_name = os.path.splitext(rel.replace("/", "_"))[0] + ".compare.png"
        if not args.dry_run:
            mont.save(os.path.join(args.out_dir, out_name))
        index.append(f"{out_name}\t{src.width}x{src.height} -> {dst.width}x{dst.height}\t{rel}")
        print(("DRY " if args.dry_run else "") + out_name)

    with open(os.path.join(args.out_dir, "qa-index.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(index) + "\n")
    print(f"\n共 {len(index)} 张 -> {args.out_dir}（左 bicubic / 右 AI，深灰垫底）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
