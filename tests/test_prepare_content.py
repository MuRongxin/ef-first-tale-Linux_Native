#!/usr/bin/env python3
"""Exercise the content importer with synthetic assets, never game data."""
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_content", ROOT / "tools/prepare_content.py")
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)
sys.path.insert(0, str(ROOT / "tools"))
from import_scripts import transcode


class ContentImportTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "build")
        self.project = Path(self.temp.name)
        self.out = self.project / "content"
        for directory in ("assets/raw/bg", "assets/hd/bg_4x", "assets/hd/bg_8x", "assets/manifest", "examples"):
            (self.project / directory).mkdir(parents=True)
        image = self.project / "assets/raw/bg/test.png-"
        Image.new("RGBA", (2, 1), (1, 2, 3, 128)).save(image, "PNG")
        self.sha = hashlib.sha256(image.read_bytes()).hexdigest()
        with (self.project / "assets/manifest/images.csv").open("w", newline="") as file:
            writer = csv.DictWriter(file, fieldnames=["rel_path", "kind", "w", "h", "sha256"])
            writer.writeheader()
            writer.writerow(dict(rel_path="bg/test.png-", kind="png", w=2, h=1, sha256=self.sha))
        (self.project / "examples/preview.scene.json").write_text((ROOT / "examples/preview.scene.json").read_text())
        self.font = self.project / "test.ttf"
        self.font.write_bytes(b"font fixture (runtime font validation is separate)")

    def tearDown(self):
        self.temp.cleanup()

    def run_import(self, **kwargs):
        return importer.prepare(self.project, self.out, ["bg/test.png-"], self.font, 0, **kwargs)

    def test_raw_fallback_and_immutable_copy(self):
        manifest = self.run_import()
        item = manifest["images"]["bg/test.png-"]
        self.assertEqual(item["design_size"], [2, 1])
        self.assertEqual(list(item["variants"]), ["1"])
        self.assertEqual(hashlib.sha256((self.out / item["variants"]["1"]["path"]).read_bytes()).hexdigest(), self.sha)
        self.assertFalse((self.out / item["variants"]["1"]["path"]).is_symlink())
        self.assertEqual(json.loads((self.out / manifest["entry_scene"]).read_text())["layers"][0]["asset"], "bg/test.png-")

    def test_hd_and_failed_update_preserve_manifest(self):
        for scale in (4, 8):
            Image.new("RGBA", (2*scale, scale), (7, 8, 9, 128)).save(self.project / f"assets/hd/bg_{scale}x/test.png-.webp", lossless=True)
        manifest = self.run_import(hd_only=True)
        self.assertEqual(list(manifest["images"]["bg/test.png-"]["variants"]), ["4", "8"])
        before = (self.out / "manifest.json").read_bytes()
        (self.project / "assets/hd/bg_8x/test.png-.webp").write_bytes(b"incomplete upload")
        with self.assertRaises(OSError):
            self.run_import()
        self.assertEqual((self.out / "manifest.json").read_bytes(), before)

    def test_raw_hash_and_path_confinement(self):
        (self.project / "assets/raw/bg/test.png-").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            self.run_import()
        for name in ("../escape", "/tmp/escape", "a\\b"):
            with self.assertRaises(ValueError):
                importer.safe_path(self.project, name)
        with self.assertRaises(ValueError):
            importer.prepare(self.project, self.project / "assets/raw", [], self.font, 0)

    def test_missing_hd_and_names(self):
        with self.assertRaises(ValueError):
            self.run_import(hd_only=True)
        self.assertEqual(importer.hd_name("thumb/a.png"), "thumb/a.webp")
        for name in ("thumb/a.png-", "thumb/a.png.tmp"):
            self.assertEqual(importer.hd_name(name), name + ".webp")

    def test_rejects_wrong_codec_despite_matching_extension_and_size(self):
        self.run_import()
        before = (self.out / "manifest.json").read_bytes()
        Image.new("RGB", (8, 4)).save(self.project / "assets/hd/bg_4x/test.png-.webp", "JPEG")
        with self.assertRaisesRegex(ValueError, "PNG/WebP"):
            self.run_import()
        self.assertEqual((self.out / "manifest.json").read_bytes(), before)

    def test_distinct_archive_names_keep_their_pixels_and_alpha(self):
        sys.path.insert(0, str(ROOT / 'tools/upscale/remote'))
        import ef_client
        import fix_large
        self.out.mkdir()
        paths = []
        for name, color in [('icon.png', (255, 0, 0, 128)),
                            ('icon.png-', (0, 255, 0, 255)),
                            ('icon.png.tmp', (0, 0, 255, 64))]:
            rel = 'bg/' + name
            for scale in (4, 8):
                path = ef_client.out_path(self.project / 'assets/hd', rel, scale)
                self.assertEqual(path.name, importer.hd_name(name))
                self.assertEqual(path, fix_large.out_path(self.project / 'assets/hd', rel, scale, '.webp'))
                Image.new('RGBA', (2 * scale, scale), color).save(path, lossless=True)
            row = dict(rel_path=rel, kind='png', w=2, h=1, sha256='unused in hd_only')
            item = importer.import_image(self.project, self.out, row, hd_only=True)
            paths.append(item['variants']['4']['path'])
            for variant in item['variants'].values():
                with Image.open(self.out / variant['path']) as im:
                    self.assertEqual(im.convert('RGBA').getpixel((0, 0)), color)
        self.assertEqual(len(set(paths)), 3)

    def test_remote_resume_preserves_true_8x_png_and_repairs_only_missing_4x(self):
        import io
        from types import SimpleNamespace
        from unittest.mock import patch
        sys.path.insert(0, str(ROOT / 'tools/upscale/remote'))
        import ef_client
        task = ef_client.Task(self.project, dict(rel_path='bg/wide.png', w=2050, h=1,
                              aliases=[dict(rel_path='st/wide.png')]))
        Image.new('RGB', (2050, 1), 'red').save(task.src)
        out = self.project / 'assets/hd'
        p4 = ef_client.out_path(out, task.rel_path, 4)
        p8 = ef_client.out_path(out, task.rel_path, 8, '.png')
        Image.new('RGB', (8200, 4), 'red').save(p4, lossless=True)
        Image.new('RGB', (16400, 8), 'red').save(p8)
        before = p8.read_bytes()
        cfg = SimpleNamespace(out_root=out, scale=8, want4=True, force=False,
                              api='unused', token='', timeout=1, retries=1, api_ip='')
        with patch.object(ef_client, 'request_upscale', side_effect=AssertionError('unneeded GPU request')):
            self.assertTrue(ef_client.process_task(task, cfg)['noop'])
        alias = ef_client.out_path(out, 'st/wide.png', 8, '.png')
        self.assertEqual(alias.read_bytes(), before)
        p4.unlink()
        buf = io.BytesIO()
        Image.new('RGB', (8200, 4), 'red').save(buf, 'WEBP', lossless=True)
        result = dict(ok=True, parts={4: buf.getvalue()}, upload_mb=0, download_mb=0,
                      total_s=0, timings={})
        with patch.object(ef_client, 'request_upscale', return_value=result) as request:
            self.assertTrue(ef_client.process_task(task, cfg)['ok'])
            self.assertEqual(request.call_args.args[4], '4')
        self.assertEqual(p8.read_bytes(), before)
        self.assertFalse(ef_client.out_path(out, task.rel_path, 8).exists())

    def test_script_encoding_before_escapes(self):
        # 0x81 0x5C is one GBK character, not a literal backslash.
        original = b'.message 100   ' + bytes([0x81, 0x5c]) + b'n\\n' + '中文'.encode('gbk') + b'\r\n'
        converted = transcode(original)
        self.assertEqual(converted.decode('utf-8').encode('gbk'), original)
        self.assertEqual(converted.decode('utf-8').count('\\n'), 1)
        with self.assertRaises(UnicodeDecodeError):
            transcode(b'\x81')


if __name__ == "__main__":
    unittest.main()
