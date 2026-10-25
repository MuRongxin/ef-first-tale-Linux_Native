#!/usr/bin/env python3
"""Create synthetic font fixtures for the OpenGL text appearance test."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from message_ui_smoke import ROOT, prepare_fixture

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='text-effects-', dir=ROOT/'build') as temporary:
    out = Path(temporary)
    prepare_fixture(out)
    subprocess.run([str(args.binary), str(out), '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'], check=True)
