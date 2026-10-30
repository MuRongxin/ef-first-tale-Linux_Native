#!/usr/bin/env python3
"""审计 ANI 与配对 PNG 的关系、角色状态及透明度；不发布运行时包。"""
from __future__ import annotations
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
from ani import decode


def inspect(project: Path) -> dict:
    with (project / 'assets/manifest/images.csv').open(newline='', encoding='utf-8') as file:
        rows = list(csv.DictReader(file))
    images = {r['rel_path']: r for r in rows if r['kind'] == 'png'}
    records, failures = [], []
    for row in rows:
        if row['kind'] != 'ani':
            continue
        id = row['rel_path']
        data = (project / 'assets/raw' / id).read_bytes()
        if hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError(f'ANI differs from inventory: {id}')
        patches = decode(data)
        stem = id[:-4]
        pairs = [name for name in (stem+'.png', stem+'.png-', stem+'.png.tmp') if name in images]
        if len(pairs) != 1:
            failures.append(dict(id=id, error='missing/ambiguous pair', candidates=pairs))
            continue
        pair = images[pairs[0]]
        groups = {}
        for i, patch in enumerate(patches):
            actor, separator, state = patch.name.partition(' ')
            if not separator or not actor or not state:
                failures.append(dict(id=id, error='invalid character/state', record=i))
                continue
            if patch.x + patch.width > int(pair['w']) or patch.y + patch.height > int(pair['h']):
                failures.append(dict(id=id, error='patch outside canvas', record=i))
            groups.setdefault(actor, []).append(dict(record=i, state=state, bitmap=bool(patch.width)))
        records.append(dict(id=id, image=pairs[0], source_sha256=row['sha256'], groups=groups,
                            records=len(patches), bitmap_records=sum(bool(p.width) for p in patches)))
    return dict(format='ef.ani.audit', version=1, files=len(records), records=sum(r['records'] for r in records),
                bitmap_records=sum(r['bitmap_records'] for r in records),
                actors=dict(Counter(a for r in records for a in r['groups'])), failures=failures, entries=records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.project_root.resolve())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k != 'entries'}, ensure_ascii=False))
    raise SystemExit(bool(report['failures']))


if __name__ == '__main__':
    main()
