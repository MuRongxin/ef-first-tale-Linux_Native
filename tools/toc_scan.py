#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""toc_scan.py - dump the table of contents of every PAZ archive to JSONL.

Reads/decrypts only the TOCs (a few hundred KB total), never the payloads, so
this is fast even for the 1.2 GB bg.paz. Used to plan extraction and to build
the asset inventory without decoding anything.

Usage:
    python3 tools/toc_scan.py --game-root "/path/to/game" \
        --out work/toc/entries.jsonl [--stats work/toc/stats.json]
"""

import argparse
import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import paz  # noqa: E402
import paz_fast  # noqa: E402

# archive tag -> (relative path, is_r18)
ARCHIVES = [
    ('bg', 'bg.paz', False),
    ('bgm', 'bgm.paz', False),
    ('mov', 'mov.paz', False),
    ('scr', 'scr.paz', False),
    ('se', 'se.paz', False),
    ('st', 'st.paz', False),
    ('sys', 'sys.paz', False),
    ('voice', 'voice.paz', False),
    ('r18_bg', 'r18_cn/bg.paz', True),
    ('r18_scr', 'r18_cn/scr.paz', True),
    ('r18_st', 'r18_cn/st.paz', True),
    ('r18_sys', 'r18_cn/sys.paz', True),
    ('r18_voice', 'r18_cn/voice.paz', True),
]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game-root', required=True)
    ap.add_argument('--out', required=True, help='JSONL output path')
    ap.add_argument('--stats', default=None, help='optional JSON summary path')
    args = ap.parse_args(argv)

    root = os.path.abspath(args.game_root)
    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    stats = {}
    total = 0
    with open(args.out, 'w', encoding='utf-8') as fh:
        for tag, rel, is_r18 in ARCHIVES:
            path = os.path.join(root, rel)
            if not os.path.isfile(path):
                print('skip (missing): %s' % rel, file=sys.stderr)
                continue
            arc = paz_fast.PazArchivePlus(path)
            try:
                entry = {
                    'archive': rel,
                    'tag': tag,
                    'is_r18': is_r18,
                    'game_id': arc.game_id,
                    'game': arc.game,
                    'prefix': arc.prefix,
                    'file_size': arc.total_size,
                    'toc_len': arc.toc_len,
                    'simple_key': arc.simple_key,
                    'data_start': arc.data_start,
                    'entry_count': len(arc.entries),
                }
                stats[tag] = entry
                for i, e in enumerate(arc.entries):
                    fh.write(json.dumps({
                        'tag': tag,
                        'archive': rel,
                        'index': i,
                        'name': e.name,
                        'offset': e.offset,
                        'raw_size': e.raw_size,
                        'stored_size': e.stored_size,
                        'aligned_size': e.aligned_size,
                        'packed': int(e.is_packed),
                    }, ensure_ascii=False) + '\n')
                total += len(arc.entries)
                print('%-8s %-16s entries=%6d raw=%12d packed=%6d' % (
                    tag, arc.game, len(arc.entries),
                    sum(e.raw_size for e in arc.entries),
                    sum(1 for e in arc.entries if e.is_packed)),
                    file=sys.stderr)
            finally:
                arc.close()

    if args.stats:
        with open(args.stats, 'w', encoding='utf-8') as fh:
            json.dump({'archive_count': len(stats), 'entry_total': total,
                       'archives': stats}, fh, ensure_ascii=False, indent=2)
    print('wrote %d entries from %d archives to %s' %
          (total, len(stats), args.out), file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
