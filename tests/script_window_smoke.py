#!/usr/bin/env python3
"""Test real keyboard input -> wait -> selection -> chain -> rendered message in Xvfb."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import time

from PIL import Image
from render_smoke import ROOT, importer, window_manager


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dev/ef_native")
    parser.add_argument("--wm", default="openbox")
    args = parser.parse_args()
    out = ROOT / "build/script-window-smoke"
    (out / "scripts").mkdir(parents=True, exist_ok=True)
    font, face = importer.find_font()
    shutil.copyfile(font, out / "font.ttc")
    shutil.copyfile(ROOT / "examples/script.scene.json", out / "view.json")
    manifest = {"version": 1, "design_size": [800, 600], "entry_scene": "view.json", "font": {"path": "font.ttc", "face": face}, "images": {}}
    (out / "manifest.json").write_text(json.dumps(manifest))
    scripts = {
        "start.sc": "#include common.sc\n[e].wait 20\n[j].wait 10000\n.message 100   键盘输入测试\n.wait 100\n.select 继续:next 结束:stop\n.label next\n.chain second.sc\n.label stop\n.message 900   不应到达\n.end\n",
        "common.sc": ".set counter = 2\n",
        "second.sc": ".if counter == 2 ok\n.message 999   不应到达\n.end\n.label ok\n.message 200   分支验证通过\n.end\n",
    }
    for name, text in scripts.items():
        (out / name).write_text(text, encoding="utf-8")
    index = {"version": 1, "encoding": "utf-8", "view": "view.json", "scripts": {"scr/"+name: {"path": name} for name in scripts}}
    (out / "scripts/index.json").write_text(json.dumps(index))
    environment = dict(os.environ, SDL_VIDEO_DRIVER="x11", LIBGL_ALWAYS_SOFTWARE="1")
    with window_manager(out, args.wm):
        x11 = ctypes.CDLL("libX11.so.6")
        xtest = ctypes.CDLL("libXtst.so.6")
        x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
        x11.XStringToKeysym.restype = ctypes.c_ulong
        x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        x11.XKeysymToKeycode.restype = ctypes.c_uint
        x11.XFlush.argtypes = [ctypes.c_void_p]
        x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        xtest.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        display = x11.XOpenDisplay(None)
        if not display:
            raise RuntimeError("No Xvfb display")
        def key(name):
            code = x11.XKeysymToKeycode(display, x11.XStringToKeysym(name.encode()))
            xtest.XTestFakeKeyEvent(display, code, 1, 0)
            xtest.XTestFakeKeyEvent(display, code, 0, 0)
            x11.XFlush(display)
        process = subprocess.Popen([str(args.binary), "--content", str(out), "--script", "scr/start.sc", "--frames", "240", "--screenshot", str(out / "script.ppm")],
                                   env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = b""
        try:
            selector = selectors.DefaultSelector()
            selector.register(process.stdout, selectors.EVENT_READ)
            step = 0
            deadline = time.monotonic() + 20
            while process.poll() is None:
                if time.monotonic() > deadline:
                    raise RuntimeError("Script input smoke timed out")
                for event, _ in selector.select(.2):
                    chunk = os.read(event.fileobj.fileno(), 4096)
                    output += chunk
                if step == 0 and b"L0=100 " in output:
                    key("space"); step = 1
                elif step == 1 and b"choices count=2" in output:
                    key("1"); step = 2
            output += process.stdout.read()
            if process.returncode or step != 2 or b"message scr/second.sc L0=200 " not in output:
                raise AssertionError(output.decode("utf-8", errors="replace"))
            with Image.open(out / "script.ppm") as screenshot:
                pixels = screenshot.crop((65, 225, 500, 280)).convert("RGB").tobytes()
                if sum(1 for i in range(0, len(pixels), 3) if min(pixels[i:i+3]) > 170) < 100:
                    raise AssertionError("Script message glyphs did not render")
            print("Script window smoke PASS: actual Space/1 input, variant, include, wait, choice, branch, chain, CJK frame")
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            x11.XCloseDisplay(display)
            (out / "script.log").write_bytes(output)


if __name__ == "__main__":
    main()
