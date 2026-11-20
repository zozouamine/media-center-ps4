# Building

## Inspected environment

| Component | Actual installation |
| --- | --- |
| Computer | macOS 12.7.6, Intel x86_64 |
| Host compiler | AppleClang 14.0.0.14000029 |
| PS4 compiler/linker | LLVM/Clang/LLD 18.1.8, `/usr/local/opt/llvm/bin` |
| Inspected SDK | `/Users/youssouf/OpenOrbis-PS4-Toolchain-v0.5.4` |
| Build tools | CMake 4.4.4, Ninja, Python 3.12.5 |
| Public package builder | Project-local PkgTool / LibOrbisPkg; source in `tools/pkgtool-source/` |
| .NET for package builder | SDK 8.0.427, runtime 8.0.33 |
| Host fixture dependencies | Pillow 11.3.0, NumPy 2.2.6, PyAV 18.1.0 |

The SDK directory name is not independent proof of a v0.5.4 release tag; its
CHANGELOG starts at v0.5.2. Exact inspected artifact hashes are retained in
`reports/environment-audit.json`. Adjacent projects and the older `/opt` SDK
were not modified.

## Commands

```sh
./preflight.sh --host --tests
./build.sh --host
./build.sh --test
./build.sh --sanitize
./build.sh --ps4
./build.sh --pkg
```

`./build.sh` defaults to `--project`: host build/tests and PS4 ELF/FSELF, without
PKG. `--all` is an alias. `MC_BUILD_JOBS` controls compiler concurrency (default
4). The native GUI is supplied for macOS; the portable core/tests remain separate
from Cocoa. No native Linux GUI is included.

PS4 artifacts are generated in `build/ps4/vN/`. The production build compiles all
application units plus the pinned libwebp, libtiff and zlib source lists, links an
x86-64 FreeBSD-targeted PIE ELF, inspects it with llvm-readelf and produces a real
OpenOrbis FSELF. This is compilation evidence, not console execution evidence.

PS4 builds default to the zero authentication record used by the user-tested v5
startup baseline. v4 and v6 with the explicit public Homebrew record both failed
before the interface; v6 also fails with USB disconnected. Package builds now
require `MC_PS4_SELF_PROFILE=default`. `homebrew` remains a compile-only diagnostic
option. The selected record and program authentication ID are verified before
packaging and after extraction, and the profile stays in the fingerprint and USB
diagnostics. No signing setting alone proves USB visibility on a console.
See `reports/ps4-v6-startup-investigation.md`.

On PS4, storage discovery starts in the existing directory worker after the first
successful frame. Before probing, the worker requests current-process filesystem
access through the official GoldHEN SDK. The pinned SDK reference is in
`vendor/goldhen-sdk/`, commit `bcea3c7ef01dac6d7f9f49ebf9e90fe66d86f5f7`.
The restricted adapter uses indirect syscall 0 with request 500 and only commands
0 (version), 2 (process access), and 3 (restore). Its 72-byte backup layout is
checked against the upstream header. Refresh preserves an active backup; main's
cleanup scope restores it after all application/platform workers and handles stop.
FTP sources and network code are removed.

The v8 console trial returned the known SDK version `0x100` with carry set,
reported by the generic error conversion as `-1 / errno=256`. v9 accepts that
exact value only for the version query. Every access/restore error and every
other version-query error still uses strict POSIX conversion. The result decoder
is application code and introduces no loader dependency. USB discovery also
excludes readable empty-slot directories when their device matches `/mnt`;
failure to inspect the parent does not block readable child probes.

A best-effort startup trace appends up to 512 records per run to
`/data/PS4MediaCenter/startup.log`, covering platform setup, the first frame and
USB probe stages. Previously denied logging and settings are retried after access
is enabled. If the log was inaccessible initially, it contains the later recovery
and USB probes rather than the unavailable early records. Source discovery uses
the native libc open path and reports its numeric errno. The
v6 `sceKernelOpen` import has been removed, and the ABI audit rejects its return.
USB diagnostics also report which operation prevented creation or flushing of
the private startup log. Logging failure does not prevent startup. USB handles remain
read-only; the trace uses the fixed private app directory. An absent log alone
does not prove that the executable failed to enter main, since private-data access
may also have failed.

```sh
python3 tests/verify_startup.py --binary build/host/ps4-media-center --report reports/direct-usb-startup-validation.json
python3 tools/verify_ps4_self.py --self build/ps4/v9/eboot.bin --profile default
```

