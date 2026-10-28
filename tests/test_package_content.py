"""Package boundary, reference coverage and overlay tests using generated data only."""
from pathlib import Path
import csv
import hashlib
import json
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from content_resources import ResourceIndex, audit, references
from package_content import import_videos, package, store_bytes
from prepare_content import import_image
import test_prepare_content as preview_tests
from PIL import Image


def entry(tag, name, digest="same", kind="image", **extra):
    return {"kind": kind, "tag": tag, "name": name, "source_sha256": digest, **extra}


class ReferenceTests(unittest.TestCase):
    def test_overlay_same_content_and_native_priority(self):
        entries = {"bg/Test.png": entry("bg", "Test.png"), "st/test.png": entry("st", "test.png"),
                   "sys/button.png-": entry("sys", "button.png-"),
                   "r18_sys/button.png": entry("r18_sys", "button.png", "patch"),
                   "sys/icon.png": entry("sys", "icon.png", "normal"),
                   "sys/icon.png-": entry("sys", "icon.png-", "variant"),
                   "r18_bg/only.png": entry("r18_bg", "only.png")}
        index = ResourceIndex(entries)
        self.assertEqual(index.resolve("image", "TEST.PNG")["id"], "bg/Test.png")
        self.assertEqual(index.resolve("image", "button.png")["id"], "sys/button.png-")
        self.assertEqual(index.resolve("image", "button.png.tmp", True)["id"], "r18_sys/button.png")
        self.assertEqual(index.resolve("image", "only.png")["status"], "missing")
        self.assertEqual(index.resolve("image", "icon.png")["id"], "sys/icon.png")
        self.assertEqual(index.resolve("image", "icon.png-")["id"], "sys/icon.png-")
        self.assertEqual(index.resolve("image", "icon.png.tmp")["status"], "ambiguous")
        entries["st/test.png"]["source_sha256"] = "different"
        self.assertEqual(index.resolve("image", "test.png")["id"], "st/test.png")
        for bad in ("../test.png", "/test.png", "a//b", "a\0b"):
            with self.assertRaises(ValueError):
                index.resolve("image", bad)

    def test_voice_empty_fields_variants_and_no_dialogue_false_positives(self):
        text = ('[e].message 10 voice-key #名 对话里提到fake.png与音乐.ogg\\n\n'
                '.message 20   旁白\n[j].stage * TEST.PNG 0 0\n'
                '.transition 0 MASK.png 10\n.playSE *\n#include common.sc\n'
                '; comment.png\n.chain next.sc\n')
        refs = list(references(text))
        self.assertEqual([(x["kind"], x["name"]) for x in refs],
                         [("voice", "voice-key"), ("image", "TEST.PNG"), ("image", "MASK.png"),
                          ("script", "common.sc"), ("script", "next.sc")])
        self.assertEqual(refs[1]["line"], 3)
        entries = {"voice/voice-key": entry("voice", "voice-key", kind="voice")}
        result = audit(entries, {"scr/a.sc": text})
        self.assertEqual(result["references"], 5)
        self.assertEqual(result["missing"], 4)


