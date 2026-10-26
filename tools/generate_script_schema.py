#!/usr/bin/env python3
"""Generate the public ef.script v1 JSON Schema from the shared opcode specification."""
import argparse
import json
from pathlib import Path
from script_json import OPS, encode

ROOT = Path(__file__).resolve().parents[1]
S = {'type': 'string', 'pattern': r'^[^\u0000]*$'}
I = {'type': 'integer', 'minimum': -(1 << 63), 'maximum': (1 << 63)-1}
A = {'anyOf': [S, {'type': 'null'}]}
B = {'type': 'boolean'}


def obj(properties, required=None):
    return {'type': 'object', 'properties': properties, 'required': list(properties) if required is None else required,
            'additionalProperties': False}


def build():
    text = obj({'type': {'const': 'text'}, 'value': S})
    events = {'type': 'array', 'maxItems': 65536, 'items': {'oneOf': [text,
        obj({'type': {'const': 'pause'}, 'ticks': {'type': 'integer', 'minimum': 0, 'maximum': 1000000}}),
        obj({'type': {'enum': ['auto_advance', 'wait_voice']}})]}}
    specials = {
        'dialogue': obj({'id': {'type': 'integer', 'minimum': 0, 'maximum': 65535}, 'voice': S, 'speaker': S, 'events': events}),
        'stage': obj({'frame': {**obj({'asset': A, 'x': I, 'y': I}, ['asset']), 'dependentRequired': {'x': ['y'], 'y': ['x']}},
            'background': obj({'asset': A, 'crop_x': I, 'crop_y': I}),
            'actors': {'type': 'array', 'maxItems': 10, 'items': obj({'asset': A, 'center_x': I, 'bottom_trim': I}, ['asset', 'center_x'])}}),
        'screen': obj({'frame': A, 'background': A, 'foreground': A}, ['background', 'foreground']),
        'color_filter': obj({k: {'type': 'array', 'minItems': 3, 'maxItems': 3, 'items': I} for k in ['background', 'foreground']}),
        'branch': obj({'variable': S, 'comparison': {'enum': ['==', '!=', '<', '<=', '>', '>=']}, 'value': I, 'label': S}),
        'choices': obj({'options': {'type': 'array', 'minItems': 2, 'maxItems': 256, 'items': obj({
            'text': {**S, 'minLength': 1}, 'label': {'type': 'string', 'minLength': 1, 'pattern': r'^[^:\u0000]+$'}})}}),
        'effect': {'oneOf': [obj({'name': S}), obj({'name': {'const': 'Cutin'}, 'asset': A, 'hold_ticks': I, 'rate': I}, ['name', 'asset', 'hold_ticks']),
                             obj({'name': {'const': 'WScroll2'}, 'objects': A, 'period': I, 'speed': I})]},
    }
    specials['overlay'] = specials['screen']
    for op in ['set', 'set_global']:
        specials[op] = {**obj({'name': S, 'value': I, 'from_variable': S, 'operator': {'enum': ['+', '-']}}, ['name', 'value']),
                        'dependentRequired': {'from_variable': ['operator'], 'operator': ['from_variable']}}
    alternatives = []
    types = {'string': S, 'integer': I, 'asset': A, 'flag': B, 'loop': B, 'movie_flag': B,
             'default_integer': I}
    for command, rule in OPS.items():
        op = rule['op']
        if rule.get('special'):
            params = specials[op]
        else:
            params = obj({f['name']: types[f['type']] for f in rule['fields']}, [f['name'] for f in rule['fields'][:rule['minimum']]])
        alternatives.append(obj({'op': {'const': op}, 'params': params, 'source_line': {'type': 'integer', 'minimum': 1, 'maximum': (1 << 63)-1},
                                 'variant': {'enum': ['e', 'j']}, 'colon_variant': B}, ['op', 'params', 'source_line']))
    alternatives.append(obj({'op': {'const': 'unsupported'}, 'params': obj({'command': S, 'arguments': {'type': 'array', 'items': S}, 'raw': S, 'reason': S}),
                             'source_line': {'type': 'integer', 'minimum': 1}, 'variant': {'enum': ['e', 'j']}, 'colon_variant': B}, ['op', 'params', 'source_line']))
    return {'$schema': 'https://json-schema.org/draft/2020-12/schema', 'title': 'ef.script v1',
            **obj({'format': {'const': 'ef.script'}, 'version': {'const': 1}, 'id': S,
                   'instructions': {'type': 'array', 'maxItems': 200000, 'items': {'oneOf': alternatives}}})}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--check', action='store_true')
    a = p.parse_args()
    target = ROOT/'schemas/script-v1.schema.json'
    data = encode(build())
    if a.check:
        if not target.is_file() or target.read_bytes() != data:
            p.exit(1, 'Schema is stale; run tools/generate_script_schema.py\n')
    else:
        target.write_bytes(data)
