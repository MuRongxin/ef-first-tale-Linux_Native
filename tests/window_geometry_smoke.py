#!/usr/bin/env python3
"""Window geometry smoke test: adaptive default, size memory, explicit override and corrupt fallback."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_content", ROOT / "tools/prepare_content.py")
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


def ppm_size(path):
    with path.open("rb") as handle:
        assert handle.readline().strip() == b"P6"
        line = handle.readline()
        while line.startswith(b"#"):
            line = handle.readline()
        width, height = map(int, line.split())
        return width, height


def display_size(environment):
    try:
        output = subprocess.run(["xrandr", "--current"], env=environment, capture_output=True, text=True, timeout=10)
        for token in output.stdout.split():
            if "x" in token:
                width, _, height = token.partition("x")
                if width.isdigit() and height.split("+")[0].isdigit():
                    return int(width), int(height.split("+")[0])
    except (OSError, subprocess.SubprocessError):
        pass
    return 1280, 1024


def adaptive_size(width, height):
    available_width, available_height = width * 9 // 10, height * 9 // 10
    for tier_width, tier_height in reversed([(800, 600), (1200, 900), (1600, 1200),
                                             (2000, 1500), (2400, 1800), (3200, 2400)]):
        if tier_width <= available_width and tier_height <= available_height:
            return tier_width, tier_height
    unit = max(80, min(width // 4, height // 3))
    return unit * 4, unit * 3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dev/ef_native")
    args = parser.parse_args()
    out = ROOT / "build/window-geometry-smoke"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    font, face = importer.find_font()
    shutil.copyfile(font, out / "font.ttc")
    manifest = {
        "version": 1, "design_size": [800, 600], "entry_scene": "scene.json",
        "font": {"path": "font.ttc", "face": face}, "images": {},
    }
    scene = {"version": 1, "title": "window geometry", "background": [0, 0, 0, 1], "layers": []}
    (out / "manifest.json").write_text(json.dumps(manifest))
    (out / "scene.json").write_text(json.dumps(scene))
    config = out / "window.json"
    environment = dict(os.environ, SDL_VIDEO_DRIVER="x11", LIBGL_ALWAYS_SOFTWARE="1",
                       XDG_CONFIG_HOME=str(out / "xdg-config"))

    def run(name, *arguments, success=True):
        capture = out / (name + ".ppm")
        capture.unlink(missing_ok=True)
        command = [str(args.binary.resolve()), "--content", str(out), "--window-config", str(config),
                   "--frames", "2", "--screenshot", str(capture), *arguments]
        result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=60)
        (out / (name + ".log")).write_text(result.stdout + result.stderr)
        if (result.returncode == 0) != success:
            raise AssertionError(f"{name}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
        return result, ppm_size(capture) if success else None

    def saved():
        document = json.loads(config.read_text())
        assert document["version"] == 1 and not document["maximized"], document
        return document["width"], document["height"]

    # Explicit CLI size is saved and restored on the next run.
    _, size = run("explicit", "--width", "900", "--height", "700")
    assert size == (900, 700), size
    assert saved() == (900, 700), saved()
    _, size = run("restored")
    assert size == (900, 700), size

    # Another explicit size overwrites the remembered geometry.
    _, size = run("override", "--width", "1000", "--height", "750")
    assert size == (1000, 750), size
    assert saved() == (1000, 750), saved()

    # Corrupt configuration warns and falls back to the adaptive tier.
    screen = display_size(environment)
    expected = adaptive_size(*screen)
    config.write_text('{"version":1,"width":0,"height":0}')
    result, size = run("corrupt")
    assert "Cannot read geometry" in result.stdout + result.stderr, result.stdout
    assert size == expected, (size, expected, screen)
    assert saved() == expected, saved()

    # Remembered geometry larger than the current display also falls back.
    config.write_text(json.dumps({"version": 1, "width": 2400, "height": 1800, "maximized": False}))
    _, size = run("too-large")
    assert size == expected, (size, expected, screen)
    assert saved() == expected, saved()

    # --width/--height must be paired.
    result = subprocess.run([str(args.binary.resolve()), "--content", str(out), "--window-config", str(config),
                             "--width", "900", "--frames", "1"], env=environment, capture_output=True, text=True, timeout=60)
    assert result.returncode != 0 and "must be used together" in result.stdout + result.stderr, result.stdout
    print(f"Window geometry smoke PASS: adaptive={expected[0]}x{expected[1]}, explicit save/restore and corrupt fallback")


if __name__ == "__main__":
    main()
