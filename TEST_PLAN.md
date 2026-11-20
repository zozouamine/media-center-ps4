# Test plan and status

## Current correction

The user explicitly tested `PKG Build/ps4_media_center_v8.pkg`. The interface
opened, but diagnostics reported `result=-1, errno=256` at the SDK version query
and ENOENT for `/mnt/usb1`. The value 256 is the pinned SDK version `0x100` returned
with carry set. v9 accepts that exact reply only for command 0, then permits the
existing command 2 access request. Command 2/3 failures and every other reply stay
errors. Discovery also distinguishes a mounted USB filesystem from readable empty
slot directories by comparing each slot's device with `/mnt`.

Current validation passes **19/7/6/7/35 groups** with **725/62/32/83/10277
assertions**: **74 groups / 11179 assertions** total. The same suites and all
**12 startup checks** pass normally and under ASan/UBSan. The shared native
application passes **66/66 checks** sequentially. The build compiles **119 PS4
units**, passes **24 ABI checks**, and verifies all **51 package files** after
extraction. See `reports/package-v9-release-validation.json`. Physical v9 USB
detection remains pending.

## Retained v6 computer evidence

The v6 portable core passes **35 groups / 10277 assertions** with the rebuilt
condition-variable methods linked. The compatibility suite passes **7 groups / 83
assertions**, including errno-60 timeouts, native stat/statfs layouts and positive
native file descriptors. The actual PS4 source-open branch also passes **6 host
simulation groups / 66 assertions** using mocked `sceKernelOpen` results and real
temporary directory handles: native errors, descriptor zero, handle cleanup,
wrong entry types and read-only flags. All three suites pass ASan/UBSan.
Current reports are `reports/usb-v6-core-tests.json`, `reports/usb-v6-runtime-tests.json`,
`reports/usb-v6-source-probe-tests.json` and `reports/usb-v6-sanitizer-results.json`.
The full **66/66 shared application media checks** are retained in
`reports/startup-v5-host-validation.json`.
The runs were sequential to avoid timing interference. Earlier failed icon-only
runs are retained in `reports/host-validation-icon-only-failed.json` and
`reports/sanitizer-icon-only-failed.json`.

The console startup path also passes **12 checks**, both normally and under
ASan/UBSan: the first frame precedes discovery, controls remain usable before the
scan, readable/missing sources complete, and state/trace writes inside sources
are refused. **10 SELF checks** validate both profiles against one ELF and old
executables, verify that signing changes only the authentication record, and reject
profile/ID/signature/bounds corruption and truncation. The v6 cross-build compiles
**115 units**, passes **20 static ABI checks**, and verifies the explicit Homebrew
record. Package signature/hash checks and **50 extracted-file comparisons** pass;
all v1–v5 hashes remain unchanged. See `reports/package-v6-release-validation.json`.

The initial sanitized startup run missed 80ms scripted button pulses between slow
debug render ticks. That failed input trace is retained in
`reports/usb-v6-sanitizer/startup-input-missed.json`. Longer holds now cross a
render tick; the strict pre-0.75-second control assertion remains. All 12 checks
pass normally and under sanitizers with the production startup delay unchanged.

Coverage includes direct USB discovery when its parent is traversable but unreadable;
a readable sandbox-root fixture beside a denied global namespace; empty-source
Refresh finding a newly connected source; per-path numeric errors and all diagnostics
remaining accessible through Square and Up/Down;
full private-path rejection before any mkdir; read-only flags, source identity and symlink/escape rejection;
4205-entry folders with 128-entry paging; sorting and restored focus; deep paths
and Arabic names/search; category folder/type isolation and missing category roots;
actual source labels/free space; corrupt/unsupported inputs and exhausted decode
budgets; worker cancellation and resource release; decoded image/audio/video
pixels/samples; GIF disposal and animation delays; WebP/TIFF/ICO variants; embedded
WAV/MP3/FLAC/Ogg artwork and Arabic metadata; real music seek, previous/next, pause,
Back/background playback and Stop; repeat/shuffle; favorites/settings persistence;
and simulated source disconnect during browsing/playback/photo viewing.

The video OSD is tested exactly at 6.999999 and 7.000000 seconds. Real playback
also passes hide/reveal/hide while retaining the same session and advancing frames
and audio, including dead-zone drift and intentional stick input. Paused OSD
remains visible. Test scripts use bounded button holds long enough to cross a
render tick while preserving edge semantics.

The video transport tests click the actual shared button regions while decoding
real AVI frames and PCM. They verify pause, forward/backward seek with AVI frame
quantization, changing decoded pixels without changing sessions, previous/next,
Stop/Back resource cleanup, preserved Square aspect behavior, Options, disabled
single-video controls and noninteractive gaps. A hidden click only reveals the
OSD while frames/audio continue. Replay and error-screen Back remain clickable
after the activity timeout. The final video capture comes from a genuinely paused
and sought playback session, not an illustrative interface image.

## Native window and audio

