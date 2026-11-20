#!/usr/bin/env python3
"""Cross-compile every production module; release only a verified real homebrew PKG."""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as ET
from pathlib import Path
from preflight import ROOT, audit, digest, sdk_path, llvm_bin, pkg_command
from audit_ps4_runtime import audit_runtime
from verify_ps4_self import read_authinfo, verify_self_authinfo
from build_ffmpeg import build as build_ffmpeg, LIBRARIES as FFMPEG_LIBRARIES

TITLE_ID = 'AMIN00048'
CONTENT_ID = 'IV0000-AMIN00048_00-MEDIACENTER00001'
SELF_PROFILE = os.environ.get('MC_PS4_SELF_PROFILE', 'default')


def run(args, cwd, log=None):
    command = [str(x) for x in args]
    environment = dict(os.environ)
    environment.setdefault('OO_PS4_TOOLCHAIN', str(sdk_path()))
    environment.update(DOTNET_CLI_HOME=str(ROOT / 'build/dotnet-home'), DOTNET_CLI_TELEMETRY_OPTOUT='1',
                       DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1', DOTNET_GENERATE_ASPNET_CERTIFICATE='false')
    result = subprocess.run(command, cwd=cwd, env=environment, capture_output=True, text=True)
    output = result.stdout + result.stderr
    if log: Path(log).write_text(output)
    if result.returncode:
        raise RuntimeError(f'{Path(command[0]).name} failed ({result.returncode}):\n{output[-12000:]}')
    return output


def next_version():
    override = os.environ.get('MC_APP_VERSION')
    if override:
        return int(override)
    output = ROOT / 'PKG Build'; output.mkdir(exist_ok=True)
    versions = [int(m.group(1)) for p in output.glob('ps4_media_center_v*.pkg') if (m := re.fullmatch(r'ps4_media_center_v(\d+)\.pkg', p.name))]
    return max(versions, default=0) + 1


def source_files():
    files = []
    for path in sorted((ROOT / 'src').rglob('*')):
        if path.suffix not in ['.cpp', '.c']: continue
        if '/platform/macos/' in str(path) or '/platform/host/' in str(path): continue
        files.append(path)
    for library in ['libwebp', 'libtiff', 'zlib']:
        files += [ROOT / 'vendor' / library / name for name in json.loads((ROOT / 'vendor' / library / 'decoder_sources.json').read_text())]
    return files


def input_fingerprint():
    files = list((ROOT / 'src').rglob('*.cpp')) + list((ROOT / 'src').rglob('*.hpp')) + list((ROOT / 'src').rglob('*.h')) + list((ROOT / 'src').rglob('*.c')) + list((ROOT / 'src').rglob('*.inc'))
    files += list((ROOT / 'vendor').rglob('*')) + list((ROOT / 'assets/ui').glob('*')) + [ROOT / 'assets/fonts/PS4MC-Sans.ttf', ROOT / 'assets/fonts/PS4MC-Arabic.ttf', ROOT / 'sce_sys/icon0.png']
    files += list((ROOT / 'assets/ui/transport').glob('*')) + [ROOT / 'CMakeLists.txt', ROOT / 'build.sh', ROOT / 'tools/build_ffmpeg.py', ROOT / 'tools/build_dav1d.py', ROOT / 'tools/build_video_zlib.py', ROOT / 'tools/build_ps4.py', ROOT / 'tools/audit_ps4_runtime.py', ROOT / 'tools/preflight.py', ROOT / 'tools/verify_ps4_self.py', ROOT / 'sce_sys/authinfo.hex']
    data = {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(files) if p.is_file()}
    settings = {'self_profile': SELF_PROFILE}
    return hashlib.sha256(json.dumps({'inputs': data, 'build_options': settings}, sort_keys=True).encode()).hexdigest(), data


def make_self(elf, work, destination, preflight):
    if SELF_PROFILE not in ('default', 'homebrew'):
        raise ValueError('MC_PS4_SELF_PROFILE must be default or homebrew')
    command = [preflight['fself'], f'-in={elf}', f'-out={work / "ps4_media_center.oelf"}',
               '--eboot', destination, '--paid', '0x3800000000000011']
    # The user's v5 default record launches; v4 and v6 with the public Homebrew
    # record fail before the UI. Keep the tested default until console startup
    # is understood. The explicit profile is a diagnostic override only.
    if SELF_PROFILE == 'homebrew': command += ['--authinfo', read_authinfo().hex()]
    run(command, ROOT, work / 'fself.log')
    return verify_self_authinfo(destination, profile=SELF_PROFILE)


def compile_elf(work):
    sdk = sdk_path(); llvm = llvm_bin(); objects = work / 'objects'; objects.mkdir(parents=True, exist_ok=True)
    ffmpeg = build_ffmpeg('ps4')
    resource = subprocess.check_output([str(llvm / 'clang'), '-print-resource-dir'], text=True).strip()
    flags = ['--target=x86_64-pc-freebsd12-elf', '-fPIC', '-O2', '-funwind-tables', '-ffunction-sections', '-fdata-sections',
             '-DMC_PS4=1', f'-DMC_PS4_SELF_PROFILE="{SELF_PROFILE}"', '-D__ORBIS__=1', '-DWEBP_USE_THREAD=1', '-D_GNU_SOURCE=1', '-D_POSIX_C_SOURCE=200809L', '-nostdinc', '-isysroot', str(sdk), '-isystem', str(sdk / 'include'),
             '-isystem', str(Path(resource) / 'include'),
             '-I', str(ROOT / 'src'), '-I', str(ROOT / 'vendor'), '-I', str(ROOT / 'vendor/libwebp'),
             '-I', str(ROOT / 'vendor/libtiff/libtiff'), '-I', str(ROOT / 'vendor/zlib'),
             '-I', str(ROOT / 'vendor/goldhen-sdk/include'), '-I', str(ffmpeg / 'include'), '-Wall', '-Wextra']
    def compile(path):
        name = path.relative_to(ROOT).as_posix().replace('/', '_') + '.o'; object = objects / name
        extra = ['-std=c++17', '-fno-rtti', '-isystem', str(sdk / 'include/c++/v1')] if path.suffix == '.cpp' else ['-std=c99']
        if 'libwebp' in path.parts: extra += ['-DHAVE_CONFIG_H=1']
        compiler = llvm / ('clang++' if path.suffix == '.cpp' else 'clang')
        run([compiler, *flags, *extra, '-c', path, '-o', object], ROOT, objects / (name + '.log'))
        print('PS4 compiled ' + path.relative_to(ROOT).as_posix(), flush=True)
        return object
    with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ.get('MC_BUILD_JOBS', '4'))) as executor:
        built = list(executor.map(compile, source_files()))
    elf = work / 'ps4_media_center.elf'
    libs = ['-lc++', '-lc++abi', '-lunwind', '-lc', '-lkernel', '-lSceVideoOut', '-lScePad', '-lSceUserService', '-lSceAudioOut']
    run([llvm / 'ld.lld', '-m', 'elf_x86_64', '-pie', '--script', sdk / 'link.x', '--eh-frame-hdr', '--gc-sections',
         '--Map=' + str(work / 'link.map'), '--why-extract=' + str(work / 'archive-extraction.tsv'), '-L', sdk / 'lib',
         sdk / 'lib/crt1.o', *built, '--start-group',
         *[ffmpeg / 'lib' / ('lib' + name + '.a') for name in FFMPEG_LIBRARIES],
         ROOT / 'build/dav1d-ps4/install/lib/libdav1d.a',
         ROOT / 'build/zlib-ps4/install/lib/libz.a',
         *libs, '-lm', '--end-group', '-o', elf], ROOT, work / 'link.log')
    h = elf.read_bytes()[:64]
    if h[:4] != b'\x7fELF' or h[4] != 2 or struct.unpack_from('<HH', h, 16) != (3, 62):
        raise RuntimeError('Cross-link did not produce an x86-64 PIE ELF')
    if (llvm / 'llvm-readelf').exists():
        run([llvm / 'llvm-readelf', '-h', '-l', '-d', elf], ROOT, work / 'elf-inspection.log')
    compatibility = audit_runtime(work, sdk, llvm)
    if not compatibility['passed']:
        failed = [item['name'] for item in compatibility['checks'] if not item['passed']]
        raise RuntimeError('PS4 runtime ABI audit failed: ' + ', '.join(failed))
    return elf


