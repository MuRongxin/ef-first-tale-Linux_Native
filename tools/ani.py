#!/usr/bin/env python3
"""离线读取 musica ANI 图块；不猜测口型/眨眼的播放时序。"""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct


@dataclass(frozen=True)
class Patch:
    name: str
    width: int
    height: int
    x: int
    y: int
    bgra: bytes


def decode(data: bytes) -> list[Patch]:
    if len(data) < 8 or len(data) > 64 * 1024 * 1024:
        raise ValueError('ANI size outside supported bounds')
    version, count, reserved = struct.unpack_from('<HHI', data)
    if version != 256 or reserved != 0 or not 1 <= count <= 256:
        raise ValueError('Unsupported ANI header')
    offset = 8
    patches = []
    for _ in range(count):
        end = data.find(b'\0', offset, offset + 1025)
        if end < 0:
            raise ValueError('Missing ANI record name terminator')
        name = data[offset:end].decode('cp932', errors='strict')
        offset = end + 1
        if not name or offset + 10 > len(data):
            raise ValueError('Truncated ANI record')
        width, height, depth, x, y = struct.unpack_from('<5H', data, offset)
        offset += 10
        size = width * height * 4
        if depth != 32 or (width == 0) != (height == 0) or offset + size > len(data):
            raise ValueError('Invalid ANI bitmap dimensions/depth or truncated pixels')
        patches.append(Patch(name, width, height, x, y, data[offset:offset + size]))
        offset += size
    if offset != len(data):
        raise ValueError('Unexpected trailing ANI bytes')
    return patches


def export(source: Path, destination: Path) -> dict:
    """导出原倍率 PNG + 元数据；零尺寸控制记录原样保留。"""
    from PIL import Image
    data = source.read_bytes()
    patches = decode(data)
    if destination.exists():
        raise ValueError('Export requires a new destination directory')
    destination.mkdir(parents=True)
    records = []
    for i, patch in enumerate(patches):
        record = dict(name=patch.name, size=[patch.width, patch.height], offset=[patch.x, patch.y])
        if patch.width:
            filename = f'{i:03d}.png'
            Image.frombytes('RGBA', (patch.width, patch.height), patch.bgra, 'raw', 'BGRA').save(destination / filename)
            record['path'] = filename
        records.append(record)
    result = dict(format='ef.ani.patches', version=1, source_sha256=hashlib.sha256(data).hexdigest(),
                  source_scale=1, playback='unresolved', records=records)
    (destination / 'anim.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--export', type=Path, help='new output directory for native-scale PNG patches and anim.json')
    args = parser.parse_args()
    if args.export:
        result = export(args.source, args.export)
    else:
        patches = decode(args.source.read_bytes())
        result = [dict(name=p.name, size=[p.width, p.height], offset=[p.x, p.y]) for p in patches]
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
