#!/usr/bin/env python3
"""Build pinned, static, local-file-only FFmpeg for the host or OpenOrbis."""
import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path
from preflight import ROOT, sdk_path, llvm_bin
from build_dav1d import build as build_dav1d, project_alias
from build_video_zlib import build as build_zlib

SOURCE = ROOT / 'vendor/ffmpeg-7.1.3'
LIBRARIES = ['avformat', 'avcodec', 'swresample', 'swscale', 'avutil']


def object_header_valid(header, target):
    if target == 'ps4':
        return len(header) >= 20 and header[:6] == b'\x7fELF\x02\x01' and header[18:20] == b'\x3e\x00'
    if sys.platform == 'darwin':
        return header[:4] in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf')
    return len(header) >= 20 and header[:4] == b'\x7fELF'


def archive_valid(path, target):
    """Check each object, including BSD extended archive member names."""
    if not path.is_file():
        return False
    with path.open('rb') as archive:
        if archive.read(8) != b'!<arch>\n':
            return False
        objects = 0
        while header := archive.read(60):
            if len(header) != 60 or header[58:60] != b'`\n':
                return False
            try:
                size = int(header[48:58])
                name = header[:16].decode('ascii').strip()
                name_size = int(name[3:]) if name.startswith('#1/') else 0
            except (ValueError, UnicodeDecodeError):
                return False
            start = archive.tell()
            if name_size > size or size < 0:
                return False
            if name_size:
                name = archive.read(name_size).rstrip(b'\0').decode('utf-8', errors='replace')
            if name not in ('/', '//', '/SYM64/') and not name.startswith('__.SYMDEF'):
                if size - name_size < 20 or not object_header_valid(archive.read(20), target):
                    return False
                objects += 1
            archive.seek(start + size + size % 2)
            if archive.tell() > path.stat().st_size:
                return False
        return objects > 0


def repair_objects(work, target, archives, stamp):
    invalid = []
    for path in work.rglob('*.o'):
        with path.open('rb') as source:
            if not object_header_valid(source.read(20), target):
                invalid.append(path)
    damaged_archives = [path for path in archives if path.exists() and not archive_valid(path, target)]
    if invalid or damaged_archives:
        for path in invalid:
            path.unlink()
            path.with_suffix('.d').unlink(missing_ok=True)
            path.with_suffix('.dbg.d').unlink(missing_ok=True)
        # Recreate archives from the retained good objects; ar replacement can
        # otherwise leave duplicate basenames containing stale empty members.
        for library in LIBRARIES:
            (work / ('lib' + library) / ('lib' + library + '.a')).unlink(missing_ok=True)
        for path in archives:
            path.unlink(missing_ok=True)
        stamp.unlink(missing_ok=True)
    return invalid