def sfo(path, version):
    entries = {'APP_TYPE': 1, 'APP_VER': f'{version}.00', 'ATTRIBUTE': 0, 'CATEGORY': 'gd', 'CONTENT_ID': CONTENT_ID,
               'DOWNLOAD_DATA_SIZE': 0, 'SYSTEM_VER': 0, 'TITLE': 'Media Center', 'TITLE_ID': TITLE_ID, 'VERSION': f'{version}.00'}
    sizes = {'APP_VER': 8, 'VERSION': 8, 'CATEGORY': 4, 'CONTENT_ID': 48, 'TITLE': 128, 'TITLE_ID': 12}
    keys = bytearray(); data = bytearray(); records = bytearray()
    for key, value in sorted(entries.items()):
        offset = len(keys); keys += key.encode() + b'\0'
        encoded = struct.pack('<I', value) if isinstance(value, int) else value.encode() + b'\0'
        maximum = sizes.get(key, 4)
        if len(encoded) > maximum: raise ValueError('SFO field too long: ' + key)
        records += struct.pack('<HHIII', offset, 0x404 if isinstance(value, int) else 0x204, len(encoded), maximum, len(data))
        data += encoded + bytes(maximum - len(encoded))
    key_offset = 20 + len(records); data_offset = (key_offset + len(keys) + 3) & ~3
    path.write_bytes(struct.pack('<5I', 0x46535000, 0x101, key_offset, data_offset, len(entries)) + records + keys + bytes(data_offset-key_offset-len(keys)) + data)