class PackageTests(unittest.TestCase):
    setUp = preview_tests.ContentImportTests.setUp
    tearDown = preview_tests.ContentImportTests.tearDown
    run_import = preview_tests.ContentImportTests.run_import

    def test_video_discovery_without_private_hd_manifest(self):
        self.out.mkdir()
        video = self.project / 'assets/hd/video'
        video.mkdir()
        encoded = b'video transport fixture'
        (video / 'opening.mkv').write_bytes(encoded)
        (self.project / 'assets/manifest/videos.json').write_text(json.dumps({
            'entries': [{'name': 'opening.avi', 'sha256': 'a'*64}]}))
        probe = {'streams': [{'codec_type': 'video', 'codec_name': 'h264', 'width': 3840, 'height': 2880, 'avg_frame_rate': '24/1'}],
                 'format': {'duration': '4.0'}}
        with patch('package_content.shutil.which', return_value='/usr/bin/ffprobe'), patch('package_content.subprocess.run') as run:
            run.return_value.stdout = json.dumps(probe)
            entries, warnings = import_videos(self.project, self.out)
            item = entries['mov/opening.avi']
            self.assertEqual(item['fps'], 24)
            self.assertEqual((self.out / item['path']).read_bytes(), encoded)
            self.assertFalse(warnings)
            probe['streams'][0]['codec_name'] = 'hevc'
            run.return_value.stdout = json.dumps(probe)
            with self.assertRaisesRegex(ValueError, 'H.264'):
                import_videos(self.project, self.out)
    def test_png_hd_and_webp_limit_fallback(self):
        raw = self.project / "assets/raw/bg/wide.png"
        Image.new("RGBA", (5000, 1), (1, 2, 3, 128)).save(raw)
        for scale in (4, 8):
            Image.new("RGBA", (20000, 4), (4, 5, 6, 128)).save(self.project / f"assets/hd/bg_{scale}x/wide.png")
        self.out.mkdir()
        row = {"rel_path": "bg/wide.png", "kind": "png", "w": 5000, "h": 1, "sha256": hashlib.sha256(raw.read_bytes()).hexdigest()}
        item = import_image(self.project, self.out, row)
        self.assertEqual(item["variants"]["8"]["pixels"], [20000, 4])
        self.assertTrue(item["variants"]["4"]["path"].endswith('.png'))
        self.assertEqual(item["variants"]["4"]["path"], item["variants"]["8"]["path"])

    def test_atomic_package_and_immutable_files(self):
        inventory = self.project / 'assets/manifest/images.csv'
        with inventory.open(newline='') as file:
            reader = csv.DictReader(file)
            columns, rows = reader.fieldnames, list(reader)
        (self.project / 'assets/raw/st').mkdir()
        (self.project / 'assets/raw/st/test.png').write_bytes((self.project / 'assets/raw/bg/test.png-').read_bytes())
        with inventory.open('w', newline='') as file:
            writer = csv.DictWriter(file, fieldnames=columns)
            writer.writeheader()
            writer.writerows([*rows, {**rows[0], 'rel_path': 'st/test.png'}])
        self.out.mkdir()
        data = b'.message 1 voice-key  text\n.stage * TEST.PNG 0 0\n'
        script_path = store_bytes(self.out, data, '.sc')
        index = {"version": 1, "encoding": "utf-8", "view": "view.json", "scripts": {
            "scr/a.sc": {"path": script_path, "source_sha256": hashlib.sha256(data).hexdigest()}}}
        voice_data = b'RIFF fixture (only transport tested here)'
        voice_path = store_bytes(self.out, voice_data, '.wav', 'audio')
        sound = {"voice/voice-key": entry("voice", "voice-key", hashlib.sha256(voice_data).hexdigest(),
                                          "voice", path=voice_path, format="wav")}
        with patch('package_content.import_scripts', return_value=index), patch('package_content.import_audio', return_value=(sound, {})):
            manifest = package(self.project, self.project / 'game', self.out, self.font, 0)
            report = json.loads((self.out / manifest['import_report']).read_text())
            self.assertEqual(report['missing'], 0)
            self.assertEqual(report['raw_only_categories'], {'unknown': 2})
            self.assertEqual(manifest['images']['bg/test.png-']['variants']['1']['path'],
                             manifest['images']['st/test.png']['variants']['1']['path'])
            self.assertEqual((self.out / voice_path).read_bytes(), voice_data)
            before = (self.out / 'manifest.json').read_bytes()
            (self.project / 'assets/hd/bg_4x/test.png-.webp').write_bytes(b'partial upload')
            with self.assertRaises(OSError):
                package(self.project, self.project / 'game', self.out, self.font, 0)
            self.assertEqual((self.out / 'manifest.json').read_bytes(), before)
            with self.assertRaisesRegex(ValueError, '全量资源包'):
                self.run_import()


if __name__ == '__main__':
    unittest.main()
