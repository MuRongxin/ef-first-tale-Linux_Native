#!/usr/bin/env python3
"""一次性导入 scr/r18_scr：严格 GBK→UTF-8；原 PAZ 只读，运行时仅读 content。"""
import argparse
import hashlib
import json
from pathlib import Path

from paz import PazArchive
from script_json import compile_script, encode
from prepare_content import safe_path, write_json


def transcode(data: bytes, encoding: str = "gbk") -> bytes:
    # Decode before interpreting backslashes: GBK trail bytes can equal 0x5C.
    text = data.decode(encoding, errors="strict")
    if "\0" in text:
        raise ValueError("脚本包含 NUL，不能当作文本导入")
    if text.encode(encoding, errors="strict") != data:
        raise ValueError("转码无法按位往返")
    return text.encode("utf-8")


def import_message_ui(project: Path, out: Path, index: dict) -> None:
    source = project / "examples/message.ui.json"
    if not source.is_file():
        return
    document = json.loads(source.read_text(encoding="utf-8"))
    if document.get("version") != 1 or document.get("design_size") != [800, 600]:
        raise ValueError("不支持的消息窗布局版本")
    encoded = (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    path = f"blobs/{hashlib.sha256(encoded).hexdigest()}.message.ui.json"
    write_json(safe_path(out, path), document)
    index["message_ui"] = path


def refresh_message_ui(project: Path, out: Path) -> dict:
    """只原子更新布局和脚本索引，不重新复制图像/音频/视频。"""
    project, out = project.resolve(), out.resolve()
    if not out.is_relative_to(project) or out == project or out.is_relative_to(project / ".git"):
        raise ValueError("输出必须是工程内的 content 数据目录")
    for protected in ("assets", "src", "docs", "tools", "tests", "examples"):
        if out.is_relative_to(project / protected):
            raise ValueError("输出不能覆盖工程源文件")
    import fcntl
    with safe_path(out, ".import.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        manifest = json.loads(safe_path(out, "manifest.json").read_text(encoding="utf-8"))
        index = json.loads(safe_path(out, manifest.get("script_index", "scripts/index.json")).read_text(encoding="utf-8"))
        source = project / "examples/message.ui.json"
        ui = json.loads(source.read_text(encoding="utf-8"))
        required = {ui["advance"]["asset"]}
        for mode in ui["modes"].values():
            required.update(image["asset"] for image in mode["images"])
        missing = sorted(required - manifest["images"].keys())
        if missing:
            raise ValueError(f"消息窗资源缺失：{missing}")
        import_message_ui(project, out, index)
        encoded = (json.dumps(index, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        path = f"blobs/{hashlib.sha256(encoded).hexdigest()}.scripts.json"
        write_json(safe_path(out, path), index)
        manifest["script_index"] = path
        write_json(safe_path(out, "manifest.json"), manifest)
    return index

def import_scripts(project: Path, game: Path, out: Path, encoding: str, *, publish=True) -> dict:
    project, game, out = project.resolve(), game.resolve(), out.resolve()
    if not out.is_relative_to(project) or out == project or out.is_relative_to(game):
        raise ValueError("输出必须在工程子目录，不能写入原游戏目录")
    for protected in ("assets", "src", "docs", "tools", "tests", "examples", ".git"):
        if out.is_relative_to(project / protected):
            raise ValueError("输出不能覆盖工程源文件")
    if publish and not (out / "manifest.json").is_file():
        raise ValueError("请先运行 tools/prepare_content.py 准备 content 包")
    if publish and "resources" in json.loads((out / "manifest.json").read_text()):
        raise ValueError("全量包请通过 prepare_content.py --all 原子更新，避免剧本与资源目录不同步")
    scripts_dir = safe_path(out, "scripts")
    scripts_dir.mkdir(exist_ok=True)
    blobs = safe_path(out, "blobs")
    blobs.mkdir(exist_ok=True)
    index = {"version": 2, "format": "ef.script.v1", "encoding": "utf-8", "scripts": {}, "source_archives": {}}
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
                utf8_digest = hashlib.sha256(converted).hexdigest()
                converted = encode(compile_script(converted.decode("utf-8"), f"{tag}/{name}"))
                digest = hashlib.sha256(converted).hexdigest()
                path = f"blobs/{digest}.script.json"
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
                    "utf8_sha256": utf8_digest, "json_sha256": digest, "source_encoding": encoding,
                    "lines": len(original.decode(encoding).split("\n")),
                }
    view = json.loads((project / "examples/script.scene.json").read_text(encoding="utf-8"))
    view_data = (json.dumps(view, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    view_path = f"blobs/{hashlib.sha256(view_data).hexdigest()}.script.scene.json"
    write_json(safe_path(out, view_path), view)
    index["view"] = view_path
    import_message_ui(project, out, index)
    if publish:
        write_json(safe_path(out, "scripts/index.json"), index)
    print(f"已严格转码并编译为JSON，导入 {len(index['scripts'])} 个脚本", flush=True)
    return index


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--game-root", type=Path, help="只读的原版游戏目录")
    parser.add_argument("--out", type=Path, default=Path("content"))
    parser.add_argument("--encoding", choices=("gbk", "cp932"), default="gbk")
    parser.add_argument("--refresh-ui", action="store_true", help="仅更新已有包的消息窗布局，不读取原游戏或重打包资源")
    args = parser.parse_args()
    try:
        if args.refresh_ui:
            refresh_message_ui(args.project_root, args.project_root / args.out)
            print("消息窗布局已原子更新", flush=True)
        elif args.game_root:
            import_scripts(args.project_root, args.game_root, args.project_root / args.out, args.encoding)
        else:
            parser.error("导入剧本需要 --game-root；已有资源包可用 --refresh-ui")
    except (ValueError, OSError) as error:
        parser.exit(1, f"import_scripts: {error}\n")


if __name__ == "__main__":
    main()
