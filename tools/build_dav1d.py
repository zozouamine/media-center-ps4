#!/usr/bin/env python3
"""Build the pinned AV1 software decoder without downloads or host libraries."""
import hashlib
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path
from preflight import ROOT, sdk_path, llvm_bin

SOURCE = ROOT / 'vendor/dav1d-1.5.1'


def project_alias():
    # pkg-config and FFmpeg's makefiles need paths without embedded spaces.
    alias = Path(tempfile.gettempdir()) / ('ps4mc-ffmpeg-' + hashlib.sha256(str(ROOT).encode()).hexdigest()[:12])
    if not alias.exists():
        alias.symlink_to(ROOT, target_is_directory=True)
    if alias.resolve() != ROOT:
        raise RuntimeError('Decoder build alias points to a different project')
    return alias


def build(target):
    if target not in ('host', 'ps4'):
        raise ValueError('Unknown decoder target')
    work = ROOT / 'build' / ('dav1d-' + target)
    work.mkdir(parents=True, exist_ok=True)
    prefix = work / 'install'
    alias = project_alias()
    options = ['meson', 'setup', str(work / 'objects'), str(alias / SOURCE.relative_to(ROOT)),
               '--prefix=' + str(alias / prefix.relative_to(ROOT)), '--libdir=lib',
               '--wrap-mode=nodownload', '-Ddefault_library=static', '-Dbuildtype=release',
               '-Db_staticpic=true', '-Db_ndebug=true', '-Denable_tools=false', '-Denable_tests=false']
    cross = {}
    if target == 'ps4':
        sdk, llvm = sdk_path(), llvm_bin()
        resource = subprocess.check_output([str(llvm / 'clang'), '-print-resource-dir'], text=True).strip()
        cross = {
            'binaries': {'c': str(llvm / 'clang'), 'ar': str(llvm / 'llvm-ar'),
                         'strip': str(llvm / 'llvm-strip'), 'pkg-config': 'pkg-config'},
            'built-in options': {
                'c_args': ['--target=x86_64-pc-freebsd12-elf', '-nostdinc', '-isysroot', str(sdk),
                           '-isystem', str(sdk / 'include'), '-isystem', str(Path(resource) / 'include'),
                           '-D__ORBIS__=1', '-D_GNU_SOURCE=1', '-D_POSIX_C_SOURCE=200809L',
                           '-ffunction-sections', '-fdata-sections'],
                'c_link_args': ['--target=x86_64-pc-freebsd12-elf', '-fuse-ld=lld', '-nostdlib',
                                '-L' + str(sdk / 'lib'), '-lc', '-lkernel']},
            'properties': {'needs_exe_wrapper': True},
            'host_machine': {'system': 'freebsd', 'cpu_family': 'x86_64', 'cpu': 'btver2', 'endian': 'little'}}
        cross_path = work / 'cross.ini'
        def machine_value(value):
            if isinstance(value, bool):
                return 'true' if value else 'false'
            if isinstance(value, list):
                return '[' + ', '.join(machine_value(entry) for entry in value) + ']'
            return "'" + value.replace('\\', '\\\\').replace("'", "\\'") + "'"
        cross_path.write_text('\n'.join('[' + section + ']\n' + '\n'.join(
            key + ' = ' + machine_value(value) for key, value in values.items()) for section, values in cross.items()) + '\n')
        options += ['--cross-file=' + str(cross_path)]
    hasher = hashlib.sha256(json.dumps([options, cross]).encode())
    if target == 'ps4':
        hasher.update(b'OpenOrbis: disable optional affinity and thread-name APIs v1')
    for path in sorted(SOURCE.rglob('*')):
        if path.is_file():
            hasher.update(path.relative_to(SOURCE).as_posix().encode())
            hasher.update(path.read_bytes())
    fingerprint = hasher.hexdigest()
    stamp = work / 'fingerprint.txt'
    if stamp.exists() and stamp.read_text() == fingerprint and (prefix / 'lib/libdav1d.a').is_file():
        return prefix
    if (work / 'objects/meson-private/coredata.dat').exists():
        options += ['--reconfigure']
    with (work / 'build.log').open('w') as log:
        for command in [options, ['meson', 'compile', '-C', str(work / 'objects'), '-j', os.environ.get('MC_BUILD_JOBS', '4')],
                        ['meson', 'install', '-C', str(work / 'objects')]]:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError('AV1 decoder build failed; see ' + str(work / 'build.log'))
            if command is options and target == 'ps4':
                # OpenOrbis exposes musl's Linux-shaped optional pthread APIs
                # beneath FreeBSD headers. Decoding uses an explicit bounded
                # thread count and needs neither affinity nor thread naming.
                config_path = work / 'objects/config.h'
                config = config_path.read_text()
                config = re.sub(r'(#define HAVE_PTHREAD_(?:GETAFFINITY_NP|SETAFFINITY_NP|SETNAME_NP|SET_NAME_NP)) 1', r'\1 0', config)
                config_path.write_text(config)
                (ROOT / 'reports/dav1d-ps4-configuration.h').write_text(config)
    stamp.write_text(fingerprint)
    return prefix


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('target', choices=['host', 'ps4'])
    print(build(parser.parse_args().target))
