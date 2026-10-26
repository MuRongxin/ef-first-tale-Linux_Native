#!/usr/bin/env python3
"""Render original-coordinate message layouts with entirely synthetic bitmap fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image
from render_smoke import ROOT, importer
import sys
sys.path.insert(0,str(ROOT/"tools"))
from script_json import write_fixture_scripts


def prepare_fixture(out):
    (out / "scripts").mkdir(parents=True, exist_ok=True)
    font, face = importer.find_font()
    shutil.copyfile(font, out / "font.ttc")
    shutil.copyfile(ROOT / "examples/script.scene.json", out / "view.json")
    shutil.copyfile(ROOT / "examples/message.ui.json", out / "message.ui.json")
    manifest = {"version": 1, "design_size": [800, 600], "development": True, "entry_scene": "view.json",
                "resources": "catalog.json", "font": {"path": "font.ttc", "face": face}, "images": {}}
    catalog = {"version": 1, "entries": {}}
    fixtures = {
        "bg/background.png": Image.new("RGBA", (800, 600), (40, 80, 120, 255)),
        "sys/msgPanel.png": Image.new("RGBA", (800, 247), (30, 40, 100, 153)),
        "sys/fullPanel.png": Image.new("RGBA", (800, 600), (20, 20, 100, 102)),
        "sys/BLACK800x75.png": Image.new("RGBA", (800, 75), (0, 0, 0, 255)),
        "sys/pauseAnime.png": Image.new("RGBA", (264, 22)),
    }
    for frame in range(12):
        color = (255, 0, 0, 255) if frame % 2 else (0, 255, 0, 255)
        fixtures["sys/pauseAnime.png"].paste(color, (frame*22, 0, (frame+1)*22, 22))
    for identifier, image in fixtures.items():
        filename = identifier.replace("/", "-")
        image.save(out / filename)
        digest = hashlib.sha256((out / filename).read_bytes()).hexdigest()
        manifest["images"][identifier] = {"design_size": list(image.size), "source_sha256": digest,
            "variants": {"1": {"path": filename, "pixels": list(image.size)}}}
        tag, name = identifier.split("/", 1)
        catalog["entries"][identifier] = {"kind": "image", "name": name, "tag": tag, "source_sha256": digest}
    (out / "manifest.json").write_text(json.dumps(manifest))
    (out / "catalog.json").write_text(json.dumps(catalog))
    scripts = {
        "normal.sc": ".stage * background.png 0 0\n.panel 1\n.message 1  纮 普通消息窗，姓名占一行。\n.end\n",
        "long.sc": ".stage * background.png 0 0\n.panel 1\n.message 1   " + "长"*135 + "\n.end\n",
        "cinema.sc": ".stage * background.png 0 0\n.panel 3\n.message 1  #优子 电影字幕模式，标点保持稳定。\\n\n.end\n",
    }
    for name, text in scripts.items():
        (out / name).write_text(text)
    index = {"version": 1, "encoding": "utf-8", "view": "view.json", "message_ui": "message.ui.json",
             "scripts": {"scr/"+name: {"path": name} for name in scripts}}
    (out / "scripts/index.json").write_text(json.dumps(index))
    write_fixture_scripts(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dev/ef_native")
    args = parser.parse_args()
    out = ROOT / "build/message-ui-smoke"
    prepare_fixture(out)
    environment = dict(os.environ, SDL_VIDEO_DRIVER="x11", LIBGL_ALWAYS_SOFTWARE="1")
    for name in ("normal", "cinema", "long"):
        capture = out / (name + ".ppm")
        result = subprocess.run([str(args.binary), "--content", str(out), "--script", "scr/"+name+".sc", "--silent",
                                 "--text-speed", "0", "--frames", "100", "--screenshot", str(capture),
                                 "--window-config", str(out/"window.json"), "--width", "1000", "--height", "750"],
                                env=environment, capture_output=True, timeout=20)
        (out / (name + ".log")).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise AssertionError(result.stderr.decode(errors="replace"))
        with Image.open(capture) as screenshot:
            image = screenshot.convert("RGB")
            assert image.getpixel((500, 300)) == (40, 80, 120)
            if name == "normal":
                actual = image.getpixel((900, 700))
                assert all(abs(a-b) <= 1 for a, b in zip(actual, (34, 56, 108))), actual
                regions = [(45, 570, 400, 610), (45, 610, 900, 660)]
            elif name == "long":
                assert image.getpixel((900, 510)) != (40, 80, 120), "long message panel did not rise"
                regions = [(45, 700, 400, 745)]
            else:
                assert image.getpixel((500, 20)) == image.getpixel((900, 700)) == (0, 0, 0)
                assert image.getpixel((50, 680)) == (0, 0, 0)  # # speaker names are not printed
                regions = [(150, 660, 950, 710)]
            for region in regions:
                pixels = image.crop(region).tobytes()
                assert sum(min(pixels[i:i+3]) > 180 for i in range(0, len(pixels), 3)) > 100, (name, region)
            # A cropped frame is one solid atlas cell, not twelve miniature cells.
            strip = image.crop((0, 560, 1000, 750)).tobytes()
            colored = [i for i in range(0, len(strip), 3)
                       if (strip[i] > 245 and strip[i+1] < 5) or (strip[i+1] > 245 and strip[i] < 5)]
            assert len(colored) > 400, (name, len(colored))
            if name == "cinema":
                assert colored[0] // (1000*3) + 560 >= 715, "advance marker ignored trailing newline"
    print("Message UI smoke PASS: normal/cinema geometry, expandable long message, original alpha, CJK names/body, hidden speaker and sprite atlas cropping")


if __name__ == "__main__":
    main()
