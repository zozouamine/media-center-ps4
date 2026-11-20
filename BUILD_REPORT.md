# Build report — PS4 Media Center

Date: 2026-11-18 (Africa/Algiers). Source workspace:
`/Users/youssouf/Desktop/openCode Project/Ps4 Media Center`.
Earlier separate delivery:
`/Users/youssouf/Desktop/openCode Project/PS4 Media Center`.
The user adopted the corrected working project above as the current project.

## Current v10 video playback release

The user tested v9 and reached its AVI-only video rejection. The new candidate is
[ps4_media_center_v10.pkg](PKG%20Build/ps4_media_center_v10.pkg), APP_VER **10.00**,
Title ID **PMCE00001**, **45,088,768 bytes**. SHA-256:
`5e0ee07b680c128f04bf6dc7a0bc8134cf4a2ddf3de5c025ea90ab2bbafe6f2a`.
Source fingerprint: `0cd63b3f01f2eca435c9abcd9737b1924a7802123be28b64ea064f2f12156417`.

Production video now links static FFmpeg 7.1.3, dav1d 1.5.1 and zlib 1.3.1,
with the configured built-in software decoders, demuxers and parsers. Actual
encoded playback covers MP4/MKV/MOV/AVI/WebM/MPEG-PS/TS/M2TS/FLV/ASF/3GP/RM;
H.264, HEVC 8/10-bit, AV1, VP8/VP9, MPEG-4/MPEG-2, ProRes, FFV1, HuffYUV,
MJPEG, PNG and RV20, with AAC/AC3/EAC3/MP2/MP3/WMA/Vorbis/Opus/ALAC/FLAC/PCM
and stereo downmix of 5.1 audio. See [format details](SUPPORTED_FORMATS.md).

Frames follow decoded presentation timestamps, including B frames and VFR.
Audio follows consumed 48 kHz stereo samples, with stream offsets, resampler
delay and negative encoder preroll handled. Seek selects the preceding stream
tick and decodes video/audio preroll; returning to zero reopens the container
so TS/PS retains its first GOP. Bounded incomplete-frame recovery restores audio
following a mid-PES seek. Paused seeks remain paused and flush output buffers.
Changing-frequency AAC/44.1 kHz PCM references verify near and distant audio
seeks within one millisecond, including fractional resampler phase. A genuine
3840×2160 HEVC 10-bit fixture decodes into a 1920×1080 display buffer.

Validation passes **80 groups / 14966 assertions**, normally and with application
core ASan/UBSan instrumentation. The separately built FFmpeg/dav1d libraries are
not sanitizer-instrumented. All **66 shared application checks** and **30 encoded
video application checks** pass, covering 27 files, pause, seek, audible PCM,
sample-clock EOF and unchanged source hashes. **12 deferred startup checks** pass.
All **120 PS4 units** compile and **42 production ABI checks** pass. The final
ELF matches the validated cross-build; its default SELF profile is preserved.
Package signature/hash validation and all **58 staged/extracted files** pass.
The bundled corresponding-source archive matches every production input and
includes the codec sources, licenses, build scripts and host test sources/stubs.

The original v9 package retains its recorded SHA-256. Actual v10 console
playback and software-decoder throughput, especially 4K/HEVC/AV1, require a
physical PS4 trial; the agent has not operated a console or emulator. See
[release validation](reports/package-v10-release-validation.json) and
[encoded application playback](reports/video-v10-application-validation.json).

## Retained v9 construction and USB correction

The retained v9 package is [ps4_media_center_v9.pkg](PKG%20Build/ps4_media_center_v9.pkg),
APP_VER **09.00**, Title ID **PMCE00001**, **7,864,320 bytes**. SHA-256:
`e41e95a7acb06994312369f3b1d85e7f2ba8a85e8ac70703343158f3f6768f2b`.
Source fingerprint:
`2957bdb845a4588e0310568f3e33a3c699ba46a40483caf67d13d4358113bc56`.