def complete_gp4_directories(path, paths):
    # The installed create-gp4 emits sample directories. Every actual file
    # parent must also exist in rootdir for LibOrbisPkg's FindDir traversal.
    tree = ET.parse(path)
    root = tree.getroot().find('rootdir')
    if root is None:
        root = ET.SubElement(tree.getroot(), 'rootdir')
    root.clear()
    directories = set()
    for name in paths:
        parent = Path(name).parent
        while parent.as_posix() != '.':
            directories.add(parent.as_posix())
            parent = parent.parent
    nodes = {'.': root}
    for name in sorted(directories, key=lambda item: (len(Path(item).parts), item)):
        directory = Path(name)
        nodes[name] = ET.SubElement(nodes[directory.parent.as_posix()], 'dir', targ_name=directory.name)
    tree.write(path, encoding='utf-8', xml_declaration=True)


def corresponding_video_source(destination):
    """Accompany the GPL-linked executable with its sources/build instructions."""
    folders = ['src', 'vendor', 'assets', 'licenses', 'tools/openorbis-module-source']
    files = set()
    for name in folders:
        files.update(path for path in (ROOT / name).rglob('*') if path.is_file() and path.name != '.DS_Store')
    # CMake references the test sources even for a host application configure.
    # Include their generators/stubs without generated media or deep-path fixtures.
    files.update(path for path in (ROOT / 'tests').glob('*') if path.suffix in ('.cpp', '.hpp', '.py'))
    files.update(path for path in (ROOT / 'tests/ps4_stubs').rglob('*') if path.is_file())
    files.update((ROOT / 'tools').glob('*.py'))
    files.update(ROOT / name for name in ['CMakeLists.txt', 'build.sh', 'preflight.sh', 'BUILDING.md', 'README.md',
                 'TEST_PLAN.md', 'TECHNICAL_DESIGN.md', 'CONTROLLER_MAP.md',
                 'SUPPORTED_FORMATS.md', 'THIRD_PARTY_NOTICES.md', 'LICENSE', 'sce_sys/icon0.png', 'sce_sys/authinfo.hex'])
    with tarfile.open(destination, 'w:xz', preset=3) as archive:
        for path in sorted(files):
            # Add individual regular files only; no host caches, generated media
            # or absolute paths. Original component license notices are retained.
            if path.is_symlink():
                raise RuntimeError('Unexpected symlink in corresponding source: ' + str(path))
            archive.add(path, arcname='ps4-media-center/' + path.relative_to(ROOT).as_posix(), recursive=False)


