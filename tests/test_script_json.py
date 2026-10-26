"""Compiler, format and atomic publication tests with synthetic scripts only."""
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from script_json import compile_script, encode, dialogue_events
from migrate_scripts import migrate, restore
from content_resources import references

VERIFIER = Path(sys.argv.pop(1)) if len(sys.argv)>1 and not sys.argv[1].startswith('-') else ROOT/'build/dev/ef_script_verify'


class CompilerTests(unittest.TestCase):
    def test_unicode_empty_fields_literal_slash_and_provenance(self):
        source = '\ufeff[e]:.message 17  #名字 中文\\\\w\\n尾部  \r\n[j]；备注\r\n.unknown 参数\r\n'
        d = compile_script(source, 'scr/a.sc')
        n = d['instructions']
        self.assertEqual(len(n), 2)
        self.assertEqual((n[0]['source_line'], n[0]['variant'], n[0]['colon_variant']), (1, 'e', True))
        self.assertEqual(n[0]['params'], {'id': 17, 'voice': '', 'speaker': '#名字',
                                          'events': [{'type': 'text', 'value': '中文\\w\n尾部  '}]})
        self.assertEqual(n[1]['op'], 'unsupported')
        self.assertEqual(n[1]['params']['raw'], '.unknown 参数')
        self.assertEqual(n[1]['source_line'], 3)
        with self.assertRaises(ValueError):
            dialogue_events('尾部\\')

    def test_structured_stage_movie_and_resource_coverage(self):
        source = '.stage vista.png 0 0 bg.png 0 75 actor.png 600,75\n.movie 0 opening.mpg 800 600 f 10 20\n.effect Snow\n'
        d = compile_script(source, 'scr/a.sc')
        self.assertEqual(d['instructions'][0]['params']['actors'][0]['bottom_trim'], 75)
        self.assertFalse(d['instructions'][1]['params']['skippable'])
        self.assertEqual(list(references(source)), list(references(d)))
        self.assertEqual(len(list(references(d))), 7)
        self.assertEqual(compile_script('.playBGM * * 0\n', 'scr/a.sc')['instructions'][0]['params']['fade_in'], 2)
        self.assertEqual(compile_script('.playVoice * * 0\n', 'scr/a.sc')['instructions'][0]['op'], 'unsupported')

    def test_schema_is_reproducible(self):
        subprocess.run([sys.executable, str(ROOT/'tools/generate_script_schema.py'), '--check'], check=True)

    def test_atomic_migration_failure_backup_idempotence_and_restore(self):
        (ROOT/'work').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='json-backup-test-', dir=ROOT/'work') as backup_dir, tempfile.TemporaryDirectory(prefix='json-migration-', dir=ROOT/'build') as temporary:
            root = Path(temporary)
            (root/'blobs').mkdir()
            sources = {'scr/test.sc': '#include common.sc\n.set n = 2\n[e].wait 1\n[j].wait 2\n.stage * bg.png 0 0 actor.png 600,75\n.message 10   你好\\w世界\\n字面\\\\v\n.select 继续:left 结束:right\n.label left\n.chain second.sc\n.label right\n.end\n',
                       'scr/common.sc': '.setGlobal flag = 1\n',
                       'scr/second.sc': '.movie 0 opening.mpg 800 600 t\n.frame *\n.message 20   完成\\a\n.end\n'}
            index = {'version': 1, 'encoding': 'utf-8', 'scripts': {}}
            for n, (identifier, source) in enumerate(sources.items()):
                path = f'blobs/{n}.sc'
                (root/path).write_text(source)
                index['scripts'][identifier] = {'path': path, 'utf8_sha256': hashlib.sha256(source.encode()).hexdigest()}
            (root/'legacy.json').write_bytes(encode(index))
            manifest = {'version': 1, 'design_size': [800,600], 'entry_scene': 'unused.json',
                        'font': {'path':'unused.ttf'}, 'images': {}, 'script_index': 'legacy.json'}
            before = encode(manifest)
            (root/'manifest.json').write_bytes(before)
            # A failed verifier may leave unreferenced immutable JSON, but never changes the active package.
            with self.assertRaises(ValueError):
                migrate(root, '/bin/false', backup_parent=backup_dir)
            self.assertEqual((root/'manifest.json').read_bytes(), before)
            self.assertEqual(len(list(root.rglob('*.sc'))), 3)
            result = migrate(root, VERIFIER, backup_parent=backup_dir)
            self.assertTrue(result['published'])
            self.assertEqual(result['sc_files_remaining'], 0)
            modern = json.loads((root/json.loads((root/'manifest.json').read_text())['script_index']).read_text())
            self.assertEqual(modern['version'], 2)
            self.assertTrue(migrate(root, VERIFIER, backup_parent=backup_dir)['already_json'])
            restore(result['backup'])
            self.assertEqual(json.loads((root/'manifest.json').read_text()), manifest)
            for identifier, source in sources.items():
                self.assertEqual((root/index['scripts'][identifier]['path']).read_text(), source)


if __name__ == '__main__':
    unittest.main()