`tests/verify_native.py` opens the real Cocoa/CoreAudio window, plays a generated
35-second track, pauses, seeks ±10 seconds and resumes. It checks the output
callback clock, decoded 512×512 artwork and resident-memory growth. The final
20-second run peaks at approximately **71.7 MiB RSS**, with about **1.5 MiB** growth
between the measured steady intervals. This is computer evidence, not a long-term
console memory or performance measurement. Output is deliberately set to zero
volume; actual CoreAudio callbacks still consume samples.

The preview uses one replaceable Core Animation image, an immutable pixel copy,
explicit transaction commits and autorelease pools. This avoids retaining successive
Cocoa drawing contexts from a manually driven event loop. No PS4 graphics behavior
was changed by this host-only fix.

Native framebuffer captures and the reference comparison are in
`reports/native-music-final.png`, `reports/interface-final.png`,
`reports/music-player-final.png`, `reports/video-player-final.png` and
`reports/reference-comparison.png`. They are real application output, not a
PS4 screenshot or emulator result. An interactive window is opened separately
for inspection and can also be launched with `Preview.command`.
`Preview.command --video` opens the updated live video player and pauses/seeks the
generated AVI so its real controls remain available for interaction.

```sh
./build.sh --test
./build.sh --sanitize
./build.sh --ps4
python3 tests/verify_native.py
```

## Retained v4 build/package evidence

All **114 production application/decoder units** cross-compile for the PS4 target
and link into a real PIE ELF/FSELF. **19 static ABI checks** pass: incompatible
archive members are excluded, the timeout instruction compares 60, native clock
and filesystem imports resolve, and private I/O avoids broken *at mutation calls.
Evidence is in `reports/usb-v4-cross-build.json` and `reports/ps4-runtime-abi-audit.json`.
**v4** adds title-visible `/usbN` paths, the verified Homebrew SELF profile and USB
diagnostics. Signature/hash checks and the **50-file extracted payload comparison
pass**. The profile bytes are checked in the staged and extracted SELF, and build
inputs still match the tested source. v1/v2/v3 are preserved. Package checks establish
construction and content integrity. The user subsequently reported that v4
closes before the interface with CE-34878-0.

## Pending PS4 runtime verification

The setup is PS4 12.52, GoldHEN 2.4b18.12 and exFAT. PS4-Xplorer accesses
`/mnt/usb1` in Root / Jailbroken mode. v8 opens but blocks at the SDK version
reply; v9 corrects that interpretation. The latest user trial used v9 and reported
the old AVI-only video restriction. v10 adds FFmpeg/dav1d software playback.
Computer and construction checks do not establish target playback performance.

1. Install `PKG Build/ps4_media_center_v12.pkg` (APP_VER 12.00, Title ID `AMIN00048`) with GoldHEN active
   and the same USB connected. Open the app and choose Refresh once.
2. If no source appears, press Square for USB diagnostics. Record the complete
   `Direct USB` line and `/usb1` and `/mnt/usb1` rows. A successful preparation
   should report `SDK=0x00000100` and `GoldHEN process access enabled`.
3. Only after browsing works, test a small folder, Arabic filenames and the
   Videos/Music/Photos category roots; then PNG/GIF/WebP, a WAV with artwork,
   and a small AVI with and without PCM audio. Then play the original failing
   MP4/MKV and small H.264/AAC, HEVC/AAC and VP9/Opus files.
4. Verify pause, seek ±10 seconds, previous/next, Back/Stop, repeat/shuffle,
   persistence and source removal. Check video OSD hide/reveal after seven
   seconds while frames/audio continue. Increase folder/media sizes gradually.
5. Test paused forward/backward seeks, delayed audio, variable frame rates,
   stereo/multichannel tracks and end/replay on the new formats. Check 1080p
   before evaluating 4K/10-bit HEVC and AV1 software-decoding performance.

## v11 media validation

`tests/generate_video_fixtures.py` produces actual MP4/MKV/MOV/AVI/WebM/PS/TS
files with H.264, HEVC 8/10-bit, AV1, VP8/VP9, MPEG-4/MPEG-2,
FFV1, HuffYUV, ProRes, MJPEG and RV20. Audio includes AAC, AC3/EAC3, MP2/MP3,
WMA, Vorbis, Opus, ALAC, FLAC, PCM and a 5.1 downmix. Generator and fixture
hashes, encoder versions and decoded frame/PCM references are retained.

Portable tests compare presentation-time pixels before/after seek, verify VFR
frame holds, exact resampling against a separately concatenated reference,
delayed audio/silence, paused seeking, volume, 4K scaling, cancellation,
corruption and EOF cleanup. `tests/verify_video.py` then runs every supported encoded file
through the actual application and checks real frames, audible PCM, output
sample-clock duration, pause/seek and unchanged source hashes. Target ABI and
package verification independently inspect the exact production ELF/payload.

AAC and 44.1 kHz PCM use changing-frequency audio references to verify the
requested sample timeline after both nearby and distant seeks. The comparison
allows sub-millisecond container rounding and fractional resampler phase; it
checks the waveform after fitting that phase rather than accepting audible energy
alone. Near-keyframe AVI and DTS-indexed MPEG/TS seeks are covered by frame references.
