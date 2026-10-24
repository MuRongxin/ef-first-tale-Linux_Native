#!/usr/bin/env python3
"""alpha_pipeline.py — RGBA 立绘/UI 的 alpha-aware 超分管线（骨架，默认 --dry-run）

为什么需要它：
  realesrgan-ncnn-vulkan 直接对 RGBA 图做 4x 时，RGB 由模型生成、alpha 由
  重采样器生成。本机实测（work/upscaler/smoke/ 的 b_tachie_x4.png）表明
  alpha 重采样质量接近 Lanczos、形状正确，但半透明边缘的 RGB 由模型"编造"：
  源图 alpha≈0 处 RGB 常是深色/灰色垫底（tachie 实测边缘 RGB≈8，输出边缘
  RGB≈46~61），在深色背景上可能出现暗/亮描边。

本管线做法（标准 sprite 超分方案）：
  1. prepare：把 RGBA 拆成
       rgb.png  = 对 alpha 做 N 次膨胀填色（把透明区用最近的不透明颜色填充，
                  消除黑边来源；不预乘，避免颜色变暗）
       alpha.png= 原始 alpha 单通道
  2. upscale：对 rgb.png 调 realesrgan-ncnn-vulkan（可整目录批处理）做 S 倍
  3. merge：alpha 用 Lanczos 放大 S 倍 → 与放大后的 RGB 合成 RGBA PNG
     输出与直跑 ncnn 完全同尺寸同文件名，端口可直接换用。

默认 dry-run：只统计、打印计划；--execute 才真正调用 GPU / 写文件。
只写 --output-dir 与临时目录，绝不修改 assets/raw。

用法：
  # 先对 20 张抽样验证
  python3 tools/upscale/alpha_pipeline.py \
      --input-dir work/upscaler/smoke/batch_in --output-dir /tmp/alpha_out \
      --scale 4 --execute --limit 20
  # 通常由 run_realesrgan.sh --alpha-mode split 间接调用
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_BIN = "/home/lrxin/文档/realesrgan-ncnn-vulkan/realesrgan-ncnn-vulkan"
DEFAULT_MODELS = "/home/lrxin/文档/realesrgan-ncnn-vulkan/models"


def need_pillow_numpy():
    try:
        from PIL import Image  # noqa: F401
        import numpy  # noqa: F401
    except Exception as exc:  # pragma: no cover
        sys.exit(f"ERROR: 需要 Pillow 与 numpy（本机已装）：{exc}")


def dilate_matte(rgba, iterations: int):
    """把透明像素的 RGB 用邻域不透明颜色迭代填充（不改变 alpha，不做预乘）。"""
    import numpy as np

    img = rgba.copy()
    alpha = img[:, :, 3]
    known = alpha > 0
    if not known.any():
        return img
    for _ in range(max(1, iterations)):
        todo = ~known
        if not todo.any():
            break
        # 四邻域取均值填充
        acc = np.zeros(img[:, :, :3].shape, dtype=np.float64)
        cnt = np.zeros(alpha.shape, dtype=np.float64)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            shifted = np.roll(np.roll(img[:, :, :3], dy, axis=0), dx, axis=1)
            m = np.roll(np.roll(known, dy, axis=0), dx, axis=1)
            acc += shifted * m[:, :, None]
            cnt += m
        fill = todo & (cnt > 0)
        if not fill.any():
            break
        img[:, :, :3][fill] = (acc[fill] / cnt[fill][:, None]).astype(np.uint8)
        known = known | fill
    return img


def prepare(src_dir: str, tmp_rgb: str, tmp_alpha: str, limit: int, matte_iters: int) -> list[str]:
    from PIL import Image
    import numpy as np

    os.makedirs(tmp_rgb, exist_ok=True)
    os.makedirs(tmp_alpha, exist_ok=True)
    names = []
    for fn in sorted(os.listdir(src_dir)):
        if not fn.lower().endswith(".png"):
            continue
        if limit and len(names) >= limit:
            break
        p = os.path.join(src_dir, fn)
        with Image.open(p) as im:
            if im.mode != "RGBA":
                # 非 alpha 图直接交给 ncnn 原图通道
                im.convert("RGB").save(os.path.join(tmp_rgb, fn))
                Image.new("L", im.size, 255).save(os.path.join(tmp_alpha, fn))
            else:
                rgba = np.asarray(im.convert("RGBA"))
                matte = dilate_matte(rgba, matte_iters)
                Image.fromarray(matte[:, :, :3], "RGB").save(os.path.join(tmp_rgb, fn))
                Image.fromarray(rgba[:, :, 3], "L").save(os.path.join(tmp_alpha, fn))
        names.append(fn)
    return names


def run_ncnn(bin_path: str, models: str, rgb_dir: str, out_dir: str, scale: int,
             gpu: int, tile: int, jobs: str, fmt: str) -> int:
    os.makedirs(out_dir, exist_ok=True)  # ncnn 目录模式要求输出目录预先存在
    cmd = [bin_path, "-i", rgb_dir, "-o", out_dir, "-n", "realesr-animevideov3",
           "-s", str(scale), "-m", models, "-g", str(gpu), "-t", str(tile),
           "-j", jobs, "-f", fmt, "-v"]
    print("RUN:", " ".join(cmd))
    return subprocess.call(cmd)


def merge(rgb_up_dir: str, alpha_dir: str, out_dir: str, scale: int, names: list[str]) -> int:
    from PIL import Image
    import numpy as np

    os.makedirs(out_dir, exist_ok=True)
    ok = 0
    for fn in names:
        stem = os.path.splitext(fn)[0]
        rgb_p = os.path.join(rgb_up_dir, stem + ".png")
        a_p = os.path.join(alpha_dir, fn)
        if not (os.path.exists(rgb_p) and os.path.exists(a_p)):
            print("WARN: 缺少中间产物，跳过", fn, file=sys.stderr)
            continue
        rgb = Image.open(rgb_p).convert("RGB")
        alpha = Image.open(a_p).convert("L").resize(rgb.size, Image.LANCZOS)
        out = Image.merge("RGBA", (*rgb.split(), alpha))
        out.save(os.path.join(out_dir, stem + ".png"))
        ok += 1
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="RGBA alpha-aware 超分（默认 dry-run）")
    ap.add_argument("--input-dir", required=True)
    ap.add_argument("--output-dir", required=True)
    ap.add_argument("--scale", type=int, default=4, choices=(2, 3, 4))
    ap.add_argument("--bin", default=DEFAULT_BIN)
    ap.add_argument("--models", default=DEFAULT_MODELS)
    ap.add_argument("--gpu", type=int, default=1)
    ap.add_argument("--tile", type=int, default=0)
    ap.add_argument("--jobs", default="1:2:2")
    ap.add_argument("--format", default="png", choices=("png", "webp", "jpg"))
    ap.add_argument("--limit", type=int, default=0, help="只处理前 N 张（验收用）")
    ap.add_argument("--matte-iters", type=int, default=4, help="透明区填色膨胀次数")
    ap.add_argument("--dry-run", action="store_true", default=True)
    ap.add_argument("--execute", dest="dry_run", action="store_false")
    args = ap.parse_args()

    need_pillow_numpy()
    if args.format != "png":
        sys.exit("ERROR: alpha 输出必须是 png（RGBA）；-f 只保留 png 以保证端口兼容")
    if not os.path.isdir(args.input_dir):
        sys.exit(f"ERROR: 输入目录不存在 {args.input_dir}")
    if not args.dry_run and not (os.access(args.bin, os.X_OK) and os.path.isdir(args.models)):
        sys.exit(f"ERROR: 工具缺失：{args.bin} / {args.models}；见 docs/UPSCALE-TOOLCHAIN.md")

    files = [f for f in sorted(os.listdir(args.input_dir)) if f.lower().endswith(".png")]
    if args.limit:
        files = files[: args.limit]
    tmp = tempfile.mkdtemp(prefix="ef_alpha_")
    rgb_dir = os.path.join(tmp, "rgb")
    alpha_dir = os.path.join(tmp, "alpha")
    print(f"[alpha_pipeline] 输入 {args.input_dir}：{len(files)} 张，{args.scale}x")
    print(f"[alpha_pipeline] 临时目录 {tmp}（matter iters={args.matte_iters}）")
    if args.dry_run:
        for f in files[:5]:
            print("  plan:", f, "->", os.path.join(args.output_dir, os.path.splitext(f)[0] + ".png"))
        print(f"[DRY-RUN] 未执行。加 --execute 真跑；输出目录 {args.output_dir}")
        shutil.rmtree(tmp, ignore_errors=True)
        return 0

    try:
        names = prepare(args.input_dir, rgb_dir, alpha_dir, args.limit, args.matte_iters)
        up_dir = os.path.join(tmp, "rgb_up")
        rc = run_ncnn(args.bin, args.models, rgb_dir, up_dir, args.scale,
                      args.gpu, args.tile, args.jobs, "png")
        if rc != 0:
            print(f"ERROR: ncnn 返回 {rc}", file=sys.stderr)
            return rc
        ok = merge(up_dir, alpha_dir, args.output_dir, args.scale, names)
        print(f"[alpha_pipeline] 完成 {ok}/{len(names)} -> {args.output_dir}")
        return 0 if ok == len(names) else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
