#!/usr/bin/env python3
"""Inspect actual build tools, headers, libraries and pinned assets."""
import argparse
import datetime
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sdk_path():
    if os.environ.get('MC_OPENORBIS_SDK'):
        return Path(os.environ['MC_OPENORBIS_SDK']).expanduser().resolve()
    newer = Path.home() / 'OpenOrbis-PS4-Toolchain-v0.5.4'
    if newer.is_dir(): return newer
    return Path(os.environ.get('OO_PS4_TOOLCHAIN', '/opt/OpenOrbis-PS4-Toolchain')).expanduser().resolve()


def llvm_bin():
    if os.environ.get('MC_LLVM_BIN'): return Path(os.environ['MC_LLVM_BIN']).expanduser().resolve()
    if Path('/usr/local/opt/llvm/bin/clang++').is_file(): return Path('/usr/local/opt/llvm/bin')
    clang = shutil.which('clang++')
    return Path(clang).parent if clang else Path('/missing-llvm')


def dotnet_path():
    if os.environ.get('DOTNET'): return Path(os.environ['DOTNET']).expanduser()
    local = Path.home() / '.dotnet/dotnet'
    return local if local.exists() else Path(shutil.which('dotnet') or '/missing-dotnet')


def pkg_command():
    explicit = os.environ.get('MC_PKGTOOL')
    path = Path(explicit).expanduser() if explicit else ROOT / 'tools/pkgtool/PkgTool.Core.dll'
    return [str(dotnet_path()), str(path)] if path.suffix == '.dll' else [str(path)]


def audit(mode='all', tests=False):
    problems = []; checks = []
    def check(name, condition, detail):
        checks.append({'name': name, 'passed': bool(condition), 'detail': str(detail)})
        if not condition: problems.append(f'{name}: {detail}')
    for manifest_name in ['vendor/manifest.json', 'vendor/video-codecs-manifest.json', 'assets/fonts/manifest.json', 'assets/ui/manifest.json', 'licenses/manifest.json']:
        path = ROOT / manifest_name
        check(manifest_name, path.exists(), path)
        if path.exists():
            for entry in json.loads(path.read_text()):
                file = ROOT / entry['file']; check(entry['file'], file.is_file() and digest(file) == entry['sha256'], 'Pinned SHA-256')
    check('Application icon', (ROOT / 'sce_sys/icon0.png').is_file(), ROOT / 'sce_sys/icon0.png')
    check('Project license', (ROOT / 'LICENSE').is_file(), ROOT / 'LICENSE')
    report = {'date': datetime.datetime.now(datetime.timezone.utc).date().isoformat(), 'target': mode, 'host': platform.platform(), 'checks': checks, 'problems': problems, 'hardware_tested': False}
    if mode in ['all', 'host']:
        for tool in ['cmake', 'ninja', 'python3', 'make', 'meson', 'nasm', 'pkg-config']:
            check(tool, shutil.which(tool), shutil.which(tool) or 'Missing executable')
        if tests:
            for name in ['PIL', 'numpy', 'av']:
                try: module = __import__(name); check(name, True, getattr(module, '__version__', 'available'))
                except ImportError: check(name, False, 'Install host test-only Python dependency')
    if mode in ['all', 'ps4']:
        for tool in ['make', 'meson', 'nasm', 'pkg-config']:
            check('decoder/' + tool, shutil.which(tool), shutil.which(tool) or 'Missing executable')
        sdk = sdk_path(); llvm = llvm_bin(); os_bin = 'macos' if sys.platform == 'darwin' else 'linux'
        fself = sdk / 'bin' / os_bin / ('create-fself-macos' if sys.platform == 'darwin' and (sdk / 'bin/macos/create-fself-macos').exists() else 'create-fself')
        gp4 = sdk / 'bin' / os_bin / 'create-gp4'
        for path in [llvm / 'clang++', llvm / 'clang', llvm / 'ld.lld', fself, gp4, dotnet_path(), Path(pkg_command()[-1])]:
            check(path.name, path.is_file() and (path.suffix == '.dll' or os.access(path, os.X_OK)), path)
        for name in ['lib/crt1.o', 'lib/libc.a', 'lib/libc++.a', 'lib/libc++abi.a', 'lib/libunwind.a', 'lib/libkernel.so',
                     'lib/libScePad.so', 'lib/libSceUserService.so', 'lib/libSceAudioOut.so', 'lib/libSceVideoOut.so', 'link.x',
                     'include/orbis/VideoOut.h', 'include/orbis/Pad.h', 'include/orbis/AudioOut.h', 'include/pthread.h',
                     'samples/hello_world/sce_module/libc.prx', 'samples/hello_world/sce_module/libSceFios2.prx']:
            check(name, (sdk / name).is_file(), sdk / name)
        manifest = ROOT / 'tools/pkgtool-manifest.json'
        if manifest.exists():
            for name, entry in json.loads(manifest.read_text())['files'].items():
                path = ROOT / 'tools/pkgtool' / name
                check('PkgTool/' + name, path.exists() and digest(path) == entry['sha256'], 'Project-local tool hash')
        try:
            clang = subprocess.run([str(llvm / 'clang++'), '--version'], capture_output=True, text=True, timeout=20)
            check('Upstream Clang', clang.returncode == 0 and 'Apple clang' not in clang.stdout, clang.stdout.splitlines()[0] if clang.stdout else clang.stderr)
            package = subprocess.run(pkg_command() + ['version'], capture_output=True, text=True, timeout=20)
            check('PkgTool execution', package.returncode == 0, package.stdout.strip() or package.stderr.strip())
        except (OSError, subprocess.TimeoutExpired) as exc: check('Tool execution', False, exc)
        report.update(sdk=str(sdk), llvm=str(llvm), fself=str(fself), gp4=str(gp4), pkg_command=pkg_command())
    report['passed'] = not problems
    (ROOT / 'reports').mkdir(exist_ok=True)
    (ROOT / 'reports' / f'preflight-{mode}.json').write_text(json.dumps(report, indent=2) + '\n')
    for problem in problems: print('FAIL ' + problem, file=sys.stderr)
    print(f"Preflight {mode}: {'PASS' if not problems else 'FAIL'} ({len(checks)} actual checks)")
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); group = parser.add_mutually_exclusive_group()
    group.add_argument('--host', action='store_true'); group.add_argument('--ps4', action='store_true'); parser.add_argument('--tests', action='store_true')
    args = parser.parse_args(); result = audit('host' if args.host else 'ps4' if args.ps4 else 'all', args.tests)
    raise SystemExit(0 if result['passed'] else 1)
