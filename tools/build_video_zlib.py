#!/usr/bin/env python3
"""Static vendored zlib for FFmpeg's compressed frame decoders; no file API."""
import concurrent.futures
import hashlib
import json
import os
import subprocess
from pathlib import Path
from preflight import ROOT, sdk_path, llvm_bin


def build(target):
    work = ROOT / 'build' / ('zlib-' + target)
    prefix = work / 'install'; (prefix / 'lib').mkdir(parents=True, exist_ok=True)
    source = ROOT / 'vendor/zlib'
    flags = ['-fPIC', '-O2', '-ffunction-sections', '-fdata-sections', '-I', str(source)]
    compiler, archiver = 'cc', 'ar'
    if target == 'ps4':
        sdk, llvm = sdk_path(), llvm_bin()
        resource = subprocess.check_output([str(llvm / 'clang'), '-print-resource-dir'], text=True).strip()
        compiler, archiver = str(llvm / 'clang'), str(llvm / 'llvm-ar')
        flags += ['--target=x86_64-pc-freebsd12-elf', '-nostdinc', '-isysroot', str(sdk),
                  '-isystem', str(sdk / 'include'), '-isystem', str(Path(resource) / 'include'),
                  '-D__ORBIS__=1', '-D_GNU_SOURCE=1', '-D_POSIX_C_SOURCE=200809L']
    files = json.loads((source / 'decoder_sources.json').read_text()) + ['uncompr.c']
    hasher = hashlib.sha256(json.dumps([compiler, archiver, flags, files]).encode())
    for path in sorted(source.rglob('*')):
        if path.is_file(): hasher.update(path.name.encode()); hasher.update(path.read_bytes())
    fingerprint = hasher.hexdigest(); stamp = work / 'fingerprint.txt'
    archive = prefix / 'lib/libz.a'
    if stamp.exists() and stamp.read_text() == fingerprint and archive.is_file(): return prefix
    objects = work / 'objects'; objects.mkdir(exist_ok=True)
    def compile(name):
        obj = objects / (name + '.o')
        result = subprocess.run([compiler, *flags, '-c', str(source / name), '-o', str(obj)], capture_output=True, text=True)
        (objects / (name + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode: raise RuntimeError('Video zlib compilation failed: ' + result.stderr)
        return str(obj)
    with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ.get('MC_BUILD_JOBS', '4'))) as pool:
        built = list(pool.map(compile, files))
    result = subprocess.run([archiver, 'rcs', str(archive), *built], capture_output=True, text=True)
    if result.returncode: raise RuntimeError('Video zlib archive failed: ' + result.stderr)
    stamp.write_text(fingerprint)
    return prefix
