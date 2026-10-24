#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""extract_assets.py - bulk-extract every *image* resource from the ef PAZ
archives into a clean, provenance-preserving tree, with a per-file manifest.

Scope (image pipeline only; audio/video/scripts are handled by other tools):
    bg.paz        -> .png + .ani
    st.paz        -> .png + .ani
    sys.paz       -> .png (+ the fake extensions .png- / .png.tmp) + .psd
    r18_cn/bg.paz -> .png + .ani + the two metadata .txt
    r18_cn/st.paz -> .png + .ani
    r18_cn/sys.paz-> .png

Output:
    <out-dir>/<tag>/<entry name with '\' -> '/'>          extracted payloads
    <manifest-dir>/entries_<tag>.jsonl                    one record per file
    <manifest-dir>/extraction-summary.json                merged summary
    <manifest-dir>/archive-digests.json                   sha256 of each .paz
    <manifest-dir>/failures.jsonl                         every failure (if any)

Every record carries: source archive, TOC index/name, sizes, sha256 of the
extracted payload, sniffed format and (for PNG/PSD) decoded dimensions. This is
what the inventory / upscale-plan stage consumes.

Speed: uses paz_fast (numpy byte-reversal + C RC4 + one Blowfish context per
archive), one process per archive. `--verify-reference` additionally compares a
random sample against the untouched reference reader (paz.py) and aborts on any
mismatch.

Usage:
    python3 tools/extract_assets.py \
        --game-root "/path/to/ef - the first tale" \
        --out-dir assets/raw --manifest-dir assets/manifest \
        --jobs 6 [--resume] [--verify-reference 20]
