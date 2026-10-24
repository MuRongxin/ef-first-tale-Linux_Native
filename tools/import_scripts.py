#!/usr/bin/env python3
"""一次性导入 scr/r18_scr：严格 GBK→UTF-8；原 PAZ 只读，运行时仅读 content。"""
import argparse
import hashlib
import json
from pathlib import Path

from paz import PazArchive
from prepare_content import safe_path, write_json


def transcode(data: bytes, encoding: str = "gbk") -> bytes:
    # Decode before interpreting backslashes: GBK trail bytes can equal 0x5C.
    text = data.decode(encoding, errors="strict")
    if "\0" in text:
        raise ValueError("脚本包含 NUL，不能当作文本导入")
    if text.encode(encoding, errors="strict") != data:
        raise ValueError("转码无法按位往返")
    return text.encode("utf-8")


def import_scripts(project: Path, game: Path, out: Path, encoding: str) -> dict:
    project, game, out = project.resolve(), game.resolve(), out.resolve()
    if not out.is_relative_to(project) or out == project or out.is_relative_to(game):
        raise ValueError("输出必须在工程子目录，不能写入原游戏目录")
    for protected in ("assets", "src", "docs", "tools", "tests", "examples", ".git"):
        if out.is_relative_to(project / protected):
            raise ValueError("输出不能覆盖工程源文件")
    if not (out / "manifest.json").is_file():
        raise ValueError("请先运行 tools/prepare_content.py 准备 content 包")
    scripts_dir = safe_path(out, "scripts")
    scripts_dir.mkdir(exist_ok=True)
    blobs = safe_path(out, "blobs")
    blobs.mkdir(exist_ok=True)
    index = {"version": 1, "encoding": "utf-8", "scripts": {}, "source_archives": {}}
    for tag, relative in (("scr", "scr.paz"), ("r18_scr", "r18_cn/scr.paz")):
        archive_path = game / relative
        if tag == "r18_scr" and not archive_path.is_file():
            continue
        index["source_archives"][tag] = {"name": relative, "sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest()}
        with PazArchive(str(archive_path)) as archive:
            for entry in archive.entries:
                if not entry.name.lower().endswith(".sc"):
                    continue
                name = entry.name.replace("\\", "/")
                safe_path(out, name)
                original = archive.read(entry)
                converted = transcode(original, encoding)
                digest = hashlib.sha256(converted).hexdigest()
                path = f"blobs/{digest}.sc"
                # Immutable content-addressed files; publish the index only after all succeed.
                target = safe_path(out, path)
                if not target.exists() or target.read_bytes() != converted:
                    temporary = safe_path(out, path + ".import")
                    try:
                        temporary.write_bytes(converted)
                        temporary.replace(target)
                    finally:
                        temporary.unlink(missing_ok=True)
                index["scripts"][f"{tag}/{name}"] = {
                    "path": path, "source_sha256": hashlib.sha256(original).hexdigest(),
                    "utf8_sha256": digest, "source_encoding": encoding,
                    "lines": len(converted.decode("utf-8").splitlines()),
                }
    view = json.loads((project / "examples/script.scene.json").read_text(encoding="utf-8"))
    view_data = (json.dumps(view, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    view_path = f"blobs/{hashlib.sha256(view_data).hexdigest()}.script.scene.json"
    write_json(safe_path(out, view_path), view)
    index["view"] = view_path
    write_json(safe_path(out, "scripts/index.json"), index)
    print(f"已严格转码并导入 {len(index['scripts'])} 个脚本：{out / 'scripts/index.json'}")
    return index


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--game-root", type=Path, default=Path("/home/lrxin/Downloads/ef - the first tale"))
    parser.add_argument("--out", type=Path, default=Path("content"))
    parser.add_argument("--encoding", choices=("gbk", "cp932"), default="gbk")
    args = parser.parse_args()
    try:
        import_scripts(args.project_root, args.game_root, args.project_root / args.out, args.encoding)
    except (ValueError, OSError) as error:
        parser.exit(1, f"import_scripts: {error}\n")


if __name__ == "__main__":
    main()
