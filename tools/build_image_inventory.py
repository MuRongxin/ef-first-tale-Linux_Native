#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""build_image_inventory.py - audit the extracted image resources and build the
HD (upscale) plan.

Inputs (all produced by the extraction stage):
    assets/raw/<tag>/...                      extracted payloads
    assets/manifest/entries_<tag>.jsonl       per-file extraction records
    work/toc/entries.jsonl                     complete TOC of every archive
    docs/asset-audit.json                     script -> asset reference counts

What it does:
  1. Re-hashes every extracted file and compares it with the manifest.
  2. Fully decodes/validates every PNG (Pillow verify+load) and cross-checks
     the IHDR geometry; parses PNG chunks for tRNS/PLTE/iCCP facts.
  3. Groups duplicates by content, resolves r18/base overrides and the two
     cross-archive name collisions, and computes the "script reference count"
     for every logical asset.
  4. Classifies every asset (background / event CG / tachie / overlay / UI /
     thumbnail / mask / metadata / animation ...) from namespace + dimensions.
  5. Assigns an HD policy per unique *content*: upscale (art / UI), derive from
     an HD source (thumbnails), regenerate procedurally (solid masks), keep as
     source (PSD), or scale with its paired PNG (.ani).
  6. Writes: assets/manifest/images.json, images.csv, upscale-plan.json,
     duplicates.json, overrides.json, SUMMARY.md, and prints a summary.

Usage:
    python3 tools/build_image_inventory.py \
        --project-root . [--jobs 8] [--master-scale 4] [--skip-validate]
"""

import argparse
import collections
import csv
import hashlib
import io
import json
import multiprocessing as mp
import os
import re
import struct
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))

PNG_SIG = b'\x89PNG\r\n\x1a\n'
COLOR_TYPE = {0: 'gray', 2: 'rgb', 3: 'palette', 4: 'gray+alpha', 6: 'rgba'}
MODE_ALPHA = {'RGBA', 'LA', 'PA', 'RGBa', 'La'}

# tag -> (archive, is_r18)
ARCHIVE_META = {
    'bg': ('bg.paz', False),
    'st': ('st.paz', False),
    'sys': ('sys.paz', False),
    'r18_bg': ('r18_cn/bg.paz', True),
    'r18_st': ('r18_cn/st.paz', True),
    'r18_sys': ('r18_cn/sys.paz', True),
}
IMAGE_TAGS = tuple(ARCHIVE_META)

# ---------------------------------------------------------------------------
# Names
# ---------------------------------------------------------------------------
def logical_key(name):
    """Case-insensitive engine name (the engine was Windows: no case)."""
    return name.replace('\\', '/').lower()


def basename(key):
    return key.rsplit('/', 1)[-1]


def strip_kind_ext(base):
    """Return (stem, suffix) tolerating the packer's fake extensions."""
    for suf in ('.png.tmp', '.png-', '.png', '.ani', '.psd', '.txt'):
        if base.endswith(suf):
            return base[:-len(suf)], suf
    if '.png' in base:  # packer artifact with junk after .png
        i = base.find('.png')
        return base[:i], base[i:]
    return base, ''


def name_family(base):
    stem, _ = strip_kind_ext(base)
    m = re.match(r'^([A-Za-z]+)', stem)
    return m.group(1).lower() if m else '', stem


# ---------------------------------------------------------------------------
# Per-file validation worker
# ---------------------------------------------------------------------------
def png_chunk_facts(data):
    facts = {'trns': False, 'plte_entries': None, 'idat_count': 0,
             'iccp': False, 'srgb': False, 'gama': False}
    off = 8
    while off + 8 <= len(data):
        ln = struct.unpack('>I', data[off:off + 4])[0]
        typ = data[off + 4:off + 8]
        if typ == b'IDAT':
            facts['idat_count'] += 1
        elif typ == b'tRNS':
            facts['trns'] = True
        elif typ == b'PLTE':
            facts['plte_entries'] = ln // 3
        elif typ == b'iCCP':
            facts['iccp'] = True
        elif typ == b'sRGB':
            facts['srgb'] = True
        elif typ == b'gAMA':
            facts['gama'] = True
        if typ == b'IEND':
            break
        off += 12 + ln
    return facts


