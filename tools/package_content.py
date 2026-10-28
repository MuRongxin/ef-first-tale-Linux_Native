"""Build a complete local content package; publish one manifest after all imports succeed."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import csv
import hashlib
import json
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
from fractions import Fraction

from content_resources import ResourceIndex, audit
from import_scripts import import_scripts
from prepare_content import (DEFAULT_IMAGE, copy_blob, import_image, safe_path,
                             sha256_file, validate_output, write_json, thumbnail_sources)


def store_bytes(out, data, suffix, folder="blobs"):
    digest = hashlib.sha256(data).hexdigest()
    directory = safe_path(out, folder)
    directory.mkdir(exist_ok=True)
    path = safe_path(out, f"{folder}/{digest}{suffix}")
    if not path.is_file() or sha256_file(path) != digest:
        with tempfile.NamedTemporaryFile(dir=directory, prefix=".import-", delete=False) as file:
            temporary = Path(file.name)
            try:
                file.write(data)
                file.close()
                temporary.replace(path)
            finally:
                temporary.unlink(missing_ok=True)
    return path.relative_to(out).as_posix()


def store_json(out, value, suffix):
    return store_bytes(out, (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"), suffix)


def import_audio(game, out):
    from paz import PazArchive
    from paz_fast import PazArchivePlus
    entries, provenance = {}, {}
    for tag, relative in (("bgm", "bgm.paz"), ("se", "se.paz"), ("voice", "voice.paz"), ("r18_voice", "r18_cn/voice.paz")):
        source = game / relative
        if tag.startswith("r18_") and not source.is_file():
            continue
        digest = sha256_file(source)
        with PazArchivePlus(str(source)) as archive, PazArchive(str(source)) as reference:
            if not archive.entries:
                raise ValueError(f"空音频归档：{relative}")
            # Independently decrypt a deterministic spread, including first and last.
            samples = {0, len(archive.entries)-1, *random.Random(0).sample(range(len(archive.entries)), min(3, len(archive.entries)))}
            for number, item in enumerate(archive.entries):
                data = archive.read(item)
                if number in samples and data != reference.read(reference.entries[number]):
                    raise ValueError(f"音频基准对拍失败：{tag}/{item.name}")
                if data.startswith(b"OggS"):
                    fmt = "ogg"
                elif data.startswith(b"RIFF") and data[8:12] == b"WAVE":
                    fmt = "wav"
                else:
                    raise ValueError(f"未知音频格式：{tag}/{item.name}")
                name = item.name.replace("\\", "/")
                safe_path(out, name)
                id = f"{tag}/{name}"
                if id in entries:
                    raise ValueError(f"重复音频 ID：{id}")
                path = store_bytes(out, data, "." + fmt, "audio")
                entries[id] = {"kind": "voice" if tag == "r18_voice" else tag, "tag": tag, "name": name,
                               "path": path, "format": fmt, "bytes": len(data),
                               "source_sha256": hashlib.sha256(data).hexdigest()}
            if digest != sha256_file(source):
                raise ValueError(f"导入期间归档变化：{relative}")
            provenance[tag] = {"name": relative, "sha256": digest, "entries": len(archive.entries), "reference_samples": len(samples)}
        print(f"音频 {tag}: {provenance[tag]['entries']}，基准对拍 {len(samples)} 项通过", flush=True)
    return entries, provenance


def import_videos(project, out, game=None):
    raw = project / "assets/manifest/videos.json"
    hd = project / "assets/manifest/videos-hd.json"
    if raw.exists():
        sources = json.loads(raw.read_text())["entries"]
    elif game and (game / "mov.paz").is_file():
        from paz import PazArchive
        with PazArchive(str(game / "mov.paz")) as archive:
            sources = [{"name": e.name, "sha256": hashlib.sha256(archive.read(e)).hexdigest()} for e in archive.entries]
    else:
        return {}, ["未提供视频源清单/归档；视频引用将由审计报告为缺失"]
    targets = {Path(v["name"]).stem.lower(): v for v in json.loads(hd.read_text())["files"]} if hd.exists() else {}
    video_dir = project / "assets/hd/video"
    if video_dir.is_dir():
        discovered = {}
        for file in video_dir.iterdir():
            if file.suffix.lower() not in (".mkv", ".mp4") or not file.is_file():
                continue
            stem = file.stem.lower()
            if stem in discovered:
                raise ValueError(f"同名视频有多个来源：{stem}")
            discovered[stem] = file
        for stem, file in discovered.items():
            targets.setdefault(stem, {"name": file.name, "path": file.relative_to(project).as_posix()})
    entries, warnings = {}, []
    for source in sources:
        name = source["name"]
        target = targets.get(Path(name).stem.lower())
        if not target:
            warnings.append(f"缺少高清视频：{name}")
            continue
        source_path = safe_path(project, target["path"])
        properties = {k: target[k] for k in ("width", "height", "fps", "duration") if k in target}
        if shutil.which("ffprobe"):
            try:
                process = subprocess.run(["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(source_path)],
                                         check=True, text=True, capture_output=True, timeout=120)
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
                raise ValueError(f"视频探测失败：{source_path}") from error
            probe = json.loads(process.stdout)
            video = next((s for s in probe["streams"] if s.get("codec_type") == "video"), None)
            if not video or video.get("codec_name") != "h264":
                raise ValueError(f"视频不是 H.264：{source_path}")
            try:
                fps = float(Fraction(video["avg_frame_rate"]))
            except (ValueError, ZeroDivisionError) as error:
                raise ValueError(f"无法确认视频帧率：{source_path}") from error
            properties.update(width=video["width"], height=video["height"], codec="h264",
                              fps=fps, duration=float(probe["format"]["duration"]))
        else:
            warnings.append(f"未安装 ffprobe，视频 {name} 仅做复制/哈希校验")
        path, _ = copy_blob(source_path, out, source_path.suffix, folder="video")
        entries[f"mov/{name}"] = {"kind": "video", "tag": "mov", "name": name, "path": path,
                                  "source_sha256": source["sha256"], "sha256": Path(path).stem, **properties}
    return entries, warnings


def package(project, game, out, font, face, hd_only=False, *, decode_images=False, jobs=4):
    project, game, out = project.resolve(), game.resolve(), out.resolve()
    validate_output(project, out, game)
    if face < 0:
        raise ValueError("字体 face 必须非负")
    if not 1 <= jobs <= 8:
        raise ValueError("图像导入并发数必须为 1–8")
    out.mkdir(parents=True, exist_ok=True)
    safe_path(out, "blobs").mkdir(exist_ok=True)
    # A per-package lock prevents concurrent writers from mixing snapshots.
    import fcntl
    with safe_path(out, ".import.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with (project / "assets/manifest/images.csv").open(encoding="utf-8", newline="") as file:
            rows = list(csv.DictReader(file))
        images = [row for row in rows if row["kind"] == "png"]
        if not images:
            raise ValueError("图像清单为空")
        sources_path=project/'examples/thumbnail.sources.json'
        derived=thumbnail_sources(images,json.loads(sources_path.read_text()) if sources_path.is_file() else {}) if hd_only else {}
        entries = {row["rel_path"]: {"kind": "image", "name": row["rel_path"].split("/", 1)[1],
                                    "tag": row["rel_path"].split("/", 1)[0], "source_sha256": row["sha256"]} for row in images}
        if len(entries) != len(images):
            raise ValueError("图像清单含重复 ID")
        script_index = import_scripts(project, game, out, "gbk", publish=False)
        scripts = {}
        for id, spec in script_index["scripts"].items():
            scripts[id] = json.loads(safe_path(out, spec["path"]).read_text(encoding="utf-8")) if script_index["version"] == 2 else safe_path(out, spec["path"]).read_text(encoding="utf-8")
            entries[id] = {"kind": "script", "tag": id.split("/", 1)[0], "name": id.split("/", 1)[1],
                           "path": spec["path"], "source_sha256": spec["source_sha256"]}
        audio, archives = import_audio(game, out)
        entries.update(audio)
        videos, warnings = import_videos(project, out, game)
        entries.update(videos)
        report = audit(entries, scripts)
        print(f"资源引用审计：{report['references']} 次，缺失 {report['missing']}，歧义 {report['ambiguous']}", flush=True)
        manifest = {"version": 1, "design_size": [800, 600], "development": not hd_only, "images": {}}
        fallback = []
        pool = ThreadPoolExecutor(max_workers=jobs)
        try:
            def load_image(row):
                if row['rel_path'] in derived:
                    return {'design_size': [int(row['w']),int(row['h'])], 'source_sha256': row['sha256'],
                            'variants': {}, 'derived_from': derived[row['rel_path']]}
                return import_image(project,out,row,hd_only,decode=decode_images)
            imported = pool.map(load_image, images)
            for n, (row, spec) in enumerate(zip(images, imported), 1):
                manifest["images"][row["rel_path"]] = spec
                if "4" not in spec["variants"] and row['rel_path'] not in derived:
                    fallback.append({"id": row["rel_path"], "category": row.get("category", "unknown")})
                if n % 50 == 0 or n == len(images):
                    print(f"图像导入/{'完整解码' if decode_images else '哈希与尺寸校验'}：{n}/{len(images)}", flush=True)
        finally:
            pool.shutdown(wait=True, cancel_futures=True)
        font_path, _ = copy_blob(font, out, font.suffix.lower())
        manifest["font"] = {"path": font_path, "face": face}
        scene = json.loads((project / "examples/preview.scene.json").read_text())
        scene["layers"][0]["asset"] = DEFAULT_IMAGE if DEFAULT_IMAGE in entries else images[0]["rel_path"]
        manifest["entry_scene"] = store_json(out, scene, ".scene.json")
        manifest["script_index"] = store_json(out, script_index, ".scripts.json")
        manifest["resources"] = store_json(out, {"version": 1, "entries": entries}, ".resources.json")
        # Enumerate global filename ambiguities even if no script currently references them.
        index = ResourceIndex(entries)
        conflicts = []
        for kind, name in sorted(index.names):
            for r18 in (False, True):
                result = index.resolve(kind, name, r18)
                if result["status"] == "ambiguous":
                    conflicts.append({"kind": kind, "name": name, "r18": r18, **result})
        report.update({"counts": dict(Counter(e["kind"] for e in entries.values())),
                       "image_validation": "sha256+dimensions+decode" if decode_images else "sha256+dimensions",
                       "raw_only_images": fallback, "raw_only_categories": dict(Counter(x['category'] for x in fallback)),
                       "runtime_derived_images": derived,
                       "unconverted_animations": sum(row["kind"] == "ani" for row in rows),
                       "name_conflicts": conflicts, "warnings": warnings,
                       "audio_archives": archives})
        manifest["import_report"] = store_json(out, report, ".import-report.json")
        write_json(safe_path(out, "manifest.json"), manifest)
        print(json.dumps({k: report[k] for k in ("counts", "references", "unique_references", "missing", "ambiguous", "raw_only_categories")}, ensure_ascii=False), flush=True)
        print(f"全量资源包已原子更新：{out}；审计报告：{manifest['import_report']}", flush=True)
        return manifest
