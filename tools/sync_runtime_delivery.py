#!/usr/bin/env python3
"""Sync a reviewed source/ELF update to the existing sibling delivery, never PKGs."""
import argparse
import datetime
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DELIVERY = ROOT.parent / 'PS4 Media Center'
PLAN = ROOT / 'reports/runtime-delivery-plan.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def candidates():
    files = list((ROOT / 'src').rglob('*')) + list((ROOT / 'assets/ui').rglob('*'))
    files += list((ROOT / 'tests').glob('*')) + list((ROOT / 'tools').glob('*'))
    files += list(ROOT.glob('*.md')) + list(ROOT.glob('*.sh')) + list(ROOT.glob('*.command'))
    files += [ROOT / 'CMakeLists.txt']
    files += list((ROOT / 'reports').glob('*')) + list((ROOT / 'reports/host-validation').glob('*'))
    files += [ROOT / 'build/ps4/v3' / name for name in [
        'ps4_media_center.elf', 'ps4_media_center.oelf', 'eboot.bin', 'link.map',
        'archive-extraction.tsv', 'condition-compat-disassembly.txt', 'clock-compat-disassembly.txt',
        'elf-inspection.log', 'link.log', 'fself.log']]
    allowed = {'.cpp', '.c', '.h', '.hpp', '.py', '.sh', '.command', '.md', '.json', '.jsonl',
               '.png', '.svg', '.tsv', '.log', '.txt', '.sha256', '.elf', '.oelf', '.bin', '.map'}
    return sorted({path for path in files if path.is_file() and not path.is_symlink()
                   and (path.suffix in allowed or path.name == 'CMakeLists.txt')})


def prepare():
    entries = []
    for path in candidates():
        name = path.relative_to(ROOT).as_posix()
        if name in ['reports/runtime-delivery-plan.json', 'reports/runtime-delivery-sync.json']:
            continue
        target = DELIVERY / name
        source_hash, old_hash = digest(path), digest(target)
        if source_hash != old_hash:
            entries.append(dict(path=name, source_sha256=source_hash, destination_sha256=old_hash,
                                bytes=path.stat().st_size))
    plan = dict(state='prepared', source=str(ROOT), destination=str(DELIVERY), files=entries, pkg_files_excluded=True)
    PLAN.write_text(json.dumps(plan, indent=2) + '\n')
    print(json.dumps(dict(files=len(entries), bytes=sum(item['bytes'] for item in entries),
                         destination=str(DELIVERY), plan=str(PLAN), pkg_files_excluded=True), indent=2))


def apply():
    plan = json.loads(PLAN.read_text())
    if plan.get('state') != 'prepared':
        raise RuntimeError('Delivery plan is inactive; prepare a new plan only when a transfer is requested')
    if plan['source'] != str(ROOT) or plan['destination'] != str(DELIVERY) or DELIVERY == ROOT:
        raise RuntimeError('Delivery plan does not match the working source checkout')
    allowed_sources = set(candidates())
    for item in plan['files']:
        relative = Path(item['path'])
        if relative.is_absolute() or '..' in relative.parts or relative.suffix == '.pkg':
            raise RuntimeError('Unsafe delivery path')
        source, target = ROOT / relative, DELIVERY / relative
        if source not in allowed_sources or not target.parent.resolve().is_relative_to(DELIVERY.resolve()):
            raise RuntimeError('Path is outside the reviewed source/delivery selection: ' + str(relative))
        if source.is_symlink() or target.is_symlink() or digest(source) != item['source_sha256']:
            raise RuntimeError('Source changed or a symlink appeared: ' + str(relative))
        if digest(target) != item['destination_sha256']:
            raise RuntimeError('Delivery changed since review: ' + str(relative))
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    backup = ROOT / 'build/delivery-backup' / stamp
    for item in plan['files']:
        relative = Path(item['path']);source, target = ROOT / relative, DELIVERY / relative
        if target.exists():
            saved = backup / relative;saved.parent.mkdir(parents=True, exist_ok=True);shutil.copy2(target, saved)
        target.parent.mkdir(parents=True, exist_ok=True);shutil.copy2(source, target)
        if digest(target) != item['source_sha256']:
            raise RuntimeError('Delivery hash mismatch: ' + str(relative))
    receipt = dict(date=stamp, source=str(ROOT), destination=str(DELIVERY), files=len(plan['files']),
                   hashes_verified=True, backup=str(backup), pkg_files_excluded=True)
    text = json.dumps(receipt, indent=2) + '\n'
    (ROOT / 'reports/runtime-delivery-sync.json').write_text(text)
    (DELIVERY / 'reports/runtime-delivery-sync.json').write_text(text)
    print(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser();parser.add_argument('--apply', action='store_true')
    arguments = parser.parse_args()
    apply() if arguments.apply else prepare()
