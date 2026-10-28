#!/usr/bin/env python3
"""离线生成程序化资源的 4×/8×；遮罩与色带保留离散样本，不使用 AI。"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import hashlib
import json
import os
from pathlib import Path
from PIL import Image
from prepare_content import hd_name, safe_path, write_json


def generate(project: Path, row: dict) -> dict:
    if row['kind'] != 'png' or row['method'] != 'procedural_regen':
        raise ValueError('只接受 procedural_regen PNG')
    source = safe_path(project / 'assets/raw', row['rel_path'])
    if hashlib.sha256(source.read_bytes()).hexdigest() != row['sha256']:
        raise ValueError(f'源哈希不符：{source}')
    with Image.open(source) as image:
        image.load()
        if image.size != (int(row['w']), int(row['h'])):
            raise ValueError(f'源尺寸不符：{source}')
        original = image.convert('RGBA')
    # Control maps must retain exact samples; UI alpha/geometry gets conventional
    # premultiplied-alpha reconstruction, not invented model detail.
    sampling = Image.Resampling.NEAREST if row['category'] in ('mask', 'tint') else Image.Resampling.LANCZOS
    tag, name = row['rel_path'].split('/', 1)
    outputs = []
    for scale in (4, 8):
        dest = safe_path(project / 'assets/hd', f'{tag}_{scale}x/{Path(hd_name(name)).with_suffix(".png")}')
        if dest.with_suffix('.webp').exists():
            raise ValueError(f'存在冲突的 WebP：{dest}')
        dest.parent.mkdir(parents=True, exist_ok=True)
        scaled = original.resize((original.width * scale, original.height * scale), sampling)
        temp = dest.with_suffix('.partial')
        try:
            scaled.save(temp, 'PNG', compress_level=3)
            with Image.open(temp) as check:
                check.load()
                if check.size != scaled.size:
                    raise ValueError(f'生成尺寸不符：{dest}')
            os.replace(temp, dest)
        finally:
            temp.unlink(missing_ok=True)
        outputs.append({'path': dest.relative_to(project).as_posix(), 'pixels': list(scaled.size),
                        'sha256': hashlib.sha256(dest.read_bytes()).hexdigest()})
    return {'source': row['rel_path'], 'source_sha256': row['sha256'],
            'method': 'nearest_sample_preserving' if sampling == Image.Resampling.NEAREST else 'lanczos_rgba',
            'outputs': outputs}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--jobs', type=int, choices=range(1, 9), default=2)
    args = parser.parse_args()
    project = args.project_root.resolve()
    with (project / 'assets/manifest/images.csv').open(newline='') as file:
        rows = [r for r in csv.DictReader(file) if r['kind'] == 'png' and r['method'] == 'procedural_regen']
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda row: generate(project, row), rows))
    report = project / 'work/hd-replacement/procedural.json'
    report.parent.mkdir(parents=True, exist_ok=True)
    write_json(report, {'images': len(results), 'records': results})
    print(json.dumps({'images': len(results), 'outputs': len(results)*2, 'report': str(report)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