The explicit v8 console trial opened the interface but reported `SDK=0x00000000,
result=-1, errno=256`; `/mnt/usb1` returned ENOENT. The error-channel value 256
is the pinned GoldHEN SDK version `0x100`, so v8 rejected a valid version reply
before issuing process-access command 2. v9 accepts only this exact value for
command 0, including with carry set. Access command 2, restoration command 3 and
all other errors retain strict failure handling. Discovery also excludes readable
empty USB-slot placeholders whose device matches `/mnt`.

Files are still opened through native read-only descriptors. FTP, network imports,
firmware offsets and a new exploit are absent. The default SELF record that opens
on the user's console remains. v9 passes **74 test groups / 11179 assertions** and
**12 startup checks**, both normally and under ASan/UBSan; **66/66 shared
application checks** pass in a sequential run. All **119 PS4 units** compile and
all **24 ABI checks** pass. Package signature/hash validation and all **51 staged
versus extracted files** pass. The v6, v7 and v8 release packages and retained
v1–v8 internal packages keep their recorded hashes. See
[release validation](reports/package-v9-release-validation.json).

These computer and construction checks do not establish that v9 sees the drive
on PS4. That result requires the user's physical trial described in TEST_PLAN.md.

## Retained v7 startup correction and earlier console evidence

The user explicitly confirmed that **v6** fails before any interface appears with
**CE-34878-0**, including with USB disconnected. `/data/PS4MediaCenter` exists but
is empty. PS4-Xplorer reads the exFAT drive at `/mnt/usb1` in **Root / Jailbroken**
mode on PS4 **12.52 / GoldHEN 2.4b18.12**. v5 opened and its USB diagnostics worked,
but all probed USB paths returned errno 2. These are user reports; no console has
been operated by the agent. See [the v6 record](reports/ps4-user-crash-v6.json) and
[the USB record](reports/ps4-user-usb-v5.json).

The correction restores the default SELF record and v5's source-open imports.
v6's only new dynamic import, `sceKernelOpen`, is removed. Package builds reject
the known failing Homebrew record, and the ABI audit checks that the old `_open`
path is retained. Diagnostics now show the operation and errno that prevented
creation or flushing of the private startup log. Deferred discovery and all
existing media/UI behavior remain. The exact console fault is still unconfirmed.

The retained candidate at that stage is [ps4_media_center_v7.pkg](PKG%20Build/ps4_media_center_v7.pkg),
APP_VER **07.00**, Title ID **PMCE00001**, **7,798,784 bytes**. SHA-256:
`b3c943f86c11e20e58b506c7f8452a59988596b508ffe38dac3bb3ae7dfa5fc7`.
Source fingerprint: `d7489f110f222b6187749824b92b868337d0d7cf37ff82b8195800e3643fa1d3`.

Validation passes **35 core groups / 10277 assertions**, **7 compatibility groups /
83 assertions**, **6 production-source simulation groups / 32 assertions**, and
**12 startup checks**, both normally and under ASan/UBSan. **10 SELF/packaging
checks** and **20 ABI checks** pass. All **115 PS4 units** compile. The package's
**50 extracted files** match the stage, and its ELF/eboot match the previously
validated cross-build. The existing v6 release and six retained internal package
copies are unchanged. See [release validation](reports/package-v7-release-validation.json).

v7 has not been tested on the console. USB access has not been
corrected by this startup rollback; Xplorer's Root mode is evidence of a different
access context, not proof that this application's sandbox can reach the drive.
See [the investigation](reports/ps4-v6-startup-investigation.md).

## Retained v6 construction evidence; console startup failed