def validate_record(job):
    """Worker: validate one extracted file; return the enriched record."""
    rec, raw_dir, skip_validate = job
    out = dict(rec)
    path = os.path.join(raw_dir, rec['rel_path'])
    out['path'] = path
    out['exists'] = os.path.isfile(path)
    out['hash_ok'] = None
    out['decode_ok'] = None
    out['size_ok'] = False
    if not out['exists']:
        return out
    size = os.path.getsize(path)
    out['bytes_on_disk'] = size
    out['size_ok'] = (size == rec['raw_size'])
    if skip_validate:
        return out
    with open(path, 'rb') as fh:
        data = fh.read()
    out['sha256_on_disk'] = hashlib.sha256(data).hexdigest()
    out['hash_ok'] = (out['sha256_on_disk'] == rec['sha256'])

    kind = rec.get('kind')
    fmt = kind if kind in ('png', 'psd', 'ani', 'txt') else rec.get('fmt')
    if fmt == 'png' and data[:8] == PNG_SIG:
        out.update(png_chunk_facts(data))
        try:
            from PIL import Image
            im = Image.open(io.BytesIO(data))
            im.verify()                      # chunk CRC check
            im = Image.open(io.BytesIO(data))
            im.load()                        # full pixel decode
            out['decode_ok'] = True
            out['pillow_mode'] = im.mode
            out['pillow_size'] = list(im.size)
            rgb = im.convert('RGB')
            out['solid_color'] = all(lo == hi for lo, hi in rgb.getextrema())
            if im.mode in ('P', 'PA'):
                out['palette_entries'] = len(im.getpalette() or []) // 3
        except Exception as exc:
            out['decode_ok'] = False
            out['decode_error'] = repr(exc)
    elif fmt == 'psd':
        try:
            from PIL import Image
            im = Image.open(io.BytesIO(data))
            im.load()
            out['decode_ok'] = True
            out['pillow_mode'] = im.mode
            out['pillow_size'] = list(im.size)
        except Exception as exc:
            out['decode_ok'] = False
            out['decode_error'] = repr(exc)
    elif fmt == 'ani':
        out['ani_header'] = data[:16].hex()
        out['ani_magic'] = data[:8].hex()
        out['ani_variant'] = ('04' if data[2:4] == b'\x04\x00' else
                              '02' if data[2:4] == b'\x02\x00' else '?')
        out['decode_ok'] = None           # custom format, decoded elsewhere
    elif fmt == 'txt':
        for enc in ('utf-8', 'cp932', 'gbk'):
            try:
                text = data.decode(enc)
                out['txt_encoding'] = enc
                out['txt_lines'] = text.count('\n')
                break
            except UnicodeDecodeError:
                continue
    return out


# ---------------------------------------------------------------------------
# Classification
# ---------------------------------------------------------------------------
# Ordered rules over the lowercased stem (basename without extension). Order
# matters: masks and UI names must be matched before the generic art prefixes.
TITLE_EXACT = {'second_title', 'produce', 'producer', 'planner', 'director',
               'direc', 'os_sp', 'sound', 'music', 'movie', 'cd'}
UI_PREFIXES = ('topmenu', 'memories', 'saveload', 'gamemenu', 'config',
               'msgpanel', 'namepanel', 'fullpanel', 'select', 'pause',
               'music', 'backlog', 'knob', 'icons', 'icon', 'checkmark',
               'notsaved', 'nm', 'album', 'fontselect', 'certerror',
               'bat', 'gaiji', 'wazatogazou', 'finder', 'quicksave',
               'zzz')  # 'zzz' placeholder keeps the tuple non-empty
BG_PREFIXES = ('bg_', 'bgs_', 'bgw_', 'bgc_', 'bg_scl', 'vista', 'kei_vista',
               'kei_', 'ago_', 'prologue', 'past_')
MASK_PREFIXES = ('fade', 'snow', 'trans', 'black', 'white', 'color')


