#!/usr/bin/env python3
"""Exercise panel timing using synthetic scripts/images and a generated tone fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from message_ui_smoke import ROOT, prepare_fixture

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='panel-transition-', dir=ROOT/'build') as temporary:
    out = Path(temporary)
    prepare_fixture(out)
    shutil.copyfile(ROOT/'tests/fixtures/audio-tone.ogg', out/'tone.ogg')
    catalog = json.loads((out/'catalog.json').read_text())
    catalog['entries']['voice/tone.ogg'] = {'kind': 'voice', 'name': 'tone.ogg', 'tag': 'voice',
        'path': 'tone.ogg', 'source_sha256': hashlib.sha256((out/'tone.ogg').read_bytes()).hexdigest()}
    (out/'catalog.json').write_text(json.dumps(catalog))
    (out/'timing.sc').write_text('.stage * background.png 0 0\n.panel 1\n.message 1  姓名 旧正文\n'
        '.panel 2\n.stage * background.png 0 0\n.message 2 tone.ogg  新正文\n'
        '.panel 2\n.message 2   续写正文\n.panel 0\n.wait 5\n.panel 3\n.message 3   最终正文\n.end\n')
    index = json.loads((out/'scripts/index.json').read_text())
    index['scripts']['scr/timing.sc'] = {'path': 'timing.sc'}
    (out/'scripts/index.json').write_text(json.dumps(index))
    subprocess.run([str(args.binary), str(out)], check=True)