The preserved v6 package is `PKG Build/ps4_media_center_v6.pkg`, APP_VER `06.00`,
7,798,784 bytes, SHA-256
`adfcc85b09e6cf4d2c93cf9a969793b8b95a06ccedf6e3f0a6d5f55400cc6f79`.
The old release's successful computer tests, ELF, staged files and extracted
payload are retained in [v6 release validation](reports/package-v6-release-validation.json).
They do not prove launch, USB discovery or playback on the target console.

The user tested **v1** on physical **PS4 12.52 / GoldHEN 2.4b18.12**, with
**exFAT USB**. The interface appeared, then the title closed with **CE-34878-0**;
USB was not detected. This is user-reported hardware evidence. The agent did not
run the console. The user subsequently reported that the latest package's startup
crash stopped, but USB discovery still failed after Refresh while PS4-Xplorer read
the same drive normally. This trial is v3 in the conversation context; its exact
tested filename/hash was not independently supplied.

v4 supplies the title-visible `/usb0` through `/usb7` roots as well as the global
`/mnt/usb0` through `/mnt/usb7` paths. Its SELF includes the explicit public
Homebrew authentication profile used by Payload Guest and GoldHEN Cheat Manager.
The profile is verified in both the staged and extracted executable. The empty
Sources screen now offers Square → USB diagnostics, with per-path numeric errors
and Up/Down scrolling. No new kernel syscall or USB mutation is introduced.

The installed libc++ timed wait compared the normal FreeBSD timeout **60** with
Linux's **110**, which can terminate a background worker. The source now supplies
the target-compatible wait/notify methods and native clocks. Filesystem adapters
avoid ENOSYS stubs and realpath, decode native stat/statfs fields, and probe USB
children before listing /mnt. Settings use native operations only in the fixed
private app directory. USB access remains read-only.

See the [USB investigation](reports/ps4-usb-v3-investigation.md) and
[earlier runtime investigation](reports/ps4-runtime-investigation.md).
The native errno behind the reported v3 USB failure was not available. v4's
physical trial failed at startup, so it did not establish USB discovery either.

## Retained v4 computer and cross-build evidence

- **35 core groups / 10277 assertions pass**, with the replacement condition-variable
  methods linked into the tests: browsing, cancellation, media controls, EOF/replay,
  Arabic file data, persistence and read-only source boundaries.
  New coverage includes a readable sandbox root beside a denied global namespace,
  empty-source Refresh, numeric probe errors and scrolling through all diagnostics.
- **6 compatibility groups / 70 assertions pass**: normal errno-60 timeouts,
  timed wakeup/cancellation, files over 4 GiB, timestamps and native filesystem strings.
- **ASan and UBSan pass both suites**, with no reported sanitizer errors.
- **66/66 shared application checks pass**, including icon-only video controls,
  music Back/Stop, actual decoded frames/PCM, source metadata and Arabic names.
- **114 PS4 units cross-compile and link** into a real ELF/OELF/FSELF. All
  **19 ABI checks pass**. The linked timed wait compares 0x3c; incompatible
  condition-variable/chrono archive objects and filesystem stubs are absent.

Normal tests, sanitizer tests and application checks were run sequentially.
Earlier failed icon-only runs are preserved; they are superseded by these current
passing results. No additional console or emulator success is inferred.

Evidence: [core tests](reports/usb-v4-core-tests.json),
[compatibility tests](reports/usb-v4-runtime-tests.json),
[sanitizers](reports/usb-v4-sanitizer-results.json),
[application checks](reports/usb-v4-host-validation.json),
[cross-build](reports/usb-v4-cross-build.json) and
[ABI audit](reports/ps4-runtime-abi-audit.json).

Retained v4 ELF: `build/ps4/v4/ps4_media_center.elf` — **3,224,592 bytes**.
SHA-256: `6cb5f36b051afddbdf795e07bbfbc5955f50107596e3799f84e45ea743502cfe`.
Build-input SHA-256:
`d2ea5bd4ccc6f08c1ba5589a9b8d38092f6a4daaf0d70d5ebb8ef3c912c2aede`.
Those build inputs matched v4's source fingerprint at release. The current
source is the separately validated v5 candidate described above.