def classify(rec):
    """Return (category, role, family, notes, variant) for one record."""
    key = rec['logical_key']
    base = basename(key)
    is_r18 = rec.get('is_r18', False)
    kind = rec.get('kind')

    if kind == 'txt':
        return 'meta', 'metadata', 'txt', [], None
    if kind == 'psd':
        fam, _ = name_family(base)
        return 'source', 'source-file', fam, ['PSD 制作源文件'], None
    if kind == 'ani':
        return 'ani', 'animation', 'ani', [], None
    if key.startswith('thumb/') or '/thumb/' in key:
        return 'thumb', 'thumbnail', 'thumb', [], None

    w, h = rec.get('w'), rec.get('h')
    # 1px-wide vertical colour strips: the engine stretches these horizontally
    # as full-screen tint/colour-grade layers (not character art).
    if w and h and w <= 4 and h >= 200:
        return ('tint', 'effect', 'strip',
                ['1px 宽竖向色带（横向拉伸的着色层）'], None)

    fam, stem = name_family(base)
    if stem in ('circle', 'ball'):
        return 'ui', 'ui', stem, ['UI 滑块部件'], None
    notes = []
    cat = role = None

    if stem.startswith(('evh', 'ev', 'ed')):
        cat, role = 'cg', 'art'
        if stem.startswith('evh'):
            notes.append('r18-cg-content')
    elif stem.startswith(('st_', 'stm_')):
        cat, role = 'tachie', 'art'
    elif stem.startswith('or_'):
        cat, role = 'overlay', 'art'
    elif stem.startswith('sc_'):
        cat, role = 'screen', 'art'
    elif stem.startswith(MASK_PREFIXES):
        cat, role = 'mask', 'effect'
    elif stem.startswith('terop') or stem in TITLE_EXACT:
        cat, role = 'title', 'art'
    elif stem.startswith(BG_PREFIXES):
        cat, role = 'bg', 'art'
    elif stem.startswith(UI_PREFIXES):
        cat, role = 'ui', 'ui'
    elif stem.startswith(('bg', 'ev', 'st', 'ed', 'or', 'sc')):
        # unknown member of a known art family -> keep it as art but flag it
        cat, role = 'unclassified', 'unknown'
        notes.append('known-prefix-unknown-member: review')
    else:
        cat, role = 'unclassified', 'unknown'
        notes.append('unclassified: review')

    if is_r18 and cat not in ('unclassified',):
        notes.append('r18set')

    # selection-variant suffixes (alternate art for the same scene, not new art)
    variant = None
    m = re.search(r'_(mugai(?:_[a-z])?|sepia|pastm|stm\d*|stm75|big|close|'
                  r'bokashi|org)$', stem)
    if m:
        variant = m.group(1)
    elif stem.endswith('-'):
        variant = 'dash'
    return cat, role, fam, notes, variant


def finalize_dimensions(rec):
    w = rec.get('width') or (rec.get('pillow_size') or [None])[0]
    h = rec.get('height') or (rec.get('pillow_size') or [None, None])[1]
    rec['w'], rec['h'] = w, h
    return rec


