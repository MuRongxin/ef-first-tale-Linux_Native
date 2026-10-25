#!/usr/bin/env python3
"""从已解包素材与已完成 HD 产物生成小型、独立的 content 预览包。

不读 PAZ，不触碰远端任务；所有写入限制在 project-root 下。
包内使用内容哈希命名，manifest 最后原子替换；正在运行的预览可按 R 重载。
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile

from PIL import Image

DEFAULT_IMAGE = "bg/prologue_mugai_e.png"


def sha256_file(path: Path) -> str:
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def validate_output(project: Path, out: Path, game: Path | None = None) -> None:
    project, out = project.resolve(), out.resolve()
    if out == project or not out.is_relative_to(project):
        raise ValueError("输出目录必须是工程目录下的子目录")
    if game and out.is_relative_to(game.resolve()):
        raise ValueError("不能写入原游戏目录")
    for protected in ("assets", "src", "docs", "tools", "tests", "examples", "cmake", ".git", ".github"):
        if out.is_relative_to(project / protected):
            raise ValueError(f"输出不能覆盖工程源文件：{out}")


def safe_path(root: Path, relative: str) -> Path:
    path = PurePosixPath(relative)
    if not relative or not path.parts or path.is_absolute() or ".." in path.parts or "\\" in relative or "\0" in relative:
        raise ValueError(f"非法相对路径：{relative}")
    result = (root / path).resolve()
    if not result.is_relative_to(root.resolve()):
        raise ValueError(f"路径逃逸：{relative}")
    return result


def hd_name(name: str) -> str:
    # Match ef_client.strip_known_ext, including the packer's fake extensions.
    lower = name.lower()
    for suffix in (".png.tmp", ".png-", ".png"):
        if lower.endswith(suffix):
            return name[:-len(suffix)] + ".webp"
    if ".png" in lower:
        return name[:lower.index(".png")] + ".webp"
    return str(PurePosixPath(name).with_suffix(".webp"))


def find_font() -> tuple[Path, int]:
    if shutil.which("fc-match"):
        result = subprocess.run(
            ["fc-match", "-f", "%{file}\n%{index}\n", "Noto Sans CJK SC"],
            check=True, text=True, capture_output=True,
        ).stdout.splitlines()
        if len(result) >= 2 and "NotoSansCJK" in result[0] and Path(result[0]).is_file():
            return Path(result[0]), int(result[1])
    raise ValueError("未找到 Noto Sans CJK SC；安装 fonts-noto-cjk 或用 --font 指定中文字体")


def copy_blob(source: Path, out: Path, suffix: str, *, dimensions=None, sha256=None, folder="blobs", decode=True) -> tuple[str, list[int] | None]:
    """Independent, verified copies; reuse content-addressed files without hardlinks."""
    blobs = safe_path(out, folder)
    blobs.mkdir(exist_ok=True)
    digest = sha256_file(source)
    if sha256 and digest != sha256:
        raise ValueError(f"原始素材 SHA-256 与 manifest 不符：{source}")
    destination = safe_path(out, f"{folder}/{digest}{suffix}")

    def check_image(path):
        if dimensions is None:
            return None
        with Image.open(path) as image:
            if image.format not in ("PNG", "WEBP"):
                raise ValueError(f"运行时仅支持 PNG/WebP，不能导入 {image.format}：{source}")
            size = list(image.size)
            if tuple(size) not in dimensions:
                raise ValueError(f"{source}: 尺寸 {size} 不在允许值 {dimensions}")
            if getattr(image, "is_animated", False):
                raise ValueError(f"不能将动画当作静态图片导入：{source}")
            if decode:
                image.load()
        return size

    if destination.is_file() and sha256_file(destination) == digest:
        return destination.relative_to(out).as_posix(), check_image(destination)
    with tempfile.NamedTemporaryFile(dir=blobs, prefix=".import-", delete=False) as file:
        temporary = Path(file.name)
    try:
        shutil.copyfile(source, temporary)
        if sha256_file(temporary) != digest:
            raise ValueError(f"复制期间源文件变化：{source}")
        size = check_image(temporary)
        os.replace(temporary, destination)
        return destination.relative_to(out).as_posix(), size
    finally:
        temporary.unlink(missing_ok=True)


def import_image(project: Path, out: Path, row: dict, hd_only=False, *, decode=True) -> dict:
    relative = row["rel_path"]
    if row["kind"] != "png":
        raise ValueError(f"不能将 .ani/PSD 当作静态图像导入：{relative}")
    width, height = int(row["w"]), int(row["h"])
    if width <= 0 or height <= 0:
        raise ValueError(f"非法 design_size：{relative}")
    entry = {"design_size": [width, height], "source_sha256": row["sha256"], "variants": {}}
    tag, name = relative.split("/", 1)
    for scale in ([4, 8] if hd_only else [1, 4, 8]):
        if scale == 1:
            source = safe_path(project / "assets/raw", relative)
        else:
            webp = safe_path(project / "assets/hd", f"{tag}_{scale}x/{hd_name(name)}")
            png = webp.with_suffix(".png")
            if webp.is_file() and png.is_file():
                raise ValueError(f"同一档位存在两个高清来源，请明确保留一个：{relative} {scale}x")
            source = webp if webp.is_file() else safe_path(project / "assets/hd", png.relative_to(project / "assets/hd").as_posix())
        if not source.is_file():
            if scale == 1:
                raise ValueError(f"原始素材缺失：{source}")
            continue
        allowed = [(width * scale, height * scale)]
        if scale == 8 and (width * 8 > 16383 or height * 8 > 16383):
            allowed.append((width * 4, height * 4))
        suffix = ".png" if scale == 1 else source.suffix
        path, pixels = copy_blob(source, out, suffix, dimensions=allowed, sha256=row["sha256"] if scale == 1 else None, decode=decode)
        entry["variants"][str(scale)] = {"path": path, "pixels": pixels}
    if not entry["variants"] or (hd_only and "4" not in entry["variants"]):
        raise ValueError(f"缺少可用 4× 素材：{relative}")
    return entry


def write_json(path: Path, value) -> None:
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, encoding="utf-8", delete=False) as file:
        temporary = Path(file.name)
        json.dump(value, file, ensure_ascii=False, indent=2)
        file.write("\n")
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def prepare(project: Path, out: Path, images: list[str], font: Path, face: int, hd_only=False) -> dict:
    project, out = project.resolve(), out.resolve()
    validate_output(project, out)
    if face < 0:
        raise ValueError("字体 face 必须非负")
    out.mkdir(parents=True, exist_ok=True)
    if (out / "manifest.json").is_file() and "resources" in json.loads((out / "manifest.json").read_text()):
        raise ValueError("此目录已有全量资源包；请用 --all 更新，或 --out 指定独立预览目录")
    # Refuse a symlinked blob store before any copy/write.
    safe_path(out, "blobs")
    with (project / "assets/manifest/images.csv").open(encoding="utf-8", newline="") as file:
        inventory = {row["rel_path"]: row for row in csv.DictReader(file)}
    manifest = {"version": 1, "design_size": [800, 600], "development": not hd_only, "images": {}}
    for relative in dict.fromkeys(images):
        if relative not in inventory:
            raise ValueError(f"素材不在 images.csv 中：{relative}")
        entry = import_image(project, out, inventory[relative], hd_only)
        manifest["images"][relative] = entry
        print(f"{relative}: design={entry['design_size']}, tiers={','.join(entry['variants'])}")
    font_path, _ = copy_blob(font, out, font.suffix.lower())
    manifest["font"] = {"path": font_path, "face": face}
    scene = json.loads((project / "examples/preview.scene.json").read_text(encoding="utf-8"))
    scene["layers"][0]["asset"] = images[0]
    for relative in reversed(list(dict.fromkeys(images))[1:]):
        width, height = manifest["images"][relative]["design_size"]
        scene["layers"].insert(1, {"kind": "image", "asset": relative, "x": (800-width)/2, "y": 600-height})
    scene_data = (json.dumps(scene, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    scene_name = f"blobs/{hashlib.sha256(scene_data).hexdigest()}.scene.json"
    write_json(safe_path(out, scene_name), scene)
    manifest["entry_scene"] = scene_name
    write_json(safe_path(out, "manifest.json"), manifest)
    print(f"content 预览包已生成：{out}（仅复制本次选中的资源）")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--out", type=Path, default=Path("content"), help="相对 project-root；默认 content")
    parser.add_argument("--image", action="append", help=f"images.csv 的 rel_path；可重复，第一个为背景；默认 {DEFAULT_IMAGE}")
    parser.add_argument("--font", type=Path, help="中文字体；缺省使用系统 Noto Sans CJK SC（复制进 content）")
    parser.add_argument("--font-face", type=int, help="TTC face index；显式 --font 默认 0，自动选择时使用 fontconfig index")
    parser.add_argument("--hd-only", action="store_true", help="只导入 HD，要求所有选中图像已有 4×；不带开发用 1× 回退")
    parser.add_argument("--all", action="store_true", help="准备全量图片/剧本/原音频/视频映射并审计资源引用")
    parser.add_argument("--game-root", type=Path, help="--all 必需：只读的原版游戏目录")
    parser.add_argument("--decode-images", action="store_true", help="--all 导入时额外完整解码每个图像；默认校验哈希、格式和尺寸")
    parser.add_argument("--jobs", type=int, default=4, help="全量图像导入并发数，1–8（默认 4）")
    args = parser.parse_args()
    try:
        font, face = (args.font, 0) if args.font else find_font()
        if args.font_face is not None:
            face = args.font_face
        if args.all:
            if not args.game_root or args.image:
                raise ValueError("--all 必须指定 --game-root，且不能与 --image 同用")
            from package_content import package
            package(args.project_root, args.game_root, args.project_root / args.out, font, face, args.hd_only, decode_images=args.decode_images, jobs=args.jobs)
        else:
            if args.decode_images:
                raise ValueError("--decode-images 只用于 --all；小型预览默认完整解码")
            prepare(args.project_root, args.project_root / args.out, args.image or [DEFAULT_IMAGE], font, face, args.hd_only)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"prepare_content: {error}\n")


if __name__ == "__main__":
    main()
