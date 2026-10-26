#!/usr/bin/env python3
"""Drive full-screen continuation/clear rules through real SDL keyboard input."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import selectors
import subprocess
import time
from PIL import Image
from message_ui_smoke import ROOT, prepare_fixture
from render_smoke import window_manager


def white(image, box):
    pixels = image.crop(box).convert("RGB").tobytes()
    return sum(min(pixels[i:i+3]) > 180 for i in range(0, len(pixels), 3))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dev/ef_native")
    parser.add_argument("--wm", default="openbox")
    args = parser.parse_args()
    out = ROOT / "build/nvl-window-smoke"
    prepare_fixture(out)
    cases = {
        "retained": ".stage * background.png 0 0\n.panel 2\n.message 10  甲 第一段\n.stage * background.png 0 0\n.message 10  乙 第二段\n.panel 2\n.message 10   \\n第三段\n.end\n",
        "cleared": ".stage * background.png 0 0\n.panel 2\n.message 10   旧页一\\n旧页二\n.message 20   新页\n.end\n",
        "waiting": ".stage * background.png 0 0\n.panel 2\n.message 10   旧页一\\n旧页二\n.panel 0\n.panel 2\n.wait 10000\n.message 20   未到达的新页\n.end\n",
        "reopened": ".stage * background.png 0 0\n.panel 2\n.message 10   旧页一\\n旧页二\n.panel 0\n.panel 2\n.message 10   新页\n.end\n",
    }
    index = json.loads((out / "scripts/index.json").read_text())
    for name, source in cases.items():
        (out / (name + ".sc")).write_text(source)
        index["scripts"]["scr/"+name+".sc"] = {"path": name+".sc"}
    (out / "scripts/index.json").write_text(json.dumps(index))
    environment = dict(os.environ, SDL_VIDEO_DRIVER="x11", LIBGL_ALWAYS_SOFTWARE="1")
    with window_manager(out, args.wm):
        x = ctypes.CDLL("libX11.so.6")
        xtest = ctypes.CDLL("libXtst.so.6")
        x.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x.XOpenDisplay.restype = ctypes.c_void_p
        x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        x.XKeysymToKeycode.restype = ctypes.c_uint
        x.XFlush.argtypes = [ctypes.c_void_p]
        x.XCloseDisplay.argtypes = [ctypes.c_void_p]
        xtest.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        display = x.XOpenDisplay(None)
        if not display:
            raise RuntimeError("No Xvfb display")
        key = x.XKeysymToKeycode(display, 32)
        try:
            for name, source in cases.items():
                expected = [(line, text.split()[1]) for line, text in enumerate(source.splitlines(), 1) if text.startswith(".message ")]
                process = subprocess.Popen([str(args.binary), "--content", str(out), "--script", "scr/"+name+".sc", "--silent",
                    "--text-speed", "0", "--frames", "180", "--screenshot", str(out / (name+".ppm")),
                    "--window-config", str(out/"window.json"), "--width", "1000", "--height", "750"],
                    env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                output = b""
                step = 0
                try:
                    selector = selectors.DefaultSelector()
                    selector.register(process.stdout, selectors.EVENT_READ)
                    deadline = time.monotonic()+20
                    while process.poll() is None:
                        if time.monotonic()>deadline:
                            raise RuntimeError("NVL input test timed out")
                        for event, _ in selector.select(.1):
                            output += os.read(event.fileobj.fileno(), 4096)
                        if step < len(expected)-1:
                            line, identifier = expected[step]
                            token = f"message scr/{name}.sc L0={identifier} source_line={line}".encode()
                            position = output.find(token)
                            ready = f"message ready scr/{name}.sc L0={identifier}".encode()
                            if position >= 0 and output.find(ready, position) >= 0:
                                xtest.XTestFakeKeyEvent(display, key, 1, 0)
                                xtest.XTestFakeKeyEvent(display, key, 0, 0)
                                x.XFlush(display)
                                step += 1
                    output += process.stdout.read()
                    if process.returncode or step != len(expected)-1:
                        raise AssertionError(output.decode(errors="replace"))
                    last_line, last_id = expected[-1]
                    last_token = f"message scr/{name}.sc L0={last_id} source_line={last_line}".encode()
                    assert (last_token not in output) if name == "waiting" else (last_token in output)
                    with Image.open(out / (name+".ppm")) as screenshot:
                        if name == "waiting":
                            assert white(screenshot, (15, 20, 950, 700)) == 0, "old page reappeared during wait"
                        else:
                            assert white(screenshot, (150, 20, 850, 60)) > 100, name
                        if name == "retained":
                            assert white(screenshot, (150, 60, 850, 100)) > 100
                            assert white(screenshot, (150, 105, 850, 135)) == 0
                            assert white(screenshot, (150, 140, 850, 180)) > 100
                            assert white(screenshot, (15, 20, 150, 60)) > 30
                            assert white(screenshot, (15, 60, 150, 100)) > 30
                        else:
                            assert white(screenshot, (150, 60, 850, 180)) == 0, name
                finally:
                    if process.poll() is None:
                        process.kill(); process.wait()
                    (out / (name+".log")).write_bytes(output)
        finally:
            x.XCloseDisplay(display)
    print("NVL window smoke PASS: preserved paragraphs/speakers, implicit+explicit newlines, stage/same-mode retention, new-L0 clear and mode reentry/wait reset")


if __name__ == "__main__":
    main()