# ---------------------------------------------------------------------------
# HD policy
# ---------------------------------------------------------------------------
def hd_policy(cat, role, w, h, script_refs, solid, kind):
    """Return dict(method, reason, priority, out_rel_hint)."""
    if kind == 'ani':
        return {'method': 'scale_with_pair',
                'reason': 'ANI 动画数据与同名 PNG 配对，坐标/裁剪需随 PNG 倍数缩放（格式待逆向）',
                'priority': 2}
    if kind == 'txt':
        return {'method': 'no_hd', 'reason': '名称元数据文本，无需图像处理',
                'priority': 9}
    if kind == 'psd':
        return {'method': 'keep_as_source',
                'reason': 'PSD 是制作源文件，保留作为 UI 高清化参考/素材，不参与超分批处理',
                'priority': 9}
    if cat == 'thumb':
        return {'method': 'derive_from_hd',
                'reason': '缩略图应由对应高清 CG 降采样生成，直接超分会放大 JPEG 级噪点',
                'priority': 3}
    if cat == 'mask':
        return {'method': 'procedural_regen',
                'reason': '纯色/渐变过渡遮罩应按目标分辨率程序化重建（或最近邻放大），不适合 AI 超分',
                'priority': 4}
    if solid:
        return {'method': 'procedural_regen',
                'reason': '整图单一颜色（实测），程序化生成即可',
                'priority': 4}
    # art / ui
    if cat == 'tint':
        return {'method': 'procedural_regen',
                'reason': '1px 宽竖向渐变着色带：按目标高度重采样即可'
                          '（横向由引擎拉伸），无需 AI 超分', 'priority': 4}
    if cat in ('bg', 'cg', 'tachie', 'overlay', 'screen', 'title'):
        prio = 1 if script_refs > 0 else 2
        return {'method': 'upscale_art',
                'reason': '全屏美术资源：动画风格超分模型 4× 母版，运行时按窗口倍率降采样',
                'priority': prio}
    if cat == 'ui':
        return {'method': 'upscale_ui_or_redraw',
                'reason': 'UI 元素：可 4× 超分；细小文字/图标建议同时记录为"可矢量重绘"候选',
                'priority': 2}
    return {'method': 'upscale_art', 'reason': '未分类，默认按美术资源处理，需复核',
            'priority': 3}


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--project-root', default=os.getcwd())
    ap.add_argument('--raw-dir', default=None)
    ap.add_argument('--manifest-dir', default=None)
    ap.add_argument('--toc', default=None)
    ap.add_argument('--asset-audit', default=None)
    ap.add_argument('--out-dir', default=None)
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--master-scale', type=int, default=4)
    ap.add_argument('--skip-validate', action='store_true')
    args = ap.parse_args(argv)

    root = os.path.abspath(args.project_root)
    raw_dir = args.raw_dir or os.path.join(root, 'assets', 'raw')
    manifest_dir = args.manifest_dir or os.path.join(root, 'assets', 'manifest')
    out_dir = args.out_dir or manifest_dir
    toc_path = args.toc or os.path.join(root, 'work', 'toc', 'entries.jsonl')
    audit_path = args.asset_audit or os.path.join(root, 'docs', 'asset-audit.json')
    os.makedirs(out_dir, exist_ok=True)

    t0 = time.time()
    # -- 1. load extraction manifests -------------------------------------
    records = []
    for tag in IMAGE_TAGS:
        p = os.path.join(manifest_dir, 'entries_%s.jsonl' % tag)
        if not os.path.isfile(p):
            print('warning: missing manifest %s' % p, file=sys.stderr)
            continue
        with open(p, encoding='utf-8') as fh:
            for line in fh:
                line = line.strip()
                if line:
                    records.append(json.loads(line))
    print('loaded %d extraction records' % len(records), file=sys.stderr)

    # -- 2. per-file validation -------------------------------------------
    jobs = [(r, raw_dir, args.skip_validate) for r in records]
    if args.jobs > 1:
        with mp.Pool(args.jobs) as pool:
            records = pool.map(validate_record, jobs, chunksize=32)
    else:
        records = [validate_record(j) for j in jobs]
    print('validated %d files in %.1fs' % (len(records), time.time() - t0),
          file=sys.stderr)

    # -- 3. script reference counts ---------------------------------------
    refs = {}
    if os.path.isfile(audit_path):
        audit = json.load(open(audit_path, encoding='utf-8'))
        for name, info in audit.get('found', {}).items():
            refs[name.replace('\\', '/').lower()] = info.get('count', 0)
    print('script reference table: %d names' % len(refs), file=sys.stderr)

    # -- 4. enrich + classify ---------------------------------------------
    for rec in records:
        rec['logical_key'] = logical_key(rec['name'])
        rec['is_r18'] = ARCHIVE_META[rec['tag']][1]
        finalize_dimensions(rec)
        cat, role, fam, notes, variant = classify(rec)
        rec['category'] = cat
        rec['role'] = role
        rec['family'] = fam
        rec['notes'] = notes
        rec['variant'] = variant
        rec['script_refs'] = refs.get(rec['logical_key'], 0)
        rec['has_alpha'] = bool(rec.get('has_alpha')) or \
            bool(rec.get('trns')) or rec.get('pillow_mode') in MODE_ALPHA
        pol = hd_policy(cat, role, rec.get('w'), rec.get('h'),
                        rec['script_refs'], rec.get('solid_color'), rec['kind'])
        if rec['kind'] != 'ani':
            if rec.get('w') and rec.get('h'):
                scale = args.master_scale
                stem = name_family(basename(rec['logical_key']))[1]
                # A "big" CG variant already ships at 2x the 4:3 frame
                # (1600x1200 / 1200x1600): half the factor is enough to reach
                # the same logical master size.
                if stem.endswith('_big') and rec['w'] >= 1200 and rec['h'] >= 1200:
                    scale = max(1, args.master_scale // 2)
                    pol['hires_source'] = True
                pol['scale'] = scale
                pol['target_w'] = rec['w'] * scale
                pol['target_h'] = rec['h'] * scale
                pol['out_rel_path'] = 'assets/hd/%s' % rec['rel_path']
        rec['hd'] = pol

    # -- 5. logical groups / duplicates / overrides ------------------------
    by_key = collections.defaultdict(list)
    for rec in records:
        by_key[rec['logical_key']].append(rec)

    # png <-> ani pairing
    ani_pairs = {}
    for key, group in list(by_key.items()):
        if key.endswith('.ani'):
            png_key = key[:-4] + '.png'
            ani_pairs[key] = png_key if png_key in by_key else None
    for key, group in by_key.items():
        for rec in group:
            if rec['kind'] == 'png':
                rec['has_ani'] = (key[:-4] + '.ani') in by_key if key.endswith('.png') else False
            elif rec['kind'] == 'ani':
                rec['pair_png'] = ani_pairs.get(key)

    # content duplicate groups
    content = collections.defaultdict(list)
    for rec in records:
        if rec.get('sha256'):
            content[rec['sha256']].append(rec)
    dup_groups = {sha: recs for sha, recs in content.items() if len(recs) > 1}

    # overrides: same logical key from different tags
    overrides = {}
    for key, group in by_key.items():
        tags = sorted({r['tag'] for r in group})
        if len(tags) > 1:
            shas = {r['tag']: r.get('sha256') for r in group}
            same = len({s for s in shas.values() if s}) == 1
            overrides[key] = {'tags': tags, 'same_content': same,
                              'entries': [{'tag': r['tag'], 'rel_path': r['rel_path'],
                                           'sha256': r.get('sha256'),
                                           'bytes': r.get('raw_size'),
                                           'w': r.get('w'), 'h': r.get('h')}
                                          for r in group]}

    # -- 6. HD plan per unique content -------------------------------------
    plan_entries = []
    seen = set()
    for rec in sorted(records, key=lambda r: (r['tag'], r.get('index', 0))):
        kind = rec['kind']
        method = rec['hd']['method']
        if method in ('no_hd', 'keep_as_source'):
            continue
        if kind == 'txt':
            continue
        if method == 'scale_with_pair':
            plan_entries.append({
                'rel_path': rec['rel_path'], 'logical_key': rec['logical_key'],
                'category': 'ani', 'method': method, 'scale': args.master_scale,
                'pair_png': rec.get('pair_png'),
                'priority': rec['hd']['priority'],
                'reason': rec['hd']['reason'],
            })
            continue
        sha = rec.get('sha256')
        if sha in seen:  # same bytes elsewhere: alias, not new work
            continue
        seen.add(sha)
        plan_entries.append({
            'rel_path': rec['rel_path'],
            'logical_key': rec['logical_key'],
            'name': rec['name'],
            'tag': rec['tag'],
            'is_r18': rec['is_r18'],
            'category': rec['category'],
            'family': rec['family'],
            'w': rec.get('w'), 'h': rec.get('h'),
            'bytes': rec.get('raw_size'),
            'sha256': sha,
            'has_alpha': rec['has_alpha'],
            'script_refs': rec['script_refs'],
            'method': method,
            'scale': rec['hd'].get('scale'),
            'target_w': rec['hd'].get('target_w'),
            'target_h': rec['hd'].get('target_h'),
            'out_rel_path': rec['hd'].get('out_rel_path'),
            'hires_source': bool(rec['hd'].get('hires_source')),
            'priority': rec['hd']['priority'],
            'reason': rec['hd']['reason'],
            'aliases': [],
        })
    # attach aliases (same content, other paths/names)
    plan_by_sha = {e['sha256']: e for e in plan_entries if 'sha256' in e}
    for sha, recs in content.items():
        e = plan_by_sha.get(sha)
        if not e:
            continue
        for r in recs:
            if r['rel_path'] != e['rel_path']:
                e['aliases'].append({
                    'rel_path': r['rel_path'], 'name': r['name'],
                    'tag': r['tag'], 'is_r18': r['is_r18'],
                    'category': r['category'],
                    'override_of': None})

    # -- 7. statistics ------------------------------------------------------
    cat_stats = collections.defaultdict(lambda: collections.Counter())
    px_sum = collections.Counter()
    for rec in records:
        c = rec['category']
        cat_stats[c]['files'] += 1
        cat_stats[c]['bytes'] += rec.get('raw_size', 0)
        if rec.get('w') and rec.get('h'):
            cat_stats[c]['pixels'] += rec['w'] * rec['h']
            px_sum[c] += rec['w'] * rec['h']

    method_stats = collections.defaultdict(lambda: collections.Counter())
    for e in plan_entries:
        m = e['method']
        method_stats[m]['files'] += 1
        method_stats[m]['bytes'] += e.get('bytes') or 0
        if e.get('w') and e.get('h'):
            method_stats[m]['pixels'] += e['w'] * e['h']
            method_stats[m]['target_pixels'] += ((e.get('target_w') or 0) *
                                                 (e.get('target_h') or 0))

    # -- 8. validation problems --------------------------------------------
    problems = {
        'missing': [r['rel_path'] for r in records if not r.get('exists')],
        'size_mismatch': [r['rel_path'] for r in records
                          if r.get('exists') and not r.get('size_ok')],
        'hash_mismatch': [r['rel_path'] for r in records
                          if r.get('hash_ok') is False],
        'png_decode_failed': [r['rel_path'] for r in records
                              if r.get('decode_ok') is False],
        'ihdr_size_mismatch': [
            r['rel_path'] for r in records
            if r.get('kind') == 'png' and r.get('pillow_size') and
            list(r['pillow_size']) != [r.get('width'), r.get('height')]],
        'unclassified': [r['rel_path'] for r in records
                         if r['category'] == 'unclassified'],
        'name_table_names': [r['rel_path'] for r in records
                             if r['kind'] == 'txt'],
    }

    # -- 9. write outputs ----------------------------------------------------
    records_out = []
    keep_fields = ('tag', 'archive', 'index', 'name', 'logical_key', 'rel_path',
                   'kind', 'fmt', 'raw_size', 'stored_size', 'packed',
                   'width', 'height', 'w', 'h', 'bit_depth', 'color_type',
                   'interlace', 'pillow_mode', 'has_alpha', 'trns',
                   'plte_entries', 'idat_count', 'solid_color', 'sha256',
                   'ani_header', 'ani_magic', 'ani_variant', 'txt_encoding',
                   'txt_lines',
                   'category', 'role', 'family', 'variant', 'notes',
                   'script_refs', 'has_ani', 'pair_png', 'is_r18', 'hd')
    for r in records:
        o = {k: r.get(k) for k in keep_fields if k in r}
        o['variant'] = r.get('variant')
        records_out.append(o)
    with open(os.path.join(out_dir, 'images.json'), 'w', encoding='utf-8') as fh:
        json.dump({'generated_at': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
                   'record_count': len(records_out),
                   'records': records_out}, fh, ensure_ascii=False)

    with open(os.path.join(out_dir, 'images.csv'), 'w', newline='',
              encoding='utf-8') as fh:
        cols = ['tag', 'rel_path', 'name', 'kind', 'fmt', 'w', 'h',
                'has_alpha', 'bytes', 'sha256', 'category', 'family',
                'variant', 'script_refs', 'has_ani', 'is_r18', 'method',
                'priority', 'target_w', 'target_h']
        wr = csv.writer(fh)
        wr.writerow(cols)
        for r in records_out:
            wr.writerow([
                r.get('tag'), r.get('rel_path'), r.get('name'), r.get('kind'),
                r.get('fmt'), r.get('w'), r.get('h'),
                int(bool(r.get('has_alpha'))), r.get('raw_size'),
                r.get('sha256'), r.get('category'), r.get('family'),
                r.get('variant'), r.get('script_refs'),
                int(bool(r.get('has_ani'))), int(bool(r.get('is_r18'))),
                r['hd'].get('method') if isinstance(r.get('hd'), dict) else '',
                r['hd'].get('priority') if isinstance(r.get('hd'), dict) else '',
                r['hd'].get('target_w') if isinstance(r.get('hd'), dict) else '',
                r['hd'].get('target_h') if isinstance(r.get('hd'), dict) else '',
            ])

    with open(os.path.join(out_dir, 'upscale-plan.json'), 'w',
              encoding='utf-8') as fh:
        json.dump({
            'generated_at': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
            'master_scale': args.master_scale,
            'source_pixels': sum(cat_stats[c]['pixels'] for c in cat_stats),
            'entries': plan_entries,
            'by_method': {m: dict(v) for m, v in method_stats.items()},
            'by_category': {c: dict(v) for c, v in cat_stats.items()},
        }, fh, ensure_ascii=False)

    with open(os.path.join(out_dir, 'duplicates.json'), 'w',
              encoding='utf-8') as fh:
        json.dump({'groups': [
            {'sha256': sha, 'bytes': recs[0].get('raw_size'),
             'entries': [{'tag': r['tag'], 'rel_path': r['rel_path'],
                          'logical_key': r['logical_key']} for r in recs]}
            for sha, recs in sorted(dup_groups.items(),
                                    key=lambda kv: -kv[1][0].get('raw_size', 0))
        ]}, fh, ensure_ascii=False)

    with open(os.path.join(out_dir, 'overrides.json'), 'w',
              encoding='utf-8') as fh:
        json.dump(overrides, fh, ensure_ascii=False, indent=1)

    write_summary_md(os.path.join(out_dir, 'SUMMARY.md'),
                     records, plan_entries, cat_stats, method_stats,
                     problems, overrides, dup_groups, args.master_scale,
                     root)

    # -- 10. console summary ----------------------------------------------
    print('\n=== 图像资源清单 ===')
    print('%-14s %6s %10s %14s' % ('category', 'files', 'bytes', 'pixels'))
    for c in sorted(cat_stats, key=lambda c: -cat_stats[c]['files']):
        s = cat_stats[c]
        print('%-14s %6d %10.1fMB %14d' % (c, s['files'], s['bytes'] / 1e6,
                                           s['pixels']))
    print('\n=== HD 方案（按唯一内容去重后）===')
    for m in sorted(method_stats, key=lambda m: -method_stats[m]['files']):
        s = method_stats[m]
        print('%-22s %5d files  %.1f MB  src_px=%d  out_px=%d' %
              (m, s['files'], s['bytes'] / 1e6, s['pixels'], s['target_pixels']))
    print('\nproblems:', {k: len(v) for k, v in problems.items()})
    print('wrote inventory + plan to %s (%.1fs)' %
          (out_dir, time.time() - t0))
    return 0


# ---------------------------------------------------------------------------
# Markdown report
# ---------------------------------------------------------------------------
def write_summary_md(path, records, plan_entries, cat_stats, method_stats,
                     problems, overrides, dup_groups, scale, root):
    def mb(n):
        return '%.1f MB' % (n / 1e6)

    total_files = len(records)
    total_bytes = sum(r.get('raw_size', 0) for r in records)
    pngs = [r for r in records if r.get('kind') == 'png']
    anis = [r for r in records if r.get('kind') == 'ani']
    dims = collections.Counter((r.get('w'), r.get('h')) for r in pngs
                               if r.get('w'))
    alpha = sum(1 for r in pngs if r.get('has_alpha'))
    solid = sum(1 for r in pngs if r.get('solid_color'))

    L = []
    L.append('# 图像资源解包清单与 HD 高清化方案\n')
    L.append('> 由 `tools/build_image_inventory.py` 自动生成。原始数据：'
             '`assets/raw/`（6 个图像归档，来源 `assets/manifest/`）。\n')
    L.append('## 1. 总览\n')
    L.append('| 指标 | 数值 |')
    L.append('|---|---|')
    L.append('| 文件总数 | %d |' % total_files)
    L.append('| 总字节 | %d (%s) |' % (total_bytes, mb(total_bytes)))
    L.append('| PNG | %d |' % len(pngs))
    L.append('| ANI（自定义动画，与 PNG 配对） | %d |' % len(anis))
    L.append('| PSD（制作源文件） | %d |' % sum(1 for r in records if r.get('kind') == 'psd'))
    L.append('| 带 alpha 的 PNG | %d |' % alpha)
    L.append('| 实测整图单色（可程序化生成） | %d |' % solid)
    L.append('| 源像素总量 | %d |' % sum(cat_stats[c]['pixels'] for c in cat_stats))
    L.append('| 4× 母版输出像素量 | %d |' % (sum(cat_stats[c]['pixels'] for c in cat_stats) * scale ** 2))
    L.append('')
    L.append('## 2. 按类别统计\n')
    L.append('| 类别 | 文件数 | 字节 | 源像素 | 4× 目标像素 | 说明 |')
    L.append('|---|---:|---:|---:|---:|---|')
    cat_note = {
        'bg': '背景（含滚动/宽幅/宽屏变体）', 'cg': '事件 CG / 结局 CG / R18 CG',
        'tachie': '立绘', 'overlay': '前景角色层（or_*）',
        'screen': '全屏前景/场景层（sc_*）', 'title': '标题/制作名单美术',
        'tint': '1px 竖向着色/渐变带（or_/sc_，横向拉伸使用）',
        'ui': '系统 UI 元素', 'thumb': '存档/图鉴缩略图',
        'mask': '过渡遮罩/粒子/纯色层', 'ani': 'ANI 动画数据',
        'psd': 'PSD 源文件', 'source': 'PSD 源文件', 'meta': '元数据文本',
        'unclassified': '未分类（需人工复核）',
    }
    for c in sorted(cat_stats, key=lambda c: -cat_stats[c]['files']):
        s = cat_stats[c]
        L.append('| %s | %d | %s | %d | %d | %s |' % (
            c, s['files'], mb(s['bytes']), s['pixels'],
            s['pixels'] * scale ** 2, cat_note.get(c, '')))
    L.append('')
    L.append('## 3. HD 处理方案（按唯一内容去重）\n')
    L.append('| 方法 | 文件数 | 源字节 | 源像素 | 4× 目标像素 | 含义 |')
    L.append('|---|---:|---:|---:|---:|---|')
    method_note = {
        'upscale_art': 'AI 超分（动画风格模型）4× 母版',
        'upscale_ui_or_redraw': 'UI：超分或矢量重绘，二者择一',
        'derive_from_hd': '由高清 CG 降采样重新生成缩略图',
        'procedural_regen': '纯色/渐变遮罩：按目标分辨率程序化重建',
        'scale_with_pair': '.ani：坐标随配对 PNG 同比缩放（格式待逆向后处理）',
        'keep_as_source': 'PSD 保留为源素材，不参与批处理',
        'no_hd': '无需处理',
    }
    for m in sorted(method_stats, key=lambda m: -method_stats[m]['files']):
        s = method_stats[m]
        L.append('| %s | %d | %s | %d | %d | %s |' % (
            m, s['files'], mb(s['bytes']), s['pixels'], s['target_pixels'],
            method_note.get(m, '')))
    L.append('')
    L.append('**建议目标**：以 %d×（如 800×600 → %d×%d）作为**母版**，运行时再按实际窗口分辨率用 Lanczos 降采样；这样一次超分即可适配 1600×1200 / 1920×1440 / 4K 全屏。\n'
             % (scale, 800 * scale, 600 * scale))
    src_px = sum(cat_stats[c]['pixels'] for c in cat_stats)
    L.append('目标倍率与规模对照（按同一批美术资源估算）：\n')
    L.append('| 倍率 | 全屏画布 | 放大后总像素 | 相对 1x 纹理内存 |')
    L.append('|---:|---|---:|---:|')
    for s2 in (2, 3, 4, 5):
        L.append('| %dx | %dx%d | %d (%.2f Gpx) | %.1fx |' % (
            s2, 800 * s2, 600 * s2, src_px * s2 * s2,
            src_px * s2 * s2 / 1e9, s2 * s2))
    L.append('')
    L.append('## 4. 尺寸分布（PNG，Top 20）\n')
    L.append('| 尺寸 | 数量 |')
    L.append('|---|---:|')
    for (w, h), n in dims.most_common(20):
        L.append('| %d×%d | %d |' % (w, h, n))
    L.append('')
    L.append('## 5. 重复内容 / 覆盖关系\n')
    L.append('- 内容完全相同的重复组（按 sha256）：**%d 组**，见 `duplicates.json`。'
             '超分只需处理每组一份，其余用硬链接/复制。\n' % len(dup_groups))
    L.append('- 同名但来自不同归档的覆盖关系：**%d 个**逻辑名，见 `overrides.json`。'
             '其中内容相同 %d 个、内容不同 %d 个（不同者 r18 开关会切换版本）。\n' % (
                 len(overrides),
                 sum(1 for v in overrides.values() if v['same_content']),
                 sum(1 for v in overrides.values() if not v['same_content'])))
    L.append('')
    L.append('## 6. 校验问题\n')
    L.append('| 检查项 | 失败数 |')
    L.append('|---|---:|')
    for k, v in problems.items():
        L.append('| %s | %d |' % (k, len(v)))
    L.append('')
    for k, v in problems.items():
        if v:
            L.append('- `%s`: %s%s' % (k, ', '.join(v[:20]),
                                       ' …' if len(v) > 20 else ''))
    L.append('')
    L.append('## 7. 交付物\n')
    L.append('- `assets/raw/` — 原始解包资源（按归档分目录，保留原始文件名）')
    L.append('- `assets/manifest/entries_<tag>.jsonl` — 每文件抽取记录（sha256/尺寸/格式）')
    L.append('- `assets/manifest/images.json` / `images.csv` — 全量资源清单（含分类与 HD 策略）')
    L.append('- `assets/manifest/upscale-plan.json` — 按唯一内容去重的 HD 执行清单（下一步的输入）')
    L.append('- `assets/manifest/duplicates.json` / `overrides.json` — 去重与覆盖关系')
    L.append('- `work/toc/entries.jsonl` — 13 个归档完整 TOC（12,289 条）')
    L.append('')
    L.append('---\n')
    L.append('复现命令：\n')
    L.append('```bash')
    L.append('python3 tools/toc_scan.py --game-root "<游戏目录>" --out work/toc/entries.jsonl \\')
    L.append('    --stats work/toc/stats.json')
    L.append('python3 tools/extract_assets.py --game-root "<游戏目录>" \\')
    L.append('    --out-dir assets/raw --manifest-dir assets/manifest \\')
    L.append('    --jobs 6 --verify-reference 3 --archive-digests')
    L.append('python3 tools/build_image_inventory.py --project-root . --jobs 8')
    L.append('```')
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(L) + '\n')


if __name__ == '__main__':
    sys.exit(main())
