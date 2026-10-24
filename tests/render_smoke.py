#!/usr/bin/env python3
"""Native GL integration test with synthetic images; run inside Xvfb (1600x1200).

Checks actual framebuffer pixels, soft alpha, letterboxing, Chinese glyphs and
4x/8x selection, including corrupt/missing 8x fallback. No game data required.
"""
import argparse
from contextlib import contextmanager
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_content", ROOT / "tools/prepare_content.py")
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


def near(actual, expected, tolerance=2):
    if any(abs(a-b) > tolerance for a, b in zip(actual, expected)):
        raise AssertionError(f"Pixel {actual} != {expected}")


@contextmanager
def window_manager(out, binary):
    existing = subprocess.run(["xprop", "-root", "_NET_SUPPORTING_WM_CHECK"], capture_output=True, text=True, check=True)
    if "window id #" in existing.stdout:
        raise RuntimeError("Use an isolated Xvfb display, not an existing desktop session")
    with (out / "openbox.log").open("w") as log:
        process = subprocess.Popen([binary, "--sm-disable", "--config-file", str(ROOT / "tests/openbox-rc.xml")], stdout=log, stderr=log)
        try:
            for _ in range(50):
                state = subprocess.run(["xprop", "-root", "_NET_SUPPORTING_WM_CHECK"], capture_output=True, text=True, check=True)
                if "window id #" in state.stdout:
                    break
                if process.poll() is not None:
                    raise RuntimeError(f"Openbox failed; see {out / 'openbox.log'}")
                time.sleep(0.1)
            else:
                raise RuntimeError("Window manager did not become ready")
            yield
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dev/ef_native")
    parser.add_argument("--out", type=Path, default=ROOT / "build/render-smoke")
    parser.add_argument("--wm", default="openbox", help="Openbox executable; started only in an empty Xvfb display")
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT / "build"):
        parser.error("--out must be under the repository's build directory")
    out.mkdir(parents=True, exist_ok=True)
    font, face = importer.find_font()
    shutil.copyfile(font, out / "font.ttc")
    variants = {}
    for tier, color in [(4, (200, 40, 20, 255)), (8, (20, 60, 200, 255))]:
        image = Image.new("RGBA", (200*tier, 100*tier), color)
        image.paste((*color[:3], 128), (100*tier, 0, 200*tier, 100*tier))
        image.save(out / f"{tier}.webp", lossless=True)
        variants[str(tier)] = {"path": f"{tier}.webp", "pixels": list(image.size)}
    manifest = {
        "version": 1, "design_size": [800, 600], "entry_scene": "scene.json",
        "font": {"path": "font.ttc", "face": face},
        "images": {"fixture": {"design_size": [200, 100], "variants": variants}},
    }
    scene = {"version": 1, "title": "渲染验证", "background": [0.1, 0.2, 0.3, 1], "layers": [
        {"kind": "image", "asset": "fixture"},
        {"kind": "text", "x": 20, "y": 160, "font_size": 24, "text": "中文渲染验证"},
    ]}
    (out / "manifest.json").write_text(json.dumps(manifest))
    (out / "scene.json").write_text(json.dumps(scene))
    environment = dict(os.environ, SDL_VIDEO_DRIVER="x11", LIBGL_ALWAYS_SOFTWARE="1")

    def run(name, *arguments, success=True):
        command = [str(args.binary.resolve()), "--content", str(out), *arguments]
        result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=45)
        (out / f"{name}.log").write_text(result.stdout + result.stderr)
        if (result.returncode == 0) != success:
            raise AssertionError(f"{name}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
        return result.stdout + result.stderr

    def check(name, color, fullscreen=False):
        with Image.open(out / f"{name}.ppm") as image:
            expected_size = (1600, 1200) if fullscreen else (1200, 600)
            if image.size != expected_size:
                raise AssertionError(f"Run Xvfb at 1600x1200; got {image.size} for {name}")
            scale, offset = (2, 0) if fullscreen else (1, 200)
            near(image.getpixel((offset + 50*scale, 50*scale)), color)
            alpha = 128 / 255
            blended = tuple(round(c*alpha+b*(1-alpha)) for c, b in zip(color, (26, 51, 76)))
            near(image.getpixel((offset + 150*scale, 50*scale)), blended)
            near(image.getpixel((offset + 300*scale, 50*scale)), (26, 51, 76))
            if not fullscreen:
                near(image.getpixel((10, 50)), (0, 0, 0), 0)
                near(image.getpixel((1190, 50)), (0, 0, 0), 0)
            # A missing glyph/render path would leave this area all background.
            text = image.crop((offset+20*scale, 160*scale, offset+200*scale, 210*scale))
            pixels = text.tobytes()
            if sum(1 for i in range(0, len(pixels), 3) if min(pixels[i:i+3]) > 160) < 100:
                raise AssertionError("Chinese text was not rasterized")

    with window_manager(out, args.wm):
        run("validate", "--validate-content")
        log = run("window", "--width", "1200", "--height", "600", "--smoke", "--screenshot", str(out / "window.ppm"))
        if "Window smoke PASS" not in log:
            raise AssertionError("Did not observe 4x -> 8x -> 4x native window transitions")
        check("window", (200, 40, 20))
        run("fullscreen", "--fullscreen", "--frames", "3", "--screenshot", str(out / "fullscreen.ppm"))
        check("fullscreen", (20, 60, 200), True)
        log = run("maximized", "--maximized", "--frames", "6")
        if "window tier=8 maximized=1" not in log or "requested=8 selected=8" not in log:
            raise AssertionError("Native maximize did not select 8x")
        (out / "8.webp").write_bytes(b"RIFFbroken")
        run("invalid", "--validate-content", success=False)
        log = run("fallback", "--fullscreen", "--frames", "3", "--screenshot", str(out / "fallback.ppm"))
        if "[asset fallback]" not in log:
            raise AssertionError("Corrupt HD fallback was not reported")
        check("fallback", (200, 40, 20), True)
        (out / "8.webp").unlink()
        run("missing", "--fullscreen", "--frames", "3", "--screenshot", str(out / "missing.ppm"))
        check("missing", (200, 40, 20), True)
    print(f"Render smoke PASS: native state cycle, actual 4x/8x pixels, alpha, letterbox, CJK, corrupt/missing HD fallback ({out})")


if __name__ == "__main__":
    main()
