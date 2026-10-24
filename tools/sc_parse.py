#!/usr/bin/env python3
"""Minimal reference parser for minori "musica" .sc scripts (ef - the first tale).

Script files are text: GBK/CP932, CRLF. Each non-empty line is one command:
    [e].cmd args...      [j].cmd args...      .cmd args...      #include ...
The [e]/[j] prefix selects one of two timing variants (see docs).
This parser emits JSON: {"file":..., "lines":[{"cmd","variant","args","raw"}]}
"""
import json, sys, os

def parse_line(line):
    s = line.rstrip('\r\n')
    if not s.strip():
        return None
    variant = ''
    if s[:4] in ('[e].', '[j].') or s[:3] in ('[e]', '[j]'):
        variant = s[1]
        s = s[4:]
    elif s.startswith('.'):
        s = s[1:]
        variant = ''
    if s.startswith('#include'):
        return {'cmd': '#include', 'variant': '', 'args': s[len('#include'):].strip().split(), 'raw': line}
    if not s or s[0] in ' \t':
        return None
    parts = s.split(' ')
    cmd = '.' + parts[0]
    args = parts[1:] if len(parts) > 1 else []
    if cmd == '.message':
        # .message <id> <voice> <name> <text...>  (name may be empty -> double space)
        rest = s[len(parts[0]) + 1:]
        toks = rest.split(' ', 3)
        if len(toks) < 4:
            return {'cmd': cmd, 'variant': variant, 'args': parts[1:], 'raw': line}
        return {'cmd': cmd, 'variant': variant,
                'args': [toks[0], toks[1], toks[2], toks[3]], 'raw': line}
    return {'cmd': cmd, 'variant': variant, 'args': args, 'raw': line}

def parse_file(path, encoding='gbk'):
    raw = open(path, 'rb').read()
    try:
        text = raw.decode(encoding)
    except UnicodeDecodeError:
        text = raw.decode('cp932', errors='replace')
    return {'file': path, 'lines': [x for x in (parse_line(l) for l in text.split('\n')) if x]}

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    out = [parse_file(p) for p in sys.argv[1:]]
    json.dump(out, sys.stdout, ensure_ascii=False, indent=1)