def package(elf, work, version, fingerprint, inputs, preflight):
    if SELF_PROFILE != 'default':
        raise RuntimeError('Package builds require the default SELF profile: homebrew failed the v4 and v6 console trials')
    sdk = sdk_path(); stage = work / 'stage'
    if stage.exists(): shutil.rmtree(stage)
    (stage / 'sce_sys').mkdir(parents=True); (stage / 'sce_module').mkdir(); (stage / 'assets/fonts').mkdir(parents=True); (stage / 'licenses').mkdir()
    self_authinfo = make_self(elf, work, stage / 'eboot.bin', preflight)
    sfo(stage / 'sce_sys/param.sfo', version); shutil.copy2(ROOT / 'sce_sys/icon0.png', stage / 'sce_sys/icon0.png')
    for name in ['PS4MC-Sans.ttf', 'PS4MC-Arabic.ttf']:
        shutil.copy2(ROOT / 'assets/fonts' / name, stage / 'assets/fonts' / name)
    shutil.copytree(ROOT / 'assets/ui', stage / 'assets/ui')
    for name in ['libc.prx', 'libSceFios2.prx']:
        shutil.copy2(sdk / 'samples/hello_world/sce_module' / name, stage / 'sce_module' / name)
    shutil.copy2(ROOT / 'LICENSE', stage / 'LICENSE')
    shutil.copy2(ROOT / 'THIRD_PARTY_NOTICES.md', stage / 'licenses/THIRD_PARTY_NOTICES.md')
    for path in sorted((ROOT / 'licenses').glob('*')):
        if path.is_file(): shutil.copy2(path, stage / 'licenses' / path.name)
    corresponding_video_source(stage / 'licenses/ps4mc-corresponding-source.tar.xz')
    shutil.copy2(ROOT / 'reports/ffmpeg-ps4-configuration.txt', stage / 'licenses/ffmpeg-configuration.txt')
    shutil.copy2(ROOT / 'reports/dav1d-ps4-configuration.h', stage / 'licenses/dav1d-configuration.h')
    metadata = {'title': 'Media Center', 'title_id': TITLE_ID, 'content_id': CONTENT_ID, 'version': f'{version}.00',
                'target': 'OpenOrbis PS4', 'source_sha256': fingerprint, 'elf_sha256': digest(elf), 'hardware_tested': False,
                'video': 'Static FFmpeg 7.1.3 + dav1d 1.5.1 + zlib 1.3.1 software decoders; timestamp-based MP4/MKV/MOV/AVI/WebM/MPEG/TS playback with H.264/HEVC/AV1/MPEG-4/VP8/VP9 and decoded companion audio; target runtime verification pending',
                'music': 'WAV/MP3/FLAC/Ogg plus FFmpeg-backed AAC/M4A/Opus/WMA/AIFF; 48 kHz S16 stereo output',
                'video_output': 'Decoded presentation timestamps; 48 kHz S16 stereo audio clock; 1080p display scaling; pause and seek flush/preroll',
                'corresponding_source': 'licenses/ps4mc-corresponding-source.tar.xz',
                'application_binary_license': 'GPL-3.0-or-later with original MIT source notices retained',
                'interface_language': 'English only', 'arabic_filenames': 'UTF-8 reading, contextual glyphs and search input retained',
                'data_directory': '/data/PS4MediaCenter', 'self_authinfo': self_authinfo,
                'startup_trace': '/data/PS4MediaCenter/startup.log',
                'startup_scan': 'After the first successful frame, through the directory worker',
                'usb_open_diagnostics': 'Native libc open; GoldHEN SDK result/errno, per-mount errno and private startup-log recovery',
                'usb_namespaces': ['/usb0 through /usb7', '/mnt/usb0 through /mnt/usb7']}
    metadata['usb_access'] = 'Native read-only descriptors after GoldHEN SDK process namespace access; no FTP or networking'
    metadata['goldhen_sdk_commit'] = 'bcea3c7ef01dac6d7f9f49ebf9e90fe66d86f5f7'
    metadata['goldhen_sdk_commands'] = {'version': 0, 'process_access': 2, 'restore_process_access': 3}
    metadata['goldhen_version_reply'] = 'The known SDK 0x100 version reply is accepted with either carry flag; access and restore errors remain strict'
    (stage / 'build-info.json').write_text(json.dumps(metadata, indent=2) + '\n')
    paths = [p.relative_to(stage).as_posix() for p in sorted(stage.rglob('*')) if p.is_file()]
    run([preflight['gp4'], '-out', 'package.gp4', '--content-id=' + CONTENT_ID, '--files', ' '.join(paths)], stage, work / 'gp4.log')
    complete_gp4_directories(stage / 'package.gp4', paths)
    staged = {name: {'bytes': (stage / name).stat().st_size, 'sha256': digest(stage / name)} for name in paths}
    run(pkg_command() + ['sfo_listentries', 'sce_sys/param.sfo'], stage, work / 'sfo-inspection.log')
    run(pkg_command() + ['pkg_build', 'package.gp4', work], stage, work / 'pkg-build.log')
    built = work / (CONTENT_ID + '.pkg')
    if not built.is_file() or built.read_bytes()[:4] != b'\x7fCNT': raise RuntimeError('No real PS4 PKG produced')
    validation = run(pkg_command() + ['pkg_validate', '--verbose', built], stage, work / 'pkg-validation.log')
    if '[FAIL]' in validation or '[ERROR]' in validation or 'PKG Header Signature' not in validation:
        raise RuntimeError('PKG signature/hash verification did not pass every check')
    extracted = work / 'extracted'
    if extracted.exists(): shutil.rmtree(extracted)
    run(pkg_command() + ['pkg_makegp4', '--passcode', '0'*32, built, extracted], stage, work / 'payload-extraction.log')
    failures = [name for name, entry in staged.items() if not (extracted / name).is_file() or digest(extracted / name) != entry['sha256']]
    if failures: raise RuntimeError('Packed payload differs from the built files: ' + ', '.join(failures))
    verify_self_authinfo(extracted / 'eboot.bin', profile=SELF_PROFILE)
    final = ROOT / 'PKG Build' / f'ps4_media_center_v{version}.pkg'
    with final.open('xb') as output, built.open('rb') as input:
        shutil.copyfileobj(input, output)
    if digest(final) != digest(built): raise RuntimeError('Final copy differs from the verified package')
    report = {'date': datetime.datetime.now(datetime.timezone.utc).date().isoformat(), 'package': final.relative_to(ROOT).as_posix(), 'bytes': final.stat().st_size, 'sha256': digest(final),
              'elf_sha256': digest(elf), 'source_sha256': fingerprint, 'target': 'PS4 cross-build', 'hardware_tested': False,
              'pkg_signature_hash_validation': 'PASS', 'payload_round_trip': 'PASS', 'payload_files': staged, 'metadata': metadata,
              'sources': inputs, 'logs': str(work.relative_to(ROOT))}
    (ROOT / 'reports' / f'package-v{version}.json').write_text(json.dumps(report, indent=2) + '\n')
    (ROOT / 'reports' / f'package-v{version}.sha256').write_text(report['sha256'] + '  ' + final.name + '\n')
    shutil.copy2(work / 'pkg-validation.log', ROOT / 'reports' / f'package-v{version}-validation.log')
    print(json.dumps({key: value for key, value in report.items() if key not in ['payload_files', 'sources']}, indent=2))