## Historical packages — previous versions preserved

**The user's new build request resumed production.** v4 was built from the same
validated source and ELF. All signature/hash checks pass; all **50 extracted payload
files** match their staged originals. The previous packages remain unchanged.
The user-reported console result of v4 is **FAIL before the first interface**.
v3 has the partial user-reported startup/USB result described above.

| Existing package | APP_VER | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| ps4_media_center_v1.pkg | 01.00 | 7,536,640 | a2d54d65f3dba069f18404c2522f823ae6441ed47221fafca33cfccbaf5cc5c3 |
| ps4_media_center_v2.pkg | 02.00 | 7,536,640 | e4c40b8c33affc89b7fb7d7cf076f31d2dad96bc41799c7d5f217466a81462cc |
| ps4_media_center_v3.pkg | 03.00 | 7,798,784 | f924e9eef015461797d31d02940aae8d453a68a0e5217cc3c096cdd04f628e84 |
| ps4_media_center_v4.pkg | 04.00 | 7,798,784 | 018a85acb294063234cfd1934c1eb0f1e08df223156e7d5e6887ba5c6746b8cc |
| ps4_media_center_v5.pkg | 05.00 | 7,798,784 | bc1e8287410a6b56f2caa3f12ed79d20f072ba6c944f71de7e976c89b31e86a7 |
| ps4_media_center_v6.pkg | 06.00 | 7,798,784 | adfcc85b09e6cf4d2c93cf9a969793b8b95a06ccedf6e3f0a6d5f55400cc6f79 |
| ps4_media_center_v7.pkg | 07.00 | 7,798,784 | b3c943f86c11e20e58b506c7f8452a59988596b508ffe38dac3bb3ae7dfa5fc7 |
| ps4_media_center_v8.pkg | 08.00 | 7,864,320 | cad843d3824dad655ba6462edb5f6dd41b27022d489d8e0508b0be81eef63655 |
| ps4_media_center_v9.pkg | 09.00 | 7,864,320 | e41e95a7acb06994312369f3b1d85e7f2ba8a85e8ac70703343158f3f6768f2b |

Retained v4 package: [ps4_media_center_v4.pkg](PKG%20Build/ps4_media_center_v4.pkg),
**7.44 MiB**, Title ID `PMCE00001`.
Evidence: [package-v4.json](reports/package-v4.json),
[validation log](reports/package-v4-validation.log),
[SHA-256](reports/package-v4.sha256) and
[release verification](reports/package-v4-release-validation.json).

v1 is present in the workspace and separate delivery. v2 was produced earlier,
before the final icon-only video change and the stop request, and remains in the
workspace only, as do the retained internal copies through v9. Earlier signature/hash and payload checks prove those
packages were constructed correctly; v1's reported runtime result is **FAIL**.
Historical package metadata and validation logs remain in `reports/package-vN.*`.

## Interface and practical limits

The English UI retains Arabic filenames, search and metadata. The prior requested
main-screen, USB categories, compact music controls/artwork/Back and icon-only
video transport improvements remain in the current source. Actual computer output
is captured in [video-controls-final.png](reports/host-validation/video-controls-final.png).
The main reference comparison is in [design-comparison.md](reports/design-comparison.md).
These are application-framebuffer captures on the computer, not PS4 screenshots.

Video supports bounded AVI MJPEG/RGB24 with optional PCM; MP4/MKV, H.264/HEVC,
AAC/Opus and subtitles remain unavailable. Image support is broad, including GIF,
but not universal; see SUPPORTED_FORMATS.md. No friendly USB-label API was verified:
PS4 uses the actual mount identifier and measured capacity when available.
Readable-source discovery does not depend on a successful capacity query.

Further startup observation, actual USB discovery, native timing/pad behavior,
TV readability and performance require a new physical trial in TEST_PLAN.md.
