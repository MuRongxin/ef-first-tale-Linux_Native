"""Native route traversal with synthetic JSON scripts; no proprietary content."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from script_json import compile_script, encode
BINARY = Path(sys.argv.pop(1))


class Routes(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='route-audit-', dir=ROOT/'build')
        self.root = Path(self.temp.name)
        self.scripts = {
            'scr/start.sc': '.set route = 0\n.select 左:left 右:right\n.label left\n.set route = 1\n.chain common.sc\n.label right\n.set route = 2\n.chain common.sc\n',
            'scr/common.sc': '.if route == 2 right\n.setGlobal LEFT = 1\n.message 1   左线\n.end\n.label right\n.setGlobal RIGHT = 1\n.message 2   右线\n.end\n',
        }

    def tearDown(self):
        self.temp.cleanup()

    def run_audit(self, *flags):
        index = {'version': 2, 'format': 'ef.script.v1', 'encoding': 'utf-8', 'scripts': {}}
        for n, (id, source) in enumerate(self.scripts.items()):
            path = f'{n}.script.json'
            (self.root/path).write_bytes(encode(compile_script(source, id)))
            index['scripts'][id] = {'path': path}
        (self.root/'index.json').write_bytes(encode(index))
        manifest = {'version': 1, 'design_size': [800, 600], 'entry_scene': 'unused.json',
                    'font': {'path': 'unused.ttf'}, 'images': {}, 'script_index': 'index.json'}
        if any('.movie' in source for source in self.scripts.values()):
            (self.root/'sample.mpg').write_bytes(b'audit completion fixture; intentionally not decoded')
            (self.root/'catalog.json').write_bytes(encode({'version': 1, 'entries': {'mov/sample.mpg': {
                'kind': 'video', 'name': 'sample.mpg', 'tag': 'mov', 'source_sha256': 'a'*64, 'path': 'sample.mpg'}}}))
            manifest['resources'] = 'catalog.json'
        (self.root/'manifest.json').write_bytes(encode(manifest))
        result = subprocess.run([str(BINARY), '--content', str(self.root), '--entry', 'scr/start.sc',
                                 '--report', str(self.root/'report.json'), *flags], capture_output=True, text=True)
        self.assertIn(result.returncode, (0, 2, 3), result.stderr)
        return result.returncode, json.loads((self.root/'report.json').read_text())

    def test_branch_snapshots_preserve_independent_variables_and_all_edges(self):
        for mode in ('control', 'presentation'):
            code, report = self.run_audit('--mode', mode)
            self.assertEqual(code, 0)
            self.assertEqual(len(report['paths']), 2)
            self.assertEqual([p['state']['locals']['route'] for p in report['paths']], [1, 2])
            self.assertEqual(report['paths'][0]['state']['globals'], {'LEFT': 1})
            self.assertEqual(report['paths'][1]['state']['globals'], {'RIGHT': 1})
            self.assertEqual([p['messages'] for p in report['paths']], [1, 1])
            self.assertTrue(any('taken:right' in e for e in report['edges']))
            self.assertTrue(any('fallthrough' in e for e in report['edges']))

    def test_presentation_failures_are_never_control_flow_passes(self):
        self.scripts['scr/common.sc'] = '.effect Rain\n.end\n'
        self.assertEqual(self.run_audit('--mode', 'control')[0], 0)
        code, report = self.run_audit('--mode', 'presentation')
        self.assertEqual(code, 2)
        self.assertEqual([p['status'] for p in report['paths']], ['blocked', 'blocked'])
        self.assertTrue(all('scr/common.sc:1 .effect' in p['error'] for p in report['paths']))

    def test_movie_completion_is_explicit_and_does_not_claim_decoding(self):
        self.scripts['scr/start.sc'] = '.movie 0 sample.mpg 800 600 f\n.end\n'
        code, report = self.run_audit('--mode', 'presentation')
        self.assertEqual(code, 2)
        self.assertIn('FFmpeg', report['paths'][0]['error'])
        code, report = self.run_audit('--mode', 'presentation', '--complete-movies')
        self.assertEqual(code, 0)
        self.assertEqual(report['paths'][0]['movies'][0]['completion'], 'simulated')
        self.assertFalse(report['paths'][0]['movies'][0]['skippable'])
        self.assertIn('no GPU/image pixels, UI or movie decoding', report['scope'])

    def test_limits_report_incomplete(self):
        code, report = self.run_audit('--max-paths', '1')
        self.assertEqual((code, report['verdict'], report['pending_states']), (3, 'INCOMPLETE', 1))
        self.scripts['scr/start.sc'] = '.label loop\n.wait 1\n.goto loop\n'
        code, report = self.run_audit('--max-updates', '3')
        self.assertEqual(code, 3)
        self.assertEqual(report['paths'][0]['status'], 'budget')

    def test_patch_overlay_is_explicit_and_reported(self):
        self.scripts['r18_scr/common.sc'] = '.setGlobal PATCH = 1\n.end\n'
        self.assertTrue(all('PATCH' not in p['state']['globals'] for p in self.run_audit()[1]['paths']))
        code, report = self.run_audit('--overlay-patches')
        self.assertEqual(code, 0)
        self.assertTrue(report['overlay_patches'])
        self.assertTrue(all(p['state']['globals'] == {'PATCH': 1} for p in report['paths']))


if __name__ == '__main__':
    unittest.main()
