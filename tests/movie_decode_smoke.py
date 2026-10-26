#!/usr/bin/env python3
"""Generate a tiny H.264/AAC fixture and exercise the FFmpeg movie decoder."""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("SKIP: ffmpeg not available; movie decoder smoke test not run")
        return 0
    with tempfile.TemporaryDirectory() as directory:
        fixture = Path(directory) / "fixture.mkv"
        subprocess.run([
            ffmpeg, "-v", "error",
            "-f", "lavfi", "-i", "testsrc=size=160x120:rate=24:duration=0.5",
            "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=0.5",
            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac",
            "-shortest", "-y", str(fixture),
        ], check=True)
        subprocess.run([args.binary, str(fixture)], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
