#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""extract_videos.py — 解包 mov.paz 中的视频资源（AVI / MPEG-PS）。

本工程的图像管线由 `extract_assets.py` 负责；视频归档（mov.paz）是独立的小集合
（4 个文件 / 51 MB，未压缩存储），且 PAZ 对 mov 有特殊处理（TOC 内 256B seed
派生 RC4 keystream 循环 XOR），由 `tools/paz.py` / `paz_fast.py` 的 `read()`
实现。本脚本只做：

  1. 用快速实现解出 mov.paz 的每个条目 → `assets/raw/mov/<name>`；
  2. 用基准实现（tools/paz.py）独立重读同一批条目，逐字节对拍（不通过即报错）；
  3. 校验文件头（RIFF/AVI、MPEG-PS）、记录 SHA-256 / 大小；
  4. 若本机有 ffprobe，附带格式/时长/分辨率/帧率/音轨探测结果；
  5. 写 `assets/manifest/videos.json`。

用法：
    python3 tools/extract_videos.py --game-root "/path/to/game"
    python3 tools/extract_videos.py --game-root ... --out-dir assets/raw/mov
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import paz        # noqa: E402  基准实现（纯 Python）
import paz_fast   # noqa: E402  加速实现（numpy + C RC4）


MAGIC = {
    "avi": (b"RIFF", b"AVI "),    # offset 0 与 8
    "mpg": (b"\x00\x00\x01\xba",),
    "mpeg": (b"\x00\x00\x01\xba",),
}


def sniff_format(name: str, data: bytes) -> str:
    ext = name.rsplit(".", 1)[-1].lower() if "." in name else ""
    if data[:4] == b"RIFF" and data[8:12] == b"AVI ":
        return "AVI"
    if data[:4] == b"\x00\x00\x01\xba":
        return "MPEG-PS"
    if data[:4] in (b"\x00\x00\x01\xb3", b"\x00\x00\x01\xba"):
        return "MPEG"
    raise ValueError(f"{name}: unknown video magic {data[:12]!r} (ext={ext})")


def probe(path: Path) -> dict:
    if not shutil.which("ffprobe"):
        return {"available": False}
    cmd = ["ffprobe", "-v", "error", "-show_format", "-show_streams",
           "-of", "json", str(path)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except Exception as exc:  # noqa: BLE001
        return {"available": False, "error": str(exc)}
    if out.returncode != 0:
        return {"available": True, "error": out.stderr.strip()[:300]}
    raw = json.loads(out.stdout or "{}")
    fmt = raw.get("format") or {}
    video = next((s for s in raw.get("streams", [])
                  if s.get("codec_type") == "video"), {})
    audio = next((s for s in raw.get("streams", [])
                  if s.get("codec_type") == "audio"), {})
    fps = video.get("avg_frame_rate") or video.get("r_frame_rate") or ""
    if "/" in fps:
        try:
            num, den = fps.split("/")
            fps = round(float(num) / float(den), 3) if float(den) else fps
        except ValueError:
            pass
    return {
        "available": True,
        "format_name": fmt.get("format_name"),
        "duration_s": round(float(fmt.get("duration", 0.0) or 0.0), 3),
        "bit_rate": int(fmt.get("bit_rate") or 0),
        "video": {
            "codec": video.get("codec_name"),
            "width": video.get("width"),
            "height": video.get("height"),
            "fps": fps,
            "pix_fmt": video.get("pix_fmt"),
            "nb_frames": video.get("nb_frames"),
        } if video else None,
        "audio": {
            "codec": audio.get("codec_name"),
            "sample_rate": audio.get("sample_rate"),
            "channels": audio.get("channels"),
        } if audio else None,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-root", required=True,
                    help="原版游戏目录（只读），其中含 mov.paz")
    ap.add_argument("--out-dir", default="assets/raw/mov")
    ap.add_argument("--manifest", default="assets/manifest/videos.json")
    ap.add_argument("--no-reference", action="store_true",
                    help="跳过基准实现对拍（不建议）")
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent
    game_root = Path(args.game_root).expanduser().resolve()
    archive = game_root / "mov.paz"
    if not archive.is_file():
        sys.exit(f"mov.paz not found under {game_root}")
    out_dir = (root / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"archive: {archive} ({archive.stat().st_size/1e6:.1f} MB)")
    print(f"out    : {out_dir}")
    t0 = time.time()
    rows = []
    with paz_fast.PazArchivePlus(str(archive)) as fast:
        ref = None
        if not args.no_reference:
            ref = paz.PazArchive(str(archive))
        print(f"game keys: {fast.game}; entries: {len(fast.entries)}")
        for entry in fast.entries:
            name = entry.name
            data = fast.read(entry)
            if ref is not None:
                ref_data = ref.read(entry)
                if hashlib.sha256(ref_data).digest() != hashlib.sha256(data).digest():
                    raise SystemExit(f"reference mismatch on {name}")
                del ref_data
            fmt = sniff_format(name, data)
            dst = out_dir / name
            dst.write_bytes(data)
            row = {
                "name": name,
                "path": str(dst.relative_to(root)),
                "format": fmt,
                "size": len(data),
                "raw_size": entry.raw_size,
                "sha256": hashlib.sha256(data).hexdigest(),
                "probe": probe(dst),
            }
            rows.append(row)
            p = row["probe"]
            v = (p.get("video") or {}) if p.get("available") else {}
            a = (p.get("audio") or {}) if p.get("available") else {}
            print(f"  {name:<22} {fmt:<8} {len(data)/1e6:7.1f} MB  "
                  f"{v.get('width')}x{v.get('height')} {v.get('fps')}fps "
                  f"{p.get('duration_s','?')}s "
                  f"v={v.get('codec')} a={a.get('codec')}")
        if ref is not None:
            ref.close()

    manifest = {
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "generator": "tools/extract_videos.py",
        "archive": str(archive),
        "archive_size": archive.stat().st_size,
        "entries": rows,
        "totals": {
            "files": len(rows),
            "bytes": sum(r["size"] for r in rows),
            "reference_crosscheck": "skipped" if args.no_reference else "pass",
        },
    }
    man_path = root / args.manifest
    man_path.parent.mkdir(parents=True, exist_ok=True)
    man_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                        encoding="utf-8")
    print(f"\n{len(rows)} files, {manifest['totals']['bytes']/1e6:.1f} MB → {man_path}")
    print(f"elapsed {time.time()-t0:.1f}s; reference crosscheck: "
          f"{manifest['totals']['reference_crosscheck']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
