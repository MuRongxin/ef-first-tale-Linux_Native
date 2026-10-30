import json
import csv
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from ani import decode, export


def record(name, w=1, h=1, pixels=b'\x01\x02\x03\x80'):
    return name.encode('cp932') + b'\0' + struct.pack('<5H', w, h, 32, 7, 9) + pixels


class AniTests(unittest.TestCase):
    def test_all_counts(self):
        for n in (2, 3, 4, 6, 8, 10, 12, 14):
            data = struct.pack('<HHI', 256, n, 0) + record('景 中目') * n
            patches = decode(data)
            self.assertEqual(len(patches), n)
            self.assertEqual((patches[0].x, patches[0].y), (7, 9))

    def test_control_and_export(self):
        from PIL import Image
        data = struct.pack('<HHI', 256, 2, 0) + record('景 開き口') + record('TEL @;*:13;B:13;~', 0, 0, b'')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'sample.ani'
            source.write_bytes(data)
            out = Path(directory) / 'export'
            result = export(source, out)
            with Image.open(out / '000.png') as image:
                self.assertEqual(image.getpixel((0, 0)), (3, 2, 1, 128))
            self.assertNotIn('path', result['records'][1])
            self.assertEqual(json.loads((out / 'anim.json').read_text()), result)
            with self.assertRaises(ValueError):
                export(source, out)

    def test_pair_audit(self):
        from inspect_animations import inspect
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'assets/raw/st').mkdir(parents=True)
            (root / 'assets/manifest').mkdir(parents=True)
            data = struct.pack('<HHI', 256, 1, 0) + record('景 開き口')
            (root / 'assets/raw/st/actor.ani').write_bytes(data)
            rows = [dict(rel_path='st/actor.ani', kind='ani', sha256=hashlib.sha256(data).hexdigest(), w='', h=''),
                    dict(rel_path='st/actor.png', kind='png', sha256='', w=8, h=10)]
            def write():
                with (root / 'assets/manifest/images.csv').open('w', newline='') as file:
                    writer = csv.DictWriter(file, fieldnames=list(rows[0]))
                    writer.writeheader()
                    writer.writerows(rows)
            write()
            report = inspect(root)
            self.assertFalse(report['failures'])
            self.assertEqual(report['entries'][0]['image'], 'st/actor.png')
            self.assertIn('景', report['entries'][0]['groups'])
            rows[1]['w'] = 7
            write()
            self.assertEqual(inspect(root)['failures'][0]['error'], 'patch outside canvas')
            rows.pop()
            write()
            self.assertEqual(inspect(root)['failures'][0]['error'], 'missing/ambiguous pair')
            (root / 'assets/raw/st/actor.ani').write_bytes(data + b'x')
            with self.assertRaises(ValueError):
                inspect(root)

    def test_content_import(self):
        from import_animations import import_animations
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'assets/raw/st').mkdir(parents=True)
            (root/'assets/manifest').mkdir(parents=True)
            out = root/'content'
            out.mkdir()
            data = struct.pack('<HHI',256,1,0) + record('景 中目')
            (root/'assets/raw/st/actor.ani').write_bytes(data)
            rows = [dict(rel_path='st/actor.ani',kind='ani',sha256=hashlib.sha256(data).hexdigest()),
                    dict(rel_path='st/actor.png',kind='png',sha256='a'*64)]
            with (root/'assets/manifest/images.csv').open('w',newline='') as file:
                writer=csv.DictWriter(file,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
            manifest=dict(images={'st/actor.png':dict(source_sha256='a'*64,design_size=[8,10])})
            catalog=import_animations(root,out,manifest)
            self.assertEqual(catalog['source_scale'],1)
            patch=catalog['images']['st/actor.png']['records'][0]
            self.assertEqual(patch['offset'],[7,9])
            with Image.open(out/patch['path']) as image:
                self.assertEqual(image.getpixel((0,0)),(3,2,1,128))
            self.assertEqual(import_animations(root,out,manifest),catalog)
            manifest['images']['st/actor.png']['source_sha256']='b'*64
            with self.assertRaises(ValueError):
                import_animations(root,out,manifest)

    def test_reject_corruption(self):
        valid = struct.pack('<HHI', 256, 1, 0) + record('優子 中口')
        for data in (b'', valid[:-1], valid + b'\0', b'\0' * 8,
                     struct.pack('<HHI', 256, 1, 1) + valid[8:],
                     struct.pack('<HHI', 256, 1, 0) + b'x' * 1025):
            with self.assertRaises(ValueError):
                decode(data)


if __name__ == '__main__':
    unittest.main()