def build(target):
    work = ROOT / 'build' / ('ffmpeg-' + target)
    work.mkdir(parents=True, exist_ok=True)
    prefix = work / 'install'
    # FFmpeg's makefiles require a source and prefix path without spaces.
    alias = project_alias()
    dav1d = build_dav1d(target)
    zlib = build_zlib(target)
    zlib_include = str(alias / 'vendor/zlib')
    zlib_library = str(alias / zlib.relative_to(ROOT) / 'lib')
    environment = dict(os.environ)
    environment['PKG_CONFIG_LIBDIR'] = os.pathsep.join([str(dav1d / 'lib/pkgconfig'), str(dav1d / 'libdata/pkgconfig')])
    environment.pop('PKG_CONFIG_PATH', None)
    options = [str(alias / 'vendor/ffmpeg-7.1.3/configure'), '--prefix=' + str(alias / prefix.relative_to(ROOT)),
               '--disable-autodetect', '--disable-shared', '--enable-static',
               '--enable-pic', '--disable-programs', '--disable-doc',
               '--disable-network', '--disable-protocols', '--disable-devices',
               '--disable-avdevice', '--disable-avfilter', '--disable-postproc',
               '--disable-encoders', '--disable-muxers', '--disable-hwaccels',
               '--disable-debug', '--enable-gpl', '--enable-pthreads',
               '--enable-libdav1d', '--enable-zlib', '--pkg-config-flags=--static']
    if target == 'ps4':
        sdk, llvm = sdk_path(), llvm_bin()
        resource = subprocess.check_output([str(llvm / 'clang'), '-print-resource-dir'], text=True).strip()
        cflags = ['--target=x86_64-pc-freebsd12-elf', '-nostdinc', '-isysroot', str(sdk),
                  '-isystem', str(sdk / 'include'), '-isystem', str(Path(resource) / 'include'),
                  '-D__ORBIS__=1', '-D_GNU_SOURCE=1', '-D_POSIX_C_SOURCE=200809L',
                  '-ffunction-sections', '-fdata-sections', '-O2', '-I' + zlib_include]
        options += ['--enable-cross-compile', '--target-os=freebsd', '--arch=x86_64',
                    '--cpu=btver2', '--cc=' + str(llvm / 'clang'),
                    '--ar=' + str(llvm / 'llvm-ar'), '--ranlib=' + str(llvm / 'llvm-ranlib'),
                    '--nm=' + str(llvm / 'llvm-nm'),
                    '--strip=' + str(llvm / 'llvm-strip'),
                    '--extra-cflags=' + shlex.join(cflags),
                    '--extra-ldflags=' + shlex.join(['--target=x86_64-pc-freebsd12-elf', '-fuse-ld=lld', '-nostdlib', '-L', str(sdk / 'lib'), '-L' + zlib_library]),
                    '--extra-libs=-lz -lc -lkernel']
    else:
        options += ['--extra-cflags=-I' + zlib_include, '--extra-ldflags=-L' + zlib_library, '--extra-libs=-lz']
    # Cache only when all source bytes and build options match.
    hasher = hashlib.sha256(json.dumps(options).encode())
    hasher.update((dav1d.parent / 'fingerprint.txt').read_bytes())
    hasher.update((zlib.parent / 'fingerprint.txt').read_bytes())
    configuration_fingerprint = hasher.hexdigest()
    configuration_stamp = work / 'configuration-fingerprint.txt'
    for path in sorted(SOURCE.rglob('*')):
        if path.is_file():
            hasher.update(path.relative_to(SOURCE).as_posix().encode())
            hasher.update(path.read_bytes())
    fingerprint = hasher.hexdigest()
    stamp = work / 'fingerprint.txt'
    archives = [prefix / 'lib' / ('lib' + lib + '.a') for lib in LIBRARIES]
    repaired = repair_objects(work, target, archives, stamp)
    if stamp.exists() and stamp.read_text() == fingerprint and all(p.is_file() for p in archives):
        if not configuration_stamp.exists() or configuration_stamp.read_text() != configuration_fingerprint:
            configuration_stamp.write_text(configuration_fingerprint)
        return prefix
    with (work / 'build.log').open('w') as log:
        for path in repaired:
            log.write('Rebuilding invalid object: ' + str(path.relative_to(work)) + '\n')
        log.flush()
        configured = configuration_stamp.exists() and configuration_stamp.read_text() == configuration_fingerprint and all(
            (work / name).is_file() for name in ('ffbuild/config.mak', 'config.h', 'config_components.h'))
        commands = ([] if configured else [options]) + [['make', '-j' + os.environ.get('MC_BUILD_JOBS', '4')], ['make', 'install']]
        for command in commands:
            result = subprocess.run(command, cwd=work, env=environment, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError('FFmpeg build failed; see ' + str(work / 'build.log'))
            if command is options:
                configuration_stamp.write_text(configuration_fingerprint)
    if repair_objects(work, target, archives, stamp):
        raise RuntimeError('FFmpeg generated invalid objects; see ' + str(work / 'build.log'))
    if not all(archive_valid(path, target) for path in archives):
        raise RuntimeError('FFmpeg archive validation failed; see ' + str(work / 'build.log'))
    stamp.write_text(fingerprint)
    # Keep the enabled components as reviewable evidence of codec coverage.
    (ROOT / 'reports' / ('ffmpeg-' + target + '-configuration.txt')).write_text(
        shlex.join(options) + '\n\n' + (work / 'config_components.h').read_text())
    return prefix


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('target', choices=['host', 'ps4'])
    print(build(parser.parse_args().target))