def main(compile_only=True):
    if SELF_PROFILE not in ('default', 'homebrew'):
        raise ValueError('MC_PS4_SELF_PROFILE must be default or homebrew')
    if not compile_only and SELF_PROFILE != 'default':
        raise RuntimeError('Package builds require the default SELF profile: homebrew failed the v4 and v6 console trials')
    preflight = audit('ps4')
    if not preflight['passed']: raise RuntimeError('PS4 preflight failed; see reports/preflight-ps4.json')
    version = next_version(); work = ROOT / 'build/ps4' / f'v{version}'; work.mkdir(parents=True, exist_ok=True)
    fingerprint, inputs = input_fingerprint()
    elf = compile_elf(work)
    if compile_only:
        self_file = work / 'eboot.bin'
        self_authinfo = make_self(elf, work, self_file, preflight)
        report = {'date': datetime.datetime.now(datetime.timezone.utc).date().isoformat(), 'target': 'PS4 cross-build only', 'hardware_tested': False, 'pkg_built': False,
                  'self_authinfo': self_authinfo,
                  'source_sha256': fingerprint, 'compiled_units': [p.relative_to(ROOT).as_posix() for p in source_files()],
                  'artifacts': {p.name: {'path': p.relative_to(ROOT).as_posix(), 'bytes': p.stat().st_size, 'sha256': digest(p)}
                                for p in [elf, self_file, work / 'ps4_media_center.oelf']},
                  'package_policy': 'No PKG by default. --package is a separate explicit operation.',
                  'preflight_checks': len(preflight['checks'])}
        (ROOT / 'reports/project-build.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2)); return
    package(elf, work, version, fingerprint, inputs, preflight)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); group = parser.add_mutually_exclusive_group()
    group.add_argument('--compile-only', action='store_true')
    group.add_argument('--package', action='store_true', help='Explicitly build and verify a PKG')
    try: main(not parser.parse_args().package)
    except Exception as error:
        (ROOT / 'reports').mkdir(exist_ok=True)
        (ROOT / 'reports/ps4-build-error.txt').write_text(str(error) + '\n')
        print(str(error), file=sys.stderr); raise SystemExit(1)
