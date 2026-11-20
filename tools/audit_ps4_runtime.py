#!/usr/bin/env python3
"""Check the linked PS4 ABI adapters; this never executes console code or builds a PKG."""
import argparse
import datetime
import json
import re
import subprocess
from pathlib import Path
from preflight import ROOT, sdk_path, llvm_bin, digest


def output(command):
    result = subprocess.run([str(item) for item in command], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr or 'ABI inspection command failed')
    return result.stdout


def audit_runtime(work, sdk, llvm):
    work = Path(work)
    checks = []
    def check(name, passed, detail):
        checks.append(dict(name=name, passed=bool(passed), detail=detail))

    extraction = (work / 'archive-extraction.tsv').read_text()
    incompatible = ['condition_variable.cpp.o', 'chrono.cpp.o', 'clock_gettime.lo', 'fstatat.lo', 'lstat.lo',
                    'realpath.lo', 'readlink.lo', 'mkdirat.lo', 'renameat.lo', 'unlinkat.lo']
    pulled = [name for name in incompatible if re.search(r'\(' + re.escape(name) + r'\)', extraction)]
    check('incompatible_archive_members_excluded', not pulled, pulled)

    objects = work / 'objects'
    wait_object = objects / 'src_platform_ps4_condition_compat.cpp.o'
    wait_disassembly = output([llvm / 'llvm-objdump', '-dr', wait_object])
    (work / 'condition-compat-disassembly.txt').write_text(wait_disassembly)
    header = (sdk / 'include/bits/errno.h').read_text()
    target_timeout = int(re.search(r'#define\s+ETIMEDOUT\s+(\d+)', header).group(1))
    check('timed_wait_accepts_target_errno_60', target_timeout == 60 and
          bool(re.search(r'cmp\w*\s+\$0x3c\b', wait_disassembly)),
          'Target ETIMEDOUT=60; adapter instruction compares 0x3c')

    elf = work / 'ps4_media_center.elf'
    imports = output([llvm / 'llvm-nm', '--undefined-only', elf])
    required_imports = ['sceKernelGetProcessTime', 'sceKernelGettimeofday', 'sceKernelStat', '_fstat', '_fstatfs',
                        'sceKernelMkdir', 'sceKernelRename', 'sceKernelUnlink']
    for name in required_imports:
        check('native_import_' + name, bool(re.search(r'\b' + re.escape(name) + r'$', imports, re.M)), name)
    check('source_open_preserves_v5_loader_imports',
          bool(re.search(r'\b_open$', imports, re.M)) and not re.search(r'\bsceKernelOpen$', imports, re.M),
          'The v5 _open import is retained; the v6 sceKernelOpen dependency is absent')
    network_imports = re.findall(r'\b(?:sceNet\w*|_?(?:socket|connect|send|recv|getaddrinfo))$', imports, re.M)
    check('native_usb_has_no_network_imports', not network_imports, network_imports)

    sdk_object = objects / 'src_platform_ps4_goldhen_sdk.c.o'
    sdk_disassembly = output([llvm / 'llvm-objdump', '-dr', sdk_object])
    (work / 'goldhen-sdk-disassembly.txt').write_text(sdk_disassembly)
    check('goldhen_uses_documented_indirect_sdk_syscall_500',
          bool(re.search(r'\bsyscall\b', sdk_disassembly)) and
          bool(re.search(r'mov\w*\s+\$0x1f4,\s*%[er]di', sdk_disassembly)) and
          bool(re.search(r'(?:xor\w*\s+%eax,\s*%eax|mov\w*\s+\$0x0,\s*%[er]ax)', sdk_disassembly)),
          'The public SDK dispatch is syscall 0 with request 500 in rdi; no kernel offsets or kexec payload')
    sdk_imports = output([llvm / 'llvm-nm', '--undefined-only', sdk_object])
    allowed_sdk_references = {'__errno_location', 'mc_goldhen_sdk_result'}
    unexpected_sdk_imports = [line for line in sdk_imports.splitlines()
                              if line.strip() and line.split()[-1] not in allowed_sdk_references]
    result_definitions = output([llvm / 'llvm-nm', '--defined-only', elf])
    local_result = bool(re.search(r'\bT mc_goldhen_sdk_result$', result_definitions, re.M))
    check('goldhen_adapter_adds_no_loader_imports', not unexpected_sdk_imports and local_result,
          {'unexpected_references': unexpected_sdk_imports, 'reply_decoder_defined_in_application': local_result})
    main_symbols = output([llvm / 'llvm-nm', '--undefined-only', '--demangle', objects / 'src_app_main.cpp.o'])
    storage_symbols = output([llvm / 'llvm-nm', '--undefined-only', '--demangle', objects / 'src_storage_read_only.cpp.o'])
    check('direct_usb_preparation_stays_in_deferred_storage_discovery',
          'prepare_direct_usb_access' not in main_symbols and 'prepare_direct_usb_access' in storage_symbols,
          'Main owns only the exit cleanup scope; the deferred storage worker requests process access')

    defined = output([llvm / 'llvm-nm', '--defined-only', elf])
    for name in ['ff_h264_decoder', 'ff_hevc_decoder', 'ff_libdav1d_decoder', 'ff_vp8_decoder', 'ff_vp9_decoder',
                 'ff_mpeg4_decoder', 'ff_mpeg2video_decoder', 'ff_mjpeg_decoder', 'ff_aac_decoder',
                 'ff_ac3_decoder', 'ff_eac3_decoder', 'ff_opus_decoder', 'ff_vorbis_decoder', 'ff_dca_decoder',
                 'ff_png_decoder', 'ff_zmbv_decoder', 'ff_tscc_decoder']:
        check('linked_video_codec_' + name, bool(re.search(r'\b' + re.escape(name) + r'$', defined, re.M)),
              'Software decoder is retained in the exact production ELF')
    check('ffmpeg_posix_clock_uses_native_adapter', bool(re.search(r'\bT clock_gettime$', defined, re.M)),
          'FFmpeg CLOCK_MONOTONIC calls use the application clock translation')
    adapter_definitions = output([llvm / 'llvm-nm', '--defined-only', objects / 'src_platform_ps4_stat_compat.c.o'])
    for name in ['stat', 'fstat', 'lstat', 'fstatat', 'fstatfs']:
        symbol = re.escape(name)
        strong = bool(re.search(r'\bT ' + symbol + r'$', adapter_definitions, re.M))
        linked = bool(re.search(r'\bT ' + symbol + r'$', defined, re.M))
        unused = not re.search(r'\b' + symbol + r'$', defined + '\n' + imports, re.M)
        # --gc-sections may remove an unused adapter, including fstatat after
        # FTP removal. Require its strong compiled definition and either that
        # exact linked adapter or no linked/imported symbol at all. The archive
        # extraction check above still rejects every incompatible SDK fallback.
        check('native_adapter_' + name, strong and (linked or unused),
              {'compiled_strong_adapter': strong, 'linked': linked, 'unused_and_unimported': unused})

    for filename, forbidden in [
        ('src_settings_store.cpp.o', ['mkdirat', 'renameat', 'unlinkat', 'realpath']),
        ('src_storage_read_only.cpp.o', ['realpath', 'readlink']),
        ('src_platform_clock.cpp.o', ['clock_gettime'])
    ]:
        imports = output([llvm / 'llvm-nm', '--undefined-only', objects / filename])
        used = [name for name in forbidden if re.search(r'\b' + re.escape(name) + r'$', imports, re.M)]
        check(filename + '_avoids_incompatible_calls', not used, used)

    clock_disassembly = output([llvm / 'llvm-objdump', '-dr', objects / 'src_platform_ps4_clock_compat.cpp.o'])
    (work / 'clock-compat-disassembly.txt').write_text(clock_disassembly)
    check('chrono_uses_native_process_and_realtime_clocks',
          'sceKernelGetProcessTime' in clock_disassembly and 'sceKernelGettimeofday' in clock_disassembly,
          'Both chrono clock entry points are rebuilt; the old chrono archive object is absent')

    report = dict(date=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  target='PS4 ELF / static ABI inspection', hardware_tested=False, pkg_built=False,
                  elf_sha256=digest(elf), sdk=str(sdk), work=str(work.relative_to(ROOT)),
                  checks=checks, passed=all(item['passed'] for item in checks))
    (ROOT / 'reports/ps4-runtime-abi-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--work', required=True, type=Path)
    arguments = parser.parse_args()
    result = audit_runtime(arguments.work.resolve(), sdk_path(), llvm_bin())
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
