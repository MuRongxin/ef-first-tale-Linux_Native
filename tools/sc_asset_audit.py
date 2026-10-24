#!/usr/bin/env python3
"""Audit every asset referenced by the .sc corpus against what the PAZ archives actually contain.

Run:  python3 port/tools/sc_asset_audit.py [--root GAMEDIR]
Uses analysis/paz/paz.py (`list`) to enumerate archive entries, and the extracted
scripts under analysis/paz/extracted/{scr,r18_scr} for references.
Outputs: port/docs/asset-audit.json + a short summary on stdout.
"""
import json, os, re, subprocess, sys, collections

REF_RE = re.compile(r'[\w./\\-]+\.(png|ogg|wav|avi|mpg|mpeg|sc|ani|psd|dat|txt)', re.I)

def main(root):
    inv = collections.defaultdict(list)
    for arc in ['bg','bgm','mov','scr','se','st','sys','voice',
                'r18_cn/bg','r18_cn/scr','r18_cn/st','r18_cn/sys','r18_cn/voice']:
        p = os.path.join(root, arc + '.paz')
        if not os.path.exists(p):
            continue
        out = subprocess.run([sys.executable, os.path.join(root,'analysis/paz/paz.py'), 'list', p],
                             capture_output=True, text=True).stdout
        for ln in out.splitlines():
            m = re.match(r'^\s*\d+\s+\d+\s+\d+\s+[-P ]?\s*(.+?)\s*$', ln)
            if m:
                inv[arc].append(m.group(1))
    names = {}
    for arc, fs in inv.items():
        for f in fs:
            names.setdefault(os.path.basename(f).lower(), []).append(arc)
    refs = collections.Counter()
    per_file = collections.Counter()
    for d in ['analysis/paz/extracted/scr','analysis/paz/extracted/r18_scr']:
        dp = os.path.join(root, d)
        if not os.path.isdir(dp):
            continue
        for f in sorted(os.listdir(dp)):
            if not f.lower().endswith('.sc'):
                continue
            raw = open(os.path.join(dp,f),'rb').read()
            try: text = raw.decode('gbk')
            except UnicodeDecodeError: text = raw.decode('cp932','replace')
            for m in REF_RE.finditer(text):
                refs[m.group(0).replace('\\','/').lower()] += 1
    found, missing = {}, {}
    for ref, n in sorted(refs.items()):
        b = os.path.basename(ref)
        if b in names:
            found[ref] = {'count': n, 'archives': names[b]}
        else:
            missing[ref] = n
    result = {
        'archive_entries': {a: len(v) for a, v in inv.items()},
        'distinct_asset_refs': len(refs),
        'total_ref_occurrences': sum(refs.values()),
        'found': found,
        'missing': missing,
    }
    os.makedirs(os.path.join(root,'port/docs'), exist_ok=True)
    json.dump(result, open(os.path.join(root,'port/docs/asset-audit.json'),'w'),
              ensure_ascii=False, indent=1)
    print(json.dumps({k: result[k] for k in
          ('archive_entries','distinct_asset_refs','total_ref_occurrences')}, ensure_ascii=False))
    print('found refs:', len(found), ' missing refs:', len(missing))
    if missing:
        print('missing sample:', list(missing.items())[:20])
    by_ext = collections.Counter(os.path.splitext(r)[1] for r in refs)
    print('refs by ext:', dict(by_ext))

if __name__ == '__main__':
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    if len(sys.argv) > 1:
        root = sys.argv[1]
    main(root)
