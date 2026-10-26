"""Offline SC compiler for ef.script v1; never used to interpret scripts at runtime."""
import hashlib
import json
from pathlib import Path
import re

OPS = json.loads((Path(__file__).resolve().parents[1] / 'schemas/script-ops.json').read_text())
BY_OP = {v['op']: (k, v) for k, v in OPS.items()}


def integer(s):
    if not isinstance(s, str) or not re.fullmatch(r'-?[0-9]+', s):
        raise ValueError(f'Expected integer: {s!r}')
    n = int(s)
    if not -(1 << 63) <= n < (1 << 63):
        raise ValueError('Integer overflow')
    return n


def atom(s, kind):
    if kind in ('integer', 'default_integer'):
        return 2 if kind == 'default_integer' and s == '*' else integer(s)
    if kind == 'asset':
        return None if s == '*' else s
    if kind == 'flag':
        if s not in ('t', 'f'):
            raise ValueError('Expected t/f')
        return s == 't'
    if kind == 'loop':
        return s.startswith('t')
    if kind == 'movie_flag':
        return not s.startswith(('0', 'f', 'F'))
    return s


def dialogue_events(text):
    if len(text.encode('utf-8')) > 1024 * 1024 or '\0' in text:
        raise ValueError('Invalid dialogue source budget/NUL')
    events, buf = [], []
    def flush():
        if buf:
            events.append({'type': 'text', 'value': ''.join(buf)})
            buf.clear()
    i = 0
    while i < len(text):
        c = text[i]
        i += 1
        if c != '\\':
            buf.append(c)
            continue
        if i == len(text):
            raise ValueError('Dangling dialogue escape')
        c = text[i]
        i += 1
        if c in ('n', '\\'):
            buf.append('\n' if c == 'n' else '\\')
        elif c in 'awv':
            flush()
            events.append({'type': {'a': 'auto_advance', 'w': 'pause', 'v': 'wait_voice'}[c],
                           **({'ticks': 10} if c == 'w' else {})})
        else:
            raise ValueError(f'Unsupported dialogue escape: {c!r}')
    flush()
    return events


def legacy_lines(text):
    if '\0' in text:
        raise ValueError('NUL in script')
    for line_no, raw in enumerate(text.removeprefix('\ufeff').split('\n'), 1):
        raw = raw.removesuffix('\r')
        line = raw.lstrip(' \t')
        if not line:
            continue
        note = line[3:] if line.startswith(('[e]', '[j]')) else line
        if note.startswith('；'):
            continue
        variant, colon = '', False
        if line.startswith(('[e]:.', '[j]:.')):
            variant, colon, line = line[1], True, line[4:]
        elif line.startswith(('[e].', '[j].')):
            variant, line = line[1], line[3:]
        parts = re.split('[ \t]', line, maxsplit=1)
        command, rest = parts[0], parts[1] if len(parts) == 2 else ''
        if not line.startswith('.') and not line.startswith('#include'):
            command = '<unparsed>'
        args = rest.split(' ', 3) if command == '.message' else rest.split()
        yield {'line': line_no, 'variant': variant, 'colon_variant': colon,
               'command': command, 'args': args, 'raw': raw}


def parameters(command, a):
    rule = OPS[command]
    # Native FUN_004ea6e0 consumes only argument 0; trailing tokens are ignored.
    if command == '.playVoice' and a:
        a = a[:1]
    if not rule.get('special'):
        fields = rule['fields']
        if not rule['minimum'] <= len(a) <= len(fields):
            raise ValueError('Argument count')
        return {f['name']: atom(s, f['type']) for f, s in zip(fields, a)}
    if command == '.message':
        if len(a) != 4 or not 0 <= integer(a[0]) <= 65535:
            raise ValueError('Invalid dialogue fields/L0')
        return {'id': integer(a[0]), 'voice': a[1], 'speaker': a[2], 'events': dialogue_events(a[3])}
    if command == '.stage':
        if not 4 <= len(a) <= 26:
            raise ValueError('Stage count')
        frame, cursor = {'asset': atom(a[0], 'asset')}, 1
        try:
            x, y = integer(a[1]), integer(a[2])
        except ValueError:
            pass
        else:
            frame.update(x=x, y=y)
            cursor += 2
        if len(a) < cursor+3 or (len(a)-cursor-3) % 2:
            raise ValueError('Stage fields')
        bg = {'asset': atom(a[cursor], 'asset'), 'crop_x': integer(a[cursor+1]), 'crop_y': integer(a[cursor+2])}
        actors = []
        for n in range(cursor+3, len(a), 2):
            position = a[n+1].split(',')
            if len(position) not in (1, 2):
                raise ValueError('Stage placement')
            actor = {'asset': atom(a[n], 'asset'), 'center_x': integer(position[0])}
            if len(position) == 2:
                actor['bottom_trim'] = integer(position[1])
            actors.append(actor)
        return {'frame': frame, 'background': bg, 'actors': actors}
    if command in ('.screen', '.overlay'):
        if len(a) not in (2, 3):
            raise ValueError('Material count')
        return dict(zip(['frame', 'background', 'foreground'][-len(a):], [atom(s, 'asset') for s in a]))
    if command == '.HSVfilter':
        if len(a) != 6:
            raise ValueError('HSV count')
        return {'background': [integer(s) for s in a[:3]], 'foreground': [integer(s) for s in a[3:]]}
    if command in ('.set', '.setGlobal'):
        if len(a) not in (3, 5) or a[1] != '=':
            raise ValueError('Assignment')
        result = {'name': a[0], 'value': integer(a[-1])}
        if len(a) == 5:
            if a[3] not in ('+', '-'):
                raise ValueError('Assignment operator')
            result.update(from_variable=a[2], operator=a[3])
        return result
    if command == '.if':
        if len(a) != 4 or a[1] not in ('==', '!=', '<', '<=', '>', '>='):
            raise ValueError('Branch')
        return {'variable': a[0], 'comparison': a[1], 'value': integer(a[2]), 'label': a[3]}
    if command == '.select':
        if len(a) < 2:
            raise ValueError('Choices count')
        choices = []
        for s in a:
            text, label = s.rsplit(':', 1)
            if not text or not label:
                raise ValueError('Choice')
            choices.append({'text': text, 'label': label})
        return {'options': choices}
    if command == '.effect':
        if not a:
            raise ValueError('Effect count')
        name = 'end' if a[0] == '*' else a[0]
        if len(a) == 1:
            return {'name': name}
        if name == 'Cutin' and len(a) in (3, 4):
            return {'name': name, 'asset': atom(a[1], 'asset'), 'hold_ticks': integer(a[2]),
                    **({'rate': integer(a[3])} if len(a) == 4 else {})}
        if name == 'WScroll2' and len(a) == 4:
            return {'name': name, 'objects': atom(a[1], 'asset'), 'period': integer(a[2]), 'speed': integer(a[3])}
        raise ValueError('Unmapped effect parameters')
    raise ValueError('Unmapped operation')


