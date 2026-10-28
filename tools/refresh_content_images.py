#!/usr/bin/env python3
"""原子更新现有 content 的高清图像，保留剧本/音视频/UI；缩略图只登记运行时派生。"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import fcntl
import json
from pathlib import Path
from PIL import Image
from package_content import store_json
from prepare_content import import_image, safe_path, validate_output, write_json, thumbnail_sources


def refresh(project: Path, out: Path, overrides: dict, jobs: int = 2) -> dict:
    project, out = project.resolve(), out.resolve()
    validate_output(project, out)
    Image.MAX_IMAGE_PIXELS = None # Dimensions are checked against the local inventory.
    with (project/'assets/manifest/images.csv').open(newline='') as file:
        rows = [r for r in csv.DictReader(file) if r['kind'] == 'png']
    derived = thumbnail_sources(rows, overrides)
    with safe_path(out, '.import.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        path = out/'manifest.json'
        before = path.read_bytes()
        manifest = json.loads(before)
        if set(manifest['images']) != {r['rel_path'] for r in rows}:
            raise ValueError('当前 content 与图片清单 ID 集合不同，需先完成完整导入')
        for row in rows:
            old = manifest['images'][row['rel_path']]
            if old['source_sha256'] != row['sha256'] or old['design_size'] != [int(row['w']), int(row['h'])]:
                raise ValueError(f'现有 content 来源不一致：{row["rel_path"]}')
        concrete = [r for r in rows if r['rel_path'] not in derived]
        images = {}
        def load(row):
            item = import_image(project, out, row, hd_only=True, decode=False)
            for scale in (4, 8):
                if str(scale) not in item['variants'] or item['variants'][str(scale)]['pixels'] != [int(row['w'])*scale, int(row['h'])*scale]:
                    raise ValueError(f'缺少完整 {scale}×：{row["rel_path"]}')
            return item
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            for n, (row, item) in enumerate(zip(concrete, pool.map(load, concrete)), 1):
                images[row['rel_path']] = item
                if n % 100 == 0 or n == len(concrete):
                    print(f'HD image import: {n}/{len(concrete)}', flush=True)
        for row in rows:
            id = row['rel_path']
            if id in derived:
                images[id] = {'design_size': [int(row['w']), int(row['h'])], 'source_sha256': row['sha256'],
                              'variants': {}, 'derived_from': derived[id]}
        report = json.loads(safe_path(out, manifest['import_report']).read_text()) if 'import_report' in manifest else {}
        report.update(raw_only_images=[], raw_only_categories={}, runtime_derived_images=derived,
                      image_validation='source+blob SHA-256 and full 4x/8x dimensions; runtime-derived thumbnails',
                      image_tiers={'concrete_images': len(concrete), '4x': len(concrete), '8x': len(concrete),
                                   'runtime_derived': len(derived), '1x': 0})
        manifest.update(images=images, development=False, import_report=store_json(out, report, '.import-report.json'))
        if path.read_bytes() != before:
            raise ValueError('导入期间 manifest 被其他写入者修改，拒绝覆盖')
        backup = project/'work/hd-replacement/manifest.before.json'
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            backup.write_bytes(before)
        write_json(path, manifest)
        write_json(project/'work/hd-replacement/refresh.json', report['image_tiers'])
        print(json.dumps(report['image_tiers']), flush=True)
        return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--content', type=Path, default=Path('content'))
    parser.add_argument('--thumbnail-sources', type=Path, default=Path('examples/thumbnail.sources.json'))
    parser.add_argument('--jobs', type=int, choices=range(1, 9), default=2)
    args = parser.parse_args()
    root = args.project_root.resolve()
    overrides = json.loads((root/args.thumbnail_sources).read_text())
    refresh(root, root/args.content, overrides, args.jobs)


if __name__ == '__main__':
    main()
