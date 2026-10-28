"""Strict image refresh, runtime thumbnail recipes and deterministic control maps."""
import csv
import hashlib
import json
from pathlib import Path
import sys
import unittest
from PIL import Image
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import prepare_content as importer
from prepare_procedural_hd import generate
from refresh_content_images import refresh, thumbnail_sources
import test_prepare_content as fixture


class HDReplacementTests(unittest.TestCase):
    setUp=fixture.ContentImportTests.setUp
    tearDown=fixture.ContentImportTests.tearDown
    run_import=fixture.ContentImportTests.run_import

    def hd(self,scale):
        p=self.project/f'assets/hd/bg_{scale}x/test.png-.png'
        Image.new('RGBA',(2*scale,scale),(30,70,110,128)).save(p)

    def rows(self):
        with (self.project/'assets/manifest/images.csv').open(newline='') as f:return list(csv.DictReader(f))

    def test_refresh_removes_raw_references_and_preserves_unrelated_manifest_fields(self):
        self.run_import();path=self.out/'manifest.json'
        before=json.loads(path.read_text());before['custom_ui']='keep-me';path.write_text(json.dumps(before))
        self.hd(4);self.hd(8)
        result=refresh(self.project,self.out,{},1)
        self.assertFalse(result['development'])
        self.assertEqual(result['custom_ui'],'keep-me')
        self.assertEqual(result['font'],before['font'])
        self.assertEqual(result['entry_scene'],before['entry_scene'])
        self.assertEqual(set(result['images']['bg/test.png-']['variants']),{'4','8'})
        self.assertEqual(json.loads((self.project/'work/hd-replacement/manifest.before.json').read_text()),before)

    def test_missing_8x_never_publishes_partial_refresh(self):
        self.run_import();before=(self.out/'manifest.json').read_bytes();self.hd(4)
        with self.assertRaisesRegex(ValueError,'8×'):
            refresh(self.project,self.out,{},1)
        self.assertEqual((self.out/'manifest.json').read_bytes(),before)

    def test_thumbnail_sources_require_exact_unambiguous_source_or_checked_crop(self):
        source={**self.rows()[0],'method':'upscale_art'}
        thumb={**source,'rel_path':'sys/thumb/test.png-','method':'runtime_derive_from_hd','w':'1','h':'1'}
        plan=thumbnail_sources([source,thumb],{})
        self.assertEqual(plan[thumb['rel_path']],{'asset':'bg/test.png-','crop':[0,0,2,1]})
        with self.assertRaises(ValueError):thumbnail_sources([thumb],{})
        with self.assertRaises(ValueError):thumbnail_sources([source,thumb,{**source,'rel_path':'st/test.png-','sha256':'different'}],{})
        with self.assertRaises(ValueError):thumbnail_sources([source,thumb],{thumb['rel_path']:{'asset':'bg/test.png-','crop':[1,0,2,1]}})
        self.assertEqual(thumbnail_sources([source,thumb],{thumb['rel_path']:{'asset':'bg/test.png-','crop':[1,0,1,1]}})[thumb['rel_path']]['crop'],[1,0,1,1])

    def test_control_map_preserves_every_original_sample_and_alpha(self):
        row={**self.rows()[0],'method':'procedural_regen','category':'tint'}
        src=self.project/'assets/raw'/row['rel_path']
        im=Image.new('RGBA',(2,1));im.putdata([(3,40,80,0),(90,140,220,128)]);im.save(src,'PNG')
        row['sha256']=hashlib.sha256(src.read_bytes()).hexdigest();before=src.read_bytes()
        result=generate(self.project,row)
        for scale,output in zip((4,8),result['outputs']):
            with Image.open(self.project/output['path']) as hd:
                self.assertEqual(hd.size,(2*scale,scale))
                self.assertEqual(hd.getpixel((0,0)),(3,40,80,0))
                self.assertEqual(hd.getpixel((scale,0)),(90,140,220,128))
        self.assertEqual(src.read_bytes(),before)
        row['method']='upscale_art'
        with self.assertRaises(ValueError):generate(self.project,row)


if __name__=='__main__':unittest.main()