The startup test uses the host-only `--defer-scan` and `--mount-base` options to
verify that the UI appears and accepts input before discovery, failed discovery
leaves diagnostics available, and deferred discovery preserves output boundaries.

`ps4-source-probe-tests` compiles the production PS4-only source-open branch on
the host with real temporary directory handles. No native-open mock is linked,
so reintroducing that dependency fails the test build. It checks missing paths,
wrong types, symlink rejection, descriptor zero, source identity and Arabic paths.
The cross-build separately checks the real SDK ABI and loader imports. These
computer tests do not establish access to the PS4 process's namespace.

`ps4-direct-usb-tests` runs production discovery with a simulated GoldHEN
namespace and real host descriptors. It verifies SDK rejection/retry, exact access
restoration, Refresh, newly visible mounts, native media reads, Arabic paths,
source identity, cancellation and read-only boundaries. `ps4-private-io-tests`
checks safe absolute-path recovery from the reported openat EPERM and later
logging/settings recovery. The cross-build independently inspects the actual
SDK syscall instructions and rejects network imports. Release validation also
checks that test hooks are absent from the production ELF.

## Package production

`--pkg` is explicitly authorized for this delivery. It creates `param.sfo`,
`icon0.png`, a correctly populated GP4 directory tree, `eboot.bin`, assets and
license/source notices, then calls the public package builder.

The package uses homebrew fake-signing conventions. Validation checks signatures,
entry/PFS hashes and extracted files against the exact staged payload. A final
`PKG Build/ps4_media_center_vN.pkg` is created exclusively only after those checks
pass. The next number is greater than every existing package version. Hashes,
sizes, payload files and logs are retained in `reports/package-vN.json` and its
SHA-256/validation companions. Installation is not performed by the build.

## Different tool installations

```sh
export MC_OPENORBIS_SDK='/path/to/OpenOrbis-PS4-Toolchain'
export MC_LLVM_BIN='/path/to/llvm/bin'
export DOTNET='/path/to/dotnet'
./preflight.sh --ps4
```

`OO_PS4_TOOLCHAIN` and `MC_PKGTOOL` are also supported. The build includes Clang's
resource headers, the inspected musl headers and the required POSIX/GNU flags
with `-nostdinc`. It links the SDK's libc/C++/kernel/VideoOut/Pad/UserService/AudioOut
libraries. No proprietary SDK or retail private signing key is used.

## Offline dependencies and fixtures

Pinned sources, licenses and UI assets are included; ordinary builds do not
download dependencies. Their manifests are verified by preflight. libwebp uses a
supplied memory-budget patch, libtiff uses generated 64-bit little-endian config
headers and application-owned memory-only callbacks, and zlib supports TIFF
Deflate. External TIFF JPEG/JBIG/LERC/LZMA/WebP/ZSTD dependencies are disabled.

Video additionally builds FFmpeg 7.1.3 and dav1d 1.5.1 from vendored sources.
The host needs make, Meson, Ninja, NASM and pkg-config. `tools/build_ffmpeg.py`
builds static host/PS4 libraries and caches them only for matching source bytes
and options. A temporary verified project symlink avoids spaces in FFmpeg and
pkg-config paths. OpenOrbis uses explicit cross compilers/strip tools and disables
dav1d's optional affinity/thread-name APIs because the SDK exposes incompatible
musl-shaped interfaces. The supplied FFmpeg `libavutil/cpu.c` patch avoids
unavailable PS4 sysctl/affinity queries and uses the four-worker decoder policy.
The `img2.h`/`img2dec.c` patch omits PS4 filename glob expansion, whose SDK
implementation imports passwd/NSCD socket calls; selected-file video decoding
and container support are retained.
FFmpeg POSIX clocks use the native application adapter. The build validates object
headers and static archive members and rebuilds invalid interrupted-build output.
No external codec package is downloaded or loaded at runtime.

```sh
python3 tools/fetch_dependencies.py
python3 tools/prepare_assets.py
python3 tools/generate_ui_assets.py
python3 tools/prepare_pkgtool.py
python3 tests/generate_fixtures.py
python3 tests/verify_video.py --binary build/host/ps4-media-center
```

Asset preparation needs Pillow/fontTools; fixture generation additionally needs
NumPy/PyAV and the exercised encoders. Generated test media are not packaged.
They are regenerated from supplied source in a clean delivery checkout; bulky
Large/Deep fixture trees and CMake caches are excluded from the sibling delivery.
