#!/usr/bin/env python3
"""Compile an existing content package to JSON, verify it, then atomically publish it."""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from prepare_content import safe_path, write_json
from script_json import compile_script, encode

ROOT = Path(__file__).resolve().parents[1]


def blob(root, value, suffix):
    data = encode(value)
    digest = hashlib.sha256(data).hexdigest()
    name = f'blobs/{digest}.{suffix}'
    target = safe_path(root, name)
    if not target.exists() or target.read_bytes() != data:
        write_json(target, value)
    return name, digest


def run_verifier(binary, *args):
    result = subprocess.run([str(binary), *map(str, args)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise ValueError('Migration verification failed; manifest was not published:\n'+result.stdout)
    return result.stdout


def migrate(root, verifier, *, check_only=False, backup_parent=None):
    root, verifier = Path(root).resolve(), Path(verifier).resolve()
    if root == ROOT or not root.is_relative_to(ROOT) or root.is_relative_to(ROOT/'.git'):
        raise ValueError('Content must be a project subdirectory')
    for name in ('assets','src','docs','tools','tests','examples','schemas'):
        if root.is_relative_to(ROOT/name):
            raise ValueError('Content cannot overwrite project sources')
    if not verifier.is_file():
        raise ValueError('Build ef_script_verify first, then pass --verifier')
    with safe_path(root, '.import.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        manifest_path = safe_path(root, 'manifest.json')
        manifest = json.loads(manifest_path.read_text())
        old_path = manifest.get('script_index', 'scripts/index.json')
        old = json.loads(safe_path(root, old_path).read_text())
        if old['version'] == 2:
            result = run_verifier(verifier, root, old_path)
            print('Already JSON; validation PASS')
            return {'already_json': True, 'audit': result}
        if old['version'] != 1 or old.get('encoding') != 'utf-8':
            raise ValueError('Expected UTF-8 legacy index v1')
        parent = Path(backup_parent).resolve() if backup_parent else ROOT/'work/script-migration'
        if not parent.is_relative_to(ROOT/'work'):
            raise ValueError('Backup must be under project work/')
        parent.mkdir(parents=True, exist_ok=True)
        backup = Path(tempfile.mkdtemp(prefix=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-'), dir=parent))
        new = {**old, 'version': 2, 'format': 'ef.script.v1', 'scripts': {}}
        for identifier, entry in old['scripts'].items():
            data = safe_path(root, entry['path']).read_bytes()
            if 'utf8_sha256' in entry and hashlib.sha256(data).hexdigest() != entry['utf8_sha256']:
                raise ValueError('Legacy source checksum mismatch: '+identifier)
            document = compile_script(data.decode('utf-8'), identifier)
            path, digest = blob(root, document, 'script.json')
            new['scripts'][identifier] = {**entry, 'path': path, 'json_sha256': digest}
        new_path, _ = blob(root, new, 'scripts.json')
        candidate = {**manifest, 'script_index': new_path}
        if 'resources' in manifest:
            catalog = json.loads(safe_path(root, manifest['resources']).read_text())
            for identifier, resource in catalog['entries'].items():
                if resource['kind'] == 'script':
                    if identifier not in new['scripts']:
                        raise ValueError('Resource catalog script is absent from index: '+identifier)
                    resource['path'] = new['scripts'][identifier]['path']
            candidate['resources'], _ = blob(root, catalog, 'resources.json')
        output = run_verifier(verifier, root, old_path, new_path, backup/'verification.json')
        (backup/'verification.log').write_text(output)
        report = json.loads((backup/'verification.json').read_text())
        report.update(backup=str(backup), candidate_index=new_path, published=False)
        if check_only:
            write_json(backup/'migration.json', report)
            print(json.dumps(report, ensure_ascii=False, indent=2))
            return report
        # Back up every SC file and obsolete v1 index before changing the active package.
        legacy = []
        for file in root.rglob('*'):
            if file.is_file() and file.suffix.lower() == '.sc':
                legacy.append(file)
            elif file.is_file() and (file == safe_path(root, old_path) or file.name == 'index.json' or file.name.endswith('.scripts.json')):
                try:
                    value = json.loads(file.read_text())
                    if isinstance(value, dict) and value.get('version') == 1 and isinstance(value.get('scripts'), dict):
                        legacy.append(file)
                except (ValueError, UnicodeError):
                    pass
        backed_up = {}
        for file in legacy:
            relative = file.relative_to(root).as_posix()
            safe_path(root, relative)  # refuses links escaping the content root
            target = backup/'legacy'/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
            digest = hashlib.sha256(file.read_bytes()).hexdigest()
            if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
                raise ValueError('Backup checksum mismatch: '+relative)
            backed_up[relative] = digest
        (backup/'manifest.before.json').write_bytes(manifest_path.read_bytes())
        report.update(content=str(root), legacy_files=backed_up)
        write_json(backup/'migration.json', report)
        try:
            write_json(manifest_path, candidate)
            (backup/'post-publish-audit.log').write_text(run_verifier(verifier, root, new_path))
        except Exception:
            write_json(manifest_path, manifest)
            raise
        # Cleanup is safe now: neither the active index nor resource catalog references SC.
        for relative in backed_up:
            safe_path(root, relative).unlink()
        report['published'] = True
        report['sc_files_remaining'] = sum(p.is_file() and p.suffix.lower()=='.sc' for p in root.rglob('*'))
        write_json(backup/'migration.json', report)
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return report


def restore(backup):
    backup = Path(backup).resolve()
    if not backup.is_relative_to(ROOT/'work'):
        raise ValueError('Backup must be under work/')
    report = json.loads((backup/'migration.json').read_text())
    root = Path(report['content']).resolve()
    if root == ROOT or not root.is_relative_to(ROOT) or any(root.is_relative_to(ROOT/n) for n in ('src','docs','tools','tests','schemas','assets','.git')):
        raise ValueError('Restore only supports project content/')
    with safe_path(root, '.import.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        for relative, digest in report['legacy_files'].items():
            source = safe_path(backup/'legacy', relative)
            if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
                raise ValueError('Backup checksum mismatch: '+relative)
        for relative in report['legacy_files']:
            target = safe_path(root, relative)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(safe_path(backup/'legacy', relative), target)
        write_json(safe_path(root, 'manifest.json'), json.loads((backup/'manifest.before.json').read_text()))
    print('Legacy content restored; it requires the pre-migration engine or the offline legacy verifier.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--content', type=Path, default=ROOT/'content')
    p.add_argument('--verifier', type=Path, default=ROOT/'build/dev/ef_script_verify')
    p.add_argument('--check-only', action='store_true', help='Write candidate JSON and verify; keep the active manifest unchanged')
    p.add_argument('--restore', type=Path, help='Restore a verified legacy backup')
    a = p.parse_args()
    try:
        if a.restore:
            restore(a.restore)
        else:
            migrate(a.content, a.verifier, check_only=a.check_only)
    except (ValueError, OSError, KeyError) as e:
        p.exit(1, f'migrate_scripts: {e}\n')


if __name__ == '__main__':
    main()
