#!/usr/bin/env python3
"""Metadata-only UI updates preserve existing resource and script snapshots."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from import_scripts import refresh_message_ui


class MessageUIImportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ef-message-ui-")
        self.addCleanup(self.temporary.cleanup)
        self.project = Path(self.temporary.name)
        self.out = self.project / "content"
        (self.out / "blobs").mkdir(parents=True)
        (self.project / "examples").mkdir()
        self.ui = json.loads((ROOT / "examples/message.ui.json").read_text())
        self.source = self.project / "examples/message.ui.json"
        self.source.write_text(json.dumps(self.ui))
        required = {self.ui["advance"]["asset"]}
        for mode in self.ui["modes"].values():
            required.update(image["asset"] for image in mode["images"])
        self.manifest = {"version": 1, "script_index": "old-index.json", "resources": "catalog.json",
                         "import_report": "old-report.json", "images": {name: {} for name in required}}
        self.index = {"version": 1, "encoding": "utf-8", "view": "old-view.json", "scripts": {"scr/a.sc": {"path": "blobs/script.sc"}}}
        (self.out / "old-index.json").write_text(json.dumps(self.index))
        (self.out / "blobs/script.sc").write_bytes(b"unchanged script snapshot")
        self.publish_manifest()

    def publish_manifest(self):
        (self.out / "manifest.json").write_text(json.dumps(self.manifest))

    def test_atomic_metadata_update_and_immutable_history(self):
        old_index = (self.out / "old-index.json").read_bytes()
        updated = refresh_message_ui(self.project, self.out)
        manifest = json.loads((self.out / "manifest.json").read_text())
        self.assertEqual(updated["scripts"], self.index["scripts"])
        self.assertEqual(updated["view"], "old-view.json")
        self.assertEqual((self.out / "old-index.json").read_bytes(), old_index)
        self.assertEqual((self.out / "blobs/script.sc").read_bytes(), b"unchanged script snapshot")
        self.assertEqual(manifest["resources"], "catalog.json")
        self.assertEqual(manifest["images"], self.manifest["images"])
        self.assertEqual(json.loads((self.out / updated["message_ui"]).read_text()), self.ui)
        first = (self.out / "manifest.json").read_bytes()
        refresh_message_ui(self.project, self.out)
        self.assertEqual((self.out / "manifest.json").read_bytes(), first)
        previous_ui = (self.out / updated["message_ui"]).read_bytes()
        self.ui["font_size"] = 23
        self.source.write_text(json.dumps(self.ui))
        replacement = refresh_message_ui(self.project, self.out)
        self.assertNotEqual(updated["message_ui"], replacement["message_ui"])
        self.assertEqual((self.out / updated["message_ui"]).read_bytes(), previous_ui)

    def test_missing_resources_and_escaped_index_preserve_entry(self):
        self.manifest["images"].pop(self.ui["advance"]["asset"])
        self.publish_manifest()
        previous = (self.out / "manifest.json").read_bytes()
        with self.assertRaises(ValueError):
            refresh_message_ui(self.project, self.out)
        self.assertEqual((self.out / "manifest.json").read_bytes(), previous)
        self.manifest["script_index"] = "../outside.json"
        self.publish_manifest()
        with self.assertRaises(ValueError):
            refresh_message_ui(self.project, self.out)

    def test_output_confinement_and_import_lock(self):
        import fcntl
        with self.assertRaises(ValueError):
            refresh_message_ui(self.project, self.project / "src")
        with (self.out / ".import.lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaises(BlockingIOError):
                refresh_message_ui(self.project, self.out)


if __name__ == "__main__":
    unittest.main()
