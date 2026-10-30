#!/usr/bin/env python3
"""将已验证 ANI 转为原倍率 PNG 图块和只读动画目录，原子更新现有 content。"""
from __future__ import annotations
import argparse
import csv
import fcntl
import hashlib
import io
import json
from pathlib import Path
from PIL import Image
from ani import decode
from package_content import store_bytes, store_json
from prepare_content import safe_path, validate_output, write_json


def import_animations(project: Path, out: Path, manifest: dict) -> dict:
    with (project / 'assets/manifest/images.csv').open(newline='', encoding='utf-8') as file:
        rows = list(csv.DictReader(file))
    images = {r['rel_path']: r for r in rows if r['kind'] == 'png'}
    catalog = {}
    for row in rows:
        if row['kind'] != 'ani':
            continue
        id = row['rel_path']
        source = safe_path(project / 'assets/raw', id)
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError(f'ANI source differs from inventory: {id}')
        candidates = [id[:-4] + suffix for suffix in ('.png', '.png-', '.png.tmp') if id[:-4] + suffix in images]
        if len(candidates) != 1:
            raise ValueError(f'ANI requires unique same-directory PNG: {id}')
        image = candidates[0]
        spec = manifest['images'][image]
        if spec['source_sha256'] != images[image]['sha256']:
            raise ValueError(f'ANI base source differs from content: {image}')
        width, height = spec['design_size']
        records = []
        for patch in decode(data):
            actor, sep, state = patch.name.partition(' ')
            if not sep or not actor or not state or patch.x + patch.width > width or patch.y + patch.height > height:
                raise ValueError(f'Invalid ANI patch name/bounds: {id}')
            record = dict(actor=actor, state=state, size=[patch.width, patch.height], offset=[patch.x, patch.y])
            if patch.width:
                buffer = io.BytesIO()
                Image.frombytes('RGBA', (patch.width, patch.height), patch.bgra, 'raw', 'BGRA').save(buffer, format='PNG')
                record['path'] = store_bytes(out, buffer.getvalue(), '.png', 'animations')
            records.append(record)
        catalog[image] = dict(source_sha256=row['sha256'], base_sha256=spec['source_sha256'], records=records)
    return dict(format='ef.animations', version=1, source_scale=1, images=catalog)


def refresh(project: Path, out: Path):
    project, out = project.resolve(), out.resolve()
    validate_output(project, out)
    with safe_path(out, '.import.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        path = out / 'manifest.json'
        before = path.read_bytes()
        manifest = json.loads(before)
        catalog = import_animations(project, out, manifest)
        manifest['animations'] = store_json(out, catalog, '.animations.json')
        if path.read_bytes() != before:
            raise ValueError('Content manifest changed during animation import')
        backup = project / 'work/ani-import/manifest.before.json'
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            backup.write_bytes(before)
        write_json(path, manifest)
        print(json.dumps(dict(images=len(catalog['images']), records=sum(len(s['records']) for s in catalog['images'].values())), ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--content', type=Path, default=Path('content'))
    args = parser.parse_args()
    refresh(args.project_root, args.content)