def compile_script(text, identifier):
    nodes = []
    for line in legacy_lines(text):
        meta = {'source_line': line['line']}
        if line['variant']:
            meta['variant'] = line['variant']
        if line['colon_variant']:
            meta['colon_variant'] = True
        try:
            params = parameters(line['command'], line['args'])
            node = {**meta, 'op': OPS[line['command']]['op'], 'params': params}
        except (KeyError, ValueError) as error:
            # Preserve unknown input verbatim, never turn it into a no-op.
            node = {**meta, 'op': 'unsupported', 'params': {'command': line['command'],
                    'arguments': line['args'], 'raw': line['raw'], 'reason': str(error)}}
        nodes.append(node)
    return {'format': 'ef.script', 'version': 1, 'id': identifier, 'instructions': nodes}


def encode(document):
    return (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + '\n').encode('utf-8')


def write_fixture_scripts(root):
    """Compile synthetic .sc fixtures after a test finishes editing its source index."""
    root = Path(root)
    index_path = root / 'scripts/index.json'
    index = json.loads(index_path.read_text())
    for identifier, spec in index['scripts'].items():
        source = root / spec['path']
        if source.suffix != '.sc':
            continue
        output = source.with_suffix('.script.json')
        output.write_bytes(encode(compile_script(source.read_text(encoding='utf-8'), identifier)))
        spec['path'] = output.relative_to(root).as_posix()
        source.unlink()
    index.update(version=2, format='ef.script.v1', encoding='utf-8')
    index_path.write_bytes(encode(index))


def native_arguments(node):
    """Offline reference-audit adapter; runtime uses the C++ typed decoder."""
    p = node['params']
    if node['op'] == 'unsupported':
        return p['command'], p['arguments']
    cmd, rule = BY_OP[node['op']]
    def token(v):
        if v is None:
            return '*'
        if isinstance(v, bool):
            return 't' if v else 'f'
        return str(v)
    if not rule.get('special'):
        count = rule['minimum']
        for n, f in enumerate(rule['fields']):
            if f['name'] in p:
                count = max(count, n+1)
        return cmd, [token(p[f['name']] if f['name'] in p else f['default']) for f in rule['fields'][:count]]
    if cmd == '.message':
        return cmd, [str(p['id']), p['voice'], p['speaker'], ''.join(e.get('value', '') for e in p['events'])]
    if cmd == '.stage':
        f, b = p['frame'], p['background']
        a = [token(f['asset'])]
        if 'x' in f:
            a += [str(f['x']), str(f['y'])]
        a += [token(b['asset']), str(b['crop_x']), str(b['crop_y'])]
        for actor in p['actors']:
            a += [token(actor['asset']), str(actor['center_x']) + (','+str(actor['bottom_trim']) if 'bottom_trim' in actor else '')]
        return cmd, a
    if cmd in ('.screen', '.overlay'):
        return cmd, [token(p[k]) for k in ('frame', 'background', 'foreground') if k in p]
    if cmd == '.HSVfilter':
        return cmd, [str(n) for n in p['background']+p['foreground']]
    if cmd in ('.set', '.setGlobal'):
        return cmd, [p['name'], '='] + ([p['from_variable'], p['operator']] if 'from_variable' in p else []) + [str(p['value'])]
    if cmd == '.if':
        return cmd, [p['variable'], p['comparison'], str(p['value']), p['label']]
    if cmd == '.select':
        return cmd, [c['text']+':'+c['label'] for c in p['options']]
    if cmd == '.effect':
        a = [p['name']]
        if 'asset' in p:
            a += [token(p['asset']), str(p['hold_ticks'])]
            if 'rate' in p:
                a.append(str(p['rate']))
        elif 'objects' in p:
            a += [token(p['objects']), str(p['period']), str(p['speed'])]
        return cmd, a
    raise ValueError('Unmapped JSON operation')
