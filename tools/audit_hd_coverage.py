#!/usr/bin/env python3
"""按实际文件审计全部 PNG 的 4×/8× 覆盖，生成缺口表和统一处理计划。"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
import csv
import json
from pathlib import Path

from PIL import Image
from prepare_content import hd_name, safe_path, write_json

AI_METHODS = {'upscale_art', 'upscale_ui_or_redraw'}


def inspect_tier(project: Path, row: dict, scale: int) -> dict:
    tag, name = row['rel_path'].split('/', 1)
    webp = safe_path(project / 'assets/hd', f'{tag}_{scale}x/{hd_name(name)}')
    paths = [p for p in (webp, webp.with_suffix('.png')) if p.is_file()]
    if not paths:
        return {'status': 'missing'}
    if len(paths) != 1:
        return {'status': 'conflict', 'paths': [p.relative_to(project).as_posix() for p in paths]}
    path = paths[0]
    result = {'path': path.relative_to(project).as_posix()}
    try:
        with Image.open(path) as image:
            width, height = image.size
            if image.format not in ('PNG', 'WEBP') or getattr(image, 'is_animated', False):
                return dict(result, status='invalid_format')
        result['pixels'] = [width, height]
        w, h = int(row['w']), int(row['h'])
        if (width, height) == (w * scale, h * scale):
            return dict(result, status='complete')
        if scale == 8 and max(w * 8, h * 8) > 16383 and (width, height) == (w * 4, h * 4):
            return dict(result, status='documented_4x_fallback')
        return dict(result, status='invalid_dimensions', expected=[w * scale, h * scale])
    except (OSError, ValueError) as error:
        return dict(result, status='unreadable', error=str(error))


def audit(project: Path, rows: list[dict], plan: dict, jobs: int = 4) -> tuple[dict, dict]:
    # Header-only checks avoid decoding tens of GB. Decode/hash checks remain separate.
    Image.MAX_IMAGE_PIXELS = None
    rows = [r for r in rows if r['kind'] == 'png']
    def inspect(row):
        tiers = {str(s): inspect_tier(project, row, s) for s in (4, 8)}
        missing = [s for s in (4, 8) if tiers[str(s)]['status'] not in ('complete', 'documented_4x_fallback')]
        return {'rel_path': row['rel_path'], 'sha256': row['sha256'], 'category': row['category'],
                'method': row['method'], 'design_size': [int(row['w']), int(row['h'])],
                'tiers': tiers, 'missing_tiers': missing}
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        records = list(pool.map(inspect, rows))
    by_category = defaultdict(Counter)
    for r in records:
        counter = by_category[r['category']]
        counter['total'] += 1
        counter['neither_tier'] += all(t['status'] == 'missing' for t in r['tiers'].values())
        counter['both_full_resolution'] += all(t['status'] == 'complete' for t in r['tiers'].values())
        for scale, tier in r['tiers'].items():
            counter[f'{scale}x_{tier["status"]}'] += 1
    missing_ids = {r['rel_path'] for r in records if r['missing_tiers']}
    tasks = []
    covered = set()
    for entry in plan['entries']:
        ids = [entry['rel_path']] + [a['rel_path'] for a in entry.get('aliases', [])]
        if entry.get('method') in AI_METHODS and missing_ids.intersection(ids):
            tasks.append(entry)
            covered.update(ids)
    ai_gaps = {r['rel_path'] for r in records if r['method'] in AI_METHODS and r['missing_tiers']}
    if ai_gaps - covered:
        raise ValueError(f'AI 缺口不在既有任务计划中：{sorted(ai_gaps - covered)}')
    report = {
        'version': 1, 'validation': 'file_presence+format+header_dimensions; not full decode or visual acceptance',
        'total_images': len(records), 'categories': {k: dict(v) for k, v in sorted(by_category.items())},
        'missing_by_method': dict(Counter(r['method'] for r in records if r['missing_tiers'])),
        'ai_unique_tasks': len(tasks),
        'procedural': [r['rel_path'] for r in records if r['method'] == 'procedural_regen' and r['missing_tiers']],
        'runtime_derived': [r['rel_path'] for r in records if r['method'] == 'runtime_derive_from_hd'],
        'images': records,
    }
    return report, {'entries': tasks, 'source_plan': 'assets/manifest/upscale-plan.json',
                    'note': '统一按原远端流水线制作两档；不含程序化图和运行时派生缩略图。'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, default=Path.cwd())
    parser.add_argument('--out', type=Path, default=Path('work/hd-coverage'))
    parser.add_argument('--jobs', type=int, choices=range(1, 9), default=4)
    args = parser.parse_args()
    project = args.project_root.resolve()
    out = (project / args.out).resolve()
    if not out.is_relative_to(project / 'work'):
        parser.error('审计输出必须位于工程 work/ 中')
    with (project / 'assets/manifest/images.csv').open(encoding='utf-8', newline='') as file:
        rows = list(csv.DictReader(file))
    plan = json.loads((project / 'assets/manifest/upscale-plan.json').read_text())
    report, batch = audit(project, rows, plan, args.jobs)
    out.mkdir(parents=True, exist_ok=True)
    write_json(out / 'coverage.json', report)
    write_json(out / 'upscale-missing.json', batch)
    with (out / 'missing.csv').open('w', encoding='utf-8', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['rel_path', 'category', 'method', '4x', '8x'])
        for r in report['images']:
            if r['missing_tiers']:
                writer.writerow([r['rel_path'], r['category'], r['method'], r['tiers']['4']['status'], r['tiers']['8']['status']])
    lines = ['# 全量高清覆盖审计', '', f'PNG 总数：{report["total_images"]}。检查文件、格式与头部尺寸；不代表完整解码 / 画质验收。', '',
             '| 类别 | 总数 | 两档均全尺寸 | 4× 缺少 | 8× 缺少 | 8× 已记录的 4× 回退 |',
             '|---|---:|---:|---:|---:|---:|']
    for name, c in report['categories'].items():
        lines.append(f'| {name} | {c["total"]} | {c["both_full_resolution"]} | {c.get("4x_missing", 0)} | {c.get("8x_missing", 0)} | {c.get("8x_documented_4x_fallback", 0)} |')
    lines += ['', f'AI 去重批次：{len(batch["entries"])} 项；程序化缺口：{len(report["procedural"])} 张；运行时派生：{len(report["runtime_derived"])} 张（禁止单独 AI 超分 / 预烘）。',
              '', '异常尺寸、文件冲突和不可读文件详见 coverage.json；这些状态也计入待处理缺口。', '']
    (out / 'REPORT.md').write_text('\n'.join(lines), encoding='utf-8')
    print(json.dumps({k: report[k] for k in ['total_images', 'categories', 'missing_by_method', 'ai_unique_tasks']}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
