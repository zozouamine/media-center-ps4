# Media Center

A native, lightweight media browser for PS4 homebrew, with an English interface
and UTF-8/Arabic filenames, search and file metadata. The macOS preview runs the
same renderer, browser and decoders in a real Cocoa window with CoreAudio.

Current build results, package hashes and console limitations are recorded in
[BUILD_REPORT.md](BUILD_REPORT.md). User-reported console trials are recorded
separately from computer checks. The assistant has not operated a PS4 or emulator;
cross-compilation and PKG validation do not establish console startup stability.

## Implemented behavior

- Four distinct colored tabs: **USB Files / Videos / Music / Photos**. Media
  categories enter the source's `Videos`, `Music` or `Photos` folder and filter
  the corresponding media type; subfolders remain browsable.
- USB Files shows all readable folders/files. Nonmedia entries show their name,
  size and **Cannot open**, with full information available from Details.
- PlayStation header logo, a smaller title and smaller text throughout the UI.
  Storage name and available space are queried from the source, never invented.
- JPEG, PNG, BMP, GIF, WebP, TGA, PSD, HDR, PNM, TIFF and ICO viewing, including
  bounded GIF/WebP animation. See [SUPPORTED_FORMATS.md](SUPPORTED_FORMATS.md)
  for verified variants, compression limits and unavailable formats.
- WAV, MP3, FLAC, Ogg Vorbis, AAC, M4A, Opus, WMA and AIFF playback with real progress, previous/next,
  pause/play, stop, volume and ±10-second seek. Compact rounded buttons use
  original transport icons. Embedded album artwork appears inside its frame.
- A visible music **Back** button returns to the browser while audio continues;
  **Stop** stops audio and returns. Repeat, shuffle and playlists work.
- MP4, MKV, AVI, MOV, WebM, MPEG/MPG and TS/M2TS video through static FFmpeg,
  with H.264, HEVC, AV1, VP8/VP9 and decoded companion audio. Playback uses real
  presentation timestamps; pause and seek retain audio/video synchronization.
  Six compact rounded icon buttons
  provide previous/next, ±10-second seek, play/pause/replay and stop, with Back,
  Options and aspect icons beside them. Every video button centers its icon without
  a text label. The bottom gradient preserves the
  video behind it. Controls hide after seven seconds without intentional input.
- Photo zoom, rotation, fit/original, slideshow and animation pause/resume.
- Arabic/Latin filename search, favorites, history, queues and private settings.
- Read-only source access, bounded directory pages, background workers and safe
  cancellation/disconnect handling. There are no USB file mutation actions.

## Interactive preview

```sh
./build.sh --host
python3 tests/generate_fixtures.py
build/host/ps4-media-center --source tests/generated/usb-demo \
  --select 'Sample Video.avi' --data-dir build/live-preview-state
```

The window stays open until closed or Exit application is selected. Mouse clicks
operate tabs and player buttons; double-click opens list entries. Arrow keys or
WASD navigate, Enter/Space selects, Esc returns, T opens Options, Q opens Details,
[ / ] selects the previous/next tab or track, and 1 / 2 seeks ±10 seconds.

`./Preview.command` opens the interactive preview and prepares local fixtures if
needed; `./Preview.command --video` opens paused video with its real transport
controls. `usb-demo` contains real generated test media on the computer volume;
it is not a physical USB drive and is excluded from the PS4 package. To browse a
real mounted host drive, use `--source '/Volumes/your drive'`.

## Build and verification

```sh
./build.sh --test
./build.sh --sanitize
./build.sh --ps4
./build.sh --pkg
```

The default `--project` and `--ps4` build ELF/FSELF without packaging. `--pkg`
explicitly builds a new version, validates the real PKG and checks extracted
payload hashes before copying it into **PKG Build/**. Existing packages are never
replaced.

## Storage and evidence

The corrected current project is this working directory, **Ps4 Media Center**,
as chosen by the user. The earlier case-sensitive sibling **PS4 Media Center**
retains its previous source; it was not synchronized during this fix.

The earlier v8 trial opened the interface, but its
SDK diagnostic reports `result=-1, errno=256` and `/mnt/usb1` reports ENOENT,
while PS4-Xplorer reads the same exFAT drive in Root / Jailbroken mode on
PS4 12.52 / GoldHEN 2.4b18.12. See
[the v8 USB report](reports/ps4-user-usb-v8.json).

The user subsequently tested v9 and reached video playback, where the old build
rejected MP4/MKV/H.264/HEVC because only its AVI decoder was linked. The new video
path replaces that limitation. See [video verification](reports/video-v10-application-validation.json)
and [format details](SUPPORTED_FORMATS.md).

The next package uses the display name **Media Center**, Title ID **AMIN00048**
and the matching content ID `IV0000-AMIN00048_00-MEDIACENTER00001`.

v10 passes **80 test groups / 14966 assertions**, normally and under application
core ASan/UBSan; **66 shared application checks** and **30 encoded-video checks**
cover 27 real files, presentation timestamps, companion audio, pause and seek.
All **120 PS4 units**, **42 ABI checks**, package signature/hash verification and
**58 extracted payload files** pass. The bundled codec source archive matches
the production inputs. Physical PS4 playback/performance remains to be tested.
See [v10 release validation](reports/package-v10-release-validation.json).

The storage worker uses the official
GoldHEN SDK to enable this process's filesystem access before probing USB. Files
are read directly through native read-only handles. FTP and networking are removed
from the application; enabling GoldHEN's FTP service is unnecessary.

v9 accepts the exact GoldHEN SDK version reply `0x100` that v8 converted to
`errno=256`; all access/restore errors remain strict. It also rejects readable
empty USB-slot placeholders on the same device as `/mnt`. Normal and ASan/UBSan
validation, all 66 shared application checks, the 119-unit PS4 cross-build,
24 ABI checks and the 51-file package round trip pass. The latest v9 user report
reaches video selection and identifies its decoder restriction; the agent has
not performed console testing. Previous packages are preserved. See
[release verification](reports/package-v9-release-validation.json).

With GoldHEN running, install v10 and connect the USB drive. Choose Refresh if it
was connected after launch. Square on an empty Sources screen opens USB
diagnostics, including the SDK result and each mount's actual error.

The worker probes `/usb0` through `/usb7` and `/mnt/usb0` through `/mnt/usb7`
after the first frame, then enumerates `/mnt`; it does not mount or format drives.
The inspected SDK provides no usable
friendly USB-label API: the actual mount identifier is the honest fallback.
Free space uses native-layout `fstatfs`; macOS also reads the volume name with `fgetattrlist`.
PS4 mount names, permissions and filesystem behavior still require console tests.

Private settings are stored in `/data/PS4MediaCenter`. Clearing internal app data
requires confirmation and only removes the application's own state files.

- [Design comparison](reports/design-comparison.md): reference versus actual UI.
- [BUILDING.md](BUILDING.md): tools and reproducible build commands.
- [TECHNICAL_DESIGN.md](TECHNICAL_DESIGN.md): APIs, architecture and bounds.
- [SUPPORTED_FORMATS.md](SUPPORTED_FORMATS.md): actual tested format variants.
- [CONTROLLER_MAP.md](CONTROLLER_MAP.md): controller, keyboard and mouse actions.
- [TEST_PLAN.md](TEST_PLAN.md): evidence and pending console verification.
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md): source and licenses.
