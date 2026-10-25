#!/usr/bin/env python3
"""Exercise user font selection, fallback, recovery and live persisted controls."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import time
import tempfile
from message_ui_smoke import ROOT, prepare_fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT/'build/dev/ef_native')
    parser.add_argument('--faces-binary', type=Path, default=ROOT/'build/dev/ef_font_faces_tests')
    parser.add_argument('--faces-only', action='store_true')
    args = parser.parse_args()
    latin = Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
    if not latin.is_file():
        raise RuntimeError('Install fonts-dejavu-core for the Latin-only fallback test')
    if args.faces_only:
        with tempfile.TemporaryDirectory(prefix='font-faces-', dir=ROOT/'build') as temporary:
            out = Path(temporary)
            prepare_fixture(out)
            subprocess.run([str(args.faces_binary), str(out), str(latin)], check=True)
        return
    out = ROOT/'build/font-customization-smoke'
    prepare_fixture(out)
    (out/'nvl.sc').write_text('.stage * background.png 0 0\n.panel 2\n.message 1  姓名 '+r'\n'*8+'长'*135+'\n.end\n')
    index = json.loads((out/'scripts/index.json').read_text())
    index['scripts']['scr/nvl.sc'] = {'path': 'nvl.sc'}
    (out/'scripts/index.json').write_text(json.dumps(index))
    config = out/'preferences/font.json'
    config.parent.mkdir(exist_ok=True)
    config.unlink(missing_ok=True)
    env = dict(os.environ, SDL_VIDEO_DRIVER='x11', LIBGL_ALWAYS_SOFTWARE='1')
    base = [str(args.binary), '--content', str(out), '--font-config', str(config)]

    def run(extra, success=True):
        result = subprocess.run(base+extra, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        if (result.returncode == 0) != success:
            raise AssertionError(result.stdout)
        return result.stdout

    # A missing custom CJK glyph renders through the packaged fallback, including a long message.
    run(['--font', str(latin), '--font-size', '150', '--script', 'scr/long.sc', '--silent',
         '--text-speed', '0', '--frames', '5', '--screenshot', str(out/'custom.ppm')])
    saved = json.loads(config.read_text())
    assert saved['file'] == str(latin) and saved['scale'] == 1.5
    for name in ('normal', 'cinema', 'nvl'):
        run(['--script', 'scr/'+name+'.sc', '--silent', '--text-speed', '0', '--frames', '5',
             '--screenshot', str(out/(name+'.ppm'))])
    run(['--validate-content'])
    run(['--font', str(out/'missing.ttf'), '--frames', '1'], success=False)
    run(['--font', str(latin), '--font-face', '65535', '--frames', '1'], success=False)
    assert json.loads(config.read_text()) == saved, 'Failed selection modified preferences'
    saved['file'] = str(out/'removed.ttf')
    config.write_text(json.dumps(saved))
    assert 'Saved font unavailable' in run(['--frames', '1'])
    assert json.loads(config.read_text()) == saved, 'Recovery overwrote a temporarily unavailable font'
    config.write_text('{broken')
    assert 'Cannot read preferences' in run(['--frames', '1'])

    # Real keyboard events apply a new collection face and size without advancing dialogue.
    process = subprocess.Popen(base+['--font', str(out/'font.ttc'), '--font-size', '100',
        '--script', 'scr/normal.sc', '--silent', '--text-speed', '0', '--frames', '300'],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    x = ctypes.CDLL('libX11.so.6')
    xtest = ctypes.CDLL('libXtst.so.6')
    x.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    x.XKeysymToKeycode.restype = ctypes.c_uint
    x.XFlush.argtypes = [ctypes.c_void_p]
    x.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xtest.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    display = x.XOpenDisplay(None)
    try:
        if not display:
            raise RuntimeError('No X11 display')

        def wait_settings(predicate):
            deadline = time.monotonic()+4
            while time.monotonic()<deadline:
                try:
                    value = json.loads(config.read_text())
                    if predicate(value):
                        return
                except (ValueError, FileNotFoundError):
                    pass
                if process.poll() is not None:
                    break
                time.sleep(.02)
            raise AssertionError('Live font preferences did not reach expected state')

        def key(keysym):
            code = x.XKeysymToKeycode(display, keysym)
            xtest.XTestFakeKeyEvent(display, code, 1, 0)
            xtest.XTestFakeKeyEvent(display, code, 0, 0)
            x.XFlush(display)

        wait_settings(lambda s: s['file'] == str(out/'font.ttc'))
        time.sleep(.2)  # Let the window receive keyboard focus on a bare Xvfb server.
        key(0xffc3)  # F6
        wait_settings(lambda s: s['face'] == 1)
        key(0xffc5)  # F8
        wait_settings(lambda s: abs(s['scale']-1.05)<.001)
        key(0xffc6)  # F9
        wait_settings(lambda s: s['file'] == '' and s['face'] == 0 and s['scale'] == 1)
        stdout = process.communicate(timeout=10)[0].decode(errors='replace')
        assert process.returncode == 0, stdout
        assert 'font applied=' in stdout
    finally:
        if display:
            x.XCloseDisplay(display)
        if process.poll() is None:
            process.kill(); process.wait()
    print('Font customization PASS: Latin/CJK fallback, long text auto-fit, TTC face, persistence, invalid-file recovery and live size/reset')


if __name__ == '__main__':
    main()