"""

import argparse
import hashlib
import io
import json
import multiprocessing as mp
import os
import random
import re
import struct
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import paz  # noqa: E402
import paz_fast  # noqa: E402

# tag -> (archive path relative to game root, kinds to extract)
ARCHIVE_SPEC = [
    ('bg', 'bg.paz', ('png', 'ani')),
    ('st', 'st.paz', ('png', 'ani')),
    ('sys', 'sys.paz', ('png', 'psd')),
    ('r18_bg', 'r18_cn/bg.paz', ('png', 'ani', 'txt')),
    ('r18_st', 'r18_cn/st.paz', ('png', 'ani')),
    ('r18_sys', 'r18_cn/sys.paz', ('png',)),
]
ALL_TAGS = [s[0] for s in ARCHIVE_SPEC]

PNG_SIG = b'\x89PNG\r\n\x1a\n'
PSD_SIG = b'8BPS'


# ---------------------------------------------------------------------------
# Format sniffing (cheap, no full decode - validation happens in the inventory)
# ---------------------------------------------------------------------------
def classify_name(name):
    """Return the extraction kind for a TOC name, or None.

    Extension parsing must tolerate the packer artifacts seen in sys.paz:
        pauseAnime.png-   iconson.png-   MemoriesBaseOver.png.tmp
    so we match "contains '.png'" rather than "endswith".
    """
    low = name.lower()
    if '.png' in low:
        return 'png'
    if low.endswith('.ani'):
        return 'ani'
    if low.endswith('.psd'):
        return 'psd'
    if low.endswith('.txt'):
        return 'txt'
    return None


def sniff(data, name, kind=None):
    """Return (format, info-dict) for one extracted payload."""
    info = {}
    if kind == 'txt':
        return 'txt', {'magic': data[:16].hex()}
    fmt = 'unknown'
    if data[:8] == PNG_SIG:
        fmt = 'png'
        if len(data) >= 33 and data[12:16] == b'IHDR':
            w, h = struct.unpack('>II', data[16:24])
            bitdepth, color_type = data[24], data[25]
            interlace = data[28]
            info.update(width=w, height=h, bit_depth=bitdepth,
                        color_type=color_type, interlace=interlace,
                        has_alpha=color_type in (4, 6))
    elif data[:4] == PSD_SIG:
        fmt = 'psd'
        if len(data) >= 26:
            channels, h, w, depth, mode = struct.unpack('>HIIHH', data[12:26])
            info.update(width=w, height=h, channels=channels, depth=depth,
                        mode=mode)
    elif len(data) >= 8 and data[:4] == b'\x00\x01\x02\x00':
        fmt = 'ani'
        info['ani_header'] = data[:16].hex()
    elif len(data) >= 8 and data[:4] == b'\x00\x01\x04\x00':
        fmt = 'ani'
        info['ani_header'] = data[:16].hex()
    elif kind == 'ani':
        fmt = 'ani'
        info['ani_header'] = data[:16].hex()
    elif data[:4] in (b'OggS', b'RIFF'):
        fmt = data[:4].decode('ascii')
        info['magic'] = data[:16].hex()
    else:
        low = name.lower()
        if low.endswith('.png'):
            fmt = 'png-broken'
        info['magic'] = data[:16].hex()
    return fmt, info


def safe_relpath(name):
    rel = name.replace('\\', '/').lstrip('/')
    norm = os.path.normpath(rel)
    if norm.startswith('..') or os.path.isabs(norm):
        return None
    return norm


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# Worker: one archive per process
# ---------------------------------------------------------------------------
def extract_archive(job):
    tag, rel, kinds, game_root, out_dir, manifest_dir, resume, verbose = job
    path = os.path.join(game_root, rel)
    t0 = time.time()
    out_root = os.path.join(out_dir, tag)
    os.makedirs(out_root, exist_ok=True)
    os.makedirs(manifest_dir, exist_ok=True)
    manifest_path = os.path.join(manifest_dir, 'entries_%s.jsonl' % tag)
    failures_path = os.path.join(manifest_dir, 'failures.jsonl')

    arc = paz_fast.PazArchivePlus(path)
    stats = {
        'tag': tag, 'archive': rel, 'game': arc.game, 'game_id': arc.game_id,
        'entry_count': len(arc.entries), 'selected': 0, 'extracted': 0,
        'resumed': 0, 'failed': 0, 'bytes': 0, 'by_format': {},
        'kinds': list(kinds),
    }
    selected = []
    for idx, e in enumerate(arc.entries):
        kind = classify_name(e.name)
        if kind in kinds:
            selected.append((idx, kind, e))
    stats['selected'] = len(selected)

    done = 0
    with open(manifest_path, 'w', encoding='utf-8') as mf, \
            open(failures_path, 'a', encoding='utf-8') as ff:
        for idx, kind, e in selected:
            relp = safe_relpath(e.name)
            if relp is None:
                ff.write(json.dumps({'tag': tag, 'name': e.name,
                                     'error': 'unsafe path'}) + '\n')
                stats['failed'] += 1
                continue
            dest = os.path.join(out_root, relp)
            os.makedirs(os.path.dirname(dest) or out_root, exist_ok=True)
            rec = {'tag': tag, 'archive': rel, 'index': idx,
                   'name': e.name, 'kind': kind,
                   'rel_path': '%s/%s' % (tag, relp),
                   'raw_size': e.raw_size, 'stored_size': e.stored_size,
                   'packed': int(e.is_packed)}
            try:
                if resume and os.path.isfile(dest) and \
                        os.path.getsize(dest) == e.raw_size:
                    digest = sha256_file(dest)
                    with open(dest, 'rb') as fh:
                        head = fh.read(64)
                    stats['resumed'] += 1
                    stats['bytes'] += os.path.getsize(dest)  # keep totals exact
                else:
                    data = arc.read(e)
                    tmp = dest + '.part'
                    with open(tmp, 'wb') as fh:
                        fh.write(data)
                    os.replace(tmp, dest)
                    digest = hashlib.sha256(data).hexdigest()
                    head = data[:64]
                    stats['bytes'] += len(data)
                    stats['extracted'] += 1
                fmt, info = sniff(head, e.name, kind)
                rec.update(fmt=fmt, sha256=digest, **info)
                mf.write(json.dumps(rec, ensure_ascii=False) + '\n')
                stats['by_format'][fmt] = stats['by_format'].get(fmt, 0) + 1
            except Exception as exc:  # keep going, record the failure
                ff.write(json.dumps({'tag': tag, 'name': e.name,
                                     'error': repr(exc)}) + '\n')
                stats['failed'] += 1
                if verbose:
                    print('  FAIL %s: %r' % (e.name, exc), file=sys.stderr)
            done += 1
            if done % 250 == 0:
                print('  [%s] %d/%d %.0f%% (%.1fs)' %
                      (tag, done, len(selected), 100.0 * done / len(selected),
                       time.time() - t0), file=sys.stderr, flush=True)
    arc.close()
    stats['seconds'] = round(time.time() - t0, 2)
    stats['adjusted_entries'] = len(arc.adjusted_entries)
    print('  [%s] DONE %d files, %.1f MB, %.1fs%s' %
          (tag, stats['extracted'] + stats['resumed'],
           stats['bytes'] / 1e6, stats['seconds'],
           (', %d failed' % stats['failed']) if stats['failed'] else ''),
          file=sys.stderr, flush=True)
    return stats


# ---------------------------------------------------------------------------
# Reference cross-check (paz.py, pure python, exact ground truth)
# ---------------------------------------------------------------------------
def reference_check(game_root, tags, samples_per_archive, seed=20241024):
    rng = random.Random(seed)
    results = []
    for tag, rel, kinds in ARCHIVE_SPEC:
        if tag not in tags:
            continue
        path = os.path.join(game_root, rel)
        fast = paz_fast.PazArchivePlus(path)
        ref = paz.PazArchive(path)
        pool = [e for e in ref.entries if classify_name(e.name) in kinds]
        rng.shuffle(pool)
        for e in pool[:samples_per_archive]:
            a = fast.read(e)
            b = ref.read(e)
            ok = hashlib.sha256(a).hexdigest() == hashlib.sha256(b).hexdigest()
            results.append({'tag': tag, 'name': e.name, 'bytes': len(a),
                            'identical': ok})
            print('  ref-check %-8s %-42s %8d %s' %
                  (tag, e.name, len(a), 'OK' if ok else 'MISMATCH'),
                  file=sys.stderr, flush=True)
        fast.close()
        ref.close()
    return results


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game-root', required=True)
    ap.add_argument('--out-dir', required=True)
    ap.add_argument('--manifest-dir', required=True)
    ap.add_argument('--tags', default=','.join(ALL_TAGS),
                    help='comma list of archive tags (default: all image ones)')
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--resume', action='store_true',
                    help='skip payloads that already exist with the right size')
    ap.add_argument('--verify-reference', type=int, default=0, metavar='N',
                    help='cross-check N random entries per archive against '
                         'the pure-python reference reader and abort on mismatch')
    ap.add_argument('--archive-digests', action='store_true',
                    help='sha256 every source .paz (default: yes)')
    args = ap.parse_args(argv)

    game_root = os.path.abspath(args.game_root)
    out_dir = os.path.abspath(args.out_dir)
    manifest_dir = os.path.abspath(args.manifest_dir)
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(manifest_dir, exist_ok=True)
    tags = [t.strip() for t in args.tags.split(',') if t.strip()]
    unknown = [t for t in tags if t not in ALL_TAGS]
    if unknown:
        ap.error('unknown tags: %s (known: %s)' % (unknown, ALL_TAGS))

    t_start = time.time()
    # 0) source identity
    digests = {}
    if args.archive_digests:
        for tag, rel, _ in ARCHIVE_SPEC:
            p = os.path.join(game_root, rel)
            if not os.path.isfile(p):
                continue
            t = time.time()
            digests[rel] = {'sha256': sha256_file(p),
                            'bytes': os.path.getsize(p)}
            print('  digest %-20s %s (%.1fs)' %
                  (rel, digests[rel]['sha256'][:16], time.time() - t),
                  file=sys.stderr)
        with open(os.path.join(manifest_dir, 'archive-digests.json'), 'w',
                  encoding='utf-8') as fh:
            json.dump(digests, fh, indent=2)

    # 1) optional ground-truth cross-check before the bulk run
    ref_results = []
    if args.verify_reference:
        ref_results = reference_check(game_root, tags, args.verify_reference)
        bad = [r for r in ref_results if not r['identical']]
        if bad:
            print('ABORT: reference mismatch on %r' % bad, file=sys.stderr)
            return 2

    # 2) bulk extraction, one process per archive
    jobs = []
    for tag, rel, kinds in ARCHIVE_SPEC:
        if tag not in tags:
            continue
        jobs.append((tag, rel, kinds, game_root, out_dir, manifest_dir,
                     args.resume, False))
    nproc = max(1, min(args.jobs, len(jobs)))
    print('extracting %d archives with %d workers ...' % (len(jobs), nproc),
          file=sys.stderr)
    with mp.Pool(nproc) as pool:
        stats = pool.map(extract_archive, jobs)

    # 3) merge summary
    summary = {
        'generated_at': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
        'game_root': game_root,
        'out_dir': out_dir,
        'tool': 'tools/extract_assets.py',
        'reference_reader': 'tools/paz.py (pure python)',
        'fast_reader': 'tools/paz_fast.py (numpy rev4 + ARC4)',
        'archives': {s['tag']: s for s in stats},
        'totals': {
            'archives': len(stats),
            'selected': sum(s['selected'] for s in stats),
            'extracted': sum(s['extracted'] for s in stats),
            'resumed': sum(s['resumed'] for s in stats),
            'failed': sum(s['failed'] for s in stats),
            'bytes': sum(s['bytes'] for s in stats),
        },
        'seconds': round(time.time() - t_start, 2),
        'reference_checks': ref_results,
        'archive_digests': digests,
    }
    with open(os.path.join(manifest_dir, 'extraction-summary.json'), 'w',
              encoding='utf-8') as fh:
        json.dump(summary, fh, ensure_ascii=False, indent=2)

    t = summary['totals']
    print('\nTOTAL: %d archives, %d selected, %d extracted (+%d resumed), '
          '%d failed, %.1f MB in %.1fs' %
          (t['archives'], t['selected'], t['extracted'], t['resumed'],
           t['failed'], t['bytes'] / 1e6, summary['seconds']))
    return 0 if t['failed'] == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
