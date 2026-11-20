# Technical design — PS4 Media Center

## Inspected environment and APIs

The environment is macOS 12.7.6 / Intel x86_64, AppleClang 14 for the host and
upstream LLVM/Clang/LLD 18.1.8 for PS4. The inspected OpenOrbis installation is
`/Users/youssouf/OpenOrbis-PS4-Toolchain-v0.5.4`; its directory name is not independent
proof of that release tag. Inspected artifact hashes and unavailable APIs are
recorded in `reports/environment-audit.json` and related audits.

Typed VideoOut/libkernel APIs provide open/register/submit/status/unregister/close,
direct-memory allocation/mapping/release and event queues. PS4 uses two bounded
linear 1920×1080 RGBA buffers and vblank presentation of a portable CPU 2D canvas.
Failed output close does not cause still-owned direct memory to be freed.

Typed Pad structures provide sticks, triggers and button flags. Input is polled
once per frame, filtered through a radial 0.24 dead zone, edge detection and
bounded repeat timing. Essential actions do not depend on touchpad input.

AudioOut provides typed initialization/open/output/close. An audio worker streams
48 kHz signed 16-bit stereo in 1024-frame blocks, applies gain to actual samples
and reports consumed output frames as the playback clock. Silent video uses the
monotonic clock. Decode and file reads stay outside the render loop.

POSIX descriptor/directory calls and pthread entry points exist, but the installed
SDK also contains incompatible headers and ENOSYS filesystem stubs. PS4 adapters
decode the native 120-byte stat into the public SDK structure, implement no-follow
metadata queries through read-only descriptors, and avoid realpath/readlink.
Discovery probes title-visible `/usb0` through `/usb7` roots and the global
`/mnt/usb0` through `/mnt/usb7` paths before attempting the parent listing.
It then includes other readable USB-named mount directories. Device/inode identity is
checked throughout. No usable friendly-label API was verified; drives are never mounted
or formatted by the application.

The v8 storage worker first requests process filesystem access using the official
GoldHEN Plugin SDK ABI, pinned to commit
`bcea3c7ef01dac6d7f9f49ebf9e90fe66d86f5f7`. A restricted adapter uses syscall 0
with request 500 in rdi, command in rsi and backup pointer in rdx. It accepts only
version (0), process access (2), and restore (3). SDK major version 1 is required
before passing the 72-byte backup, whose size and field offsets are checked
against the pinned upstream header. Access preparation is serialized and Refresh
does not replace an active backup. Main restores the original process credentials
and filesystem roots after application and platform cleanup. There are no FTP
sources, network services, firmware offsets or process-memory operations.

The explicit v8 console trial reported SDK-unavailable with errno 256 and ENOENT
for `/mnt/usb1`. Its decoder had interpreted a carry-set `0x100` reply as an error,
so preparation never reached command 2. v9 handles this exact pinned version
reply in an application-owned decoder, even when carry is set. This exception
applies only to command 0; credential access/restoration and all other errors
retain strict conversion. The native assembly still uses the same SDK dispatch.

After access preparation, discovery reads the parent device through a native
read-only directory handle. Readable USB-slot directories on that same device
are placeholders rather than mounted drives and are omitted. Actual mounted
USB devices, including empty drives, remain selectable. If parent metadata is
inaccessible, individual child probes continue. The simulation models distinct
mount device identities while using real host descriptors and file contents.

SELF generation defaults to the zero authentication record used by the startup
baseline. The explicit Homebrew profile in `sce_sys/authinfo.hex` failed the v4/v6
console trials and is limited to compile-only diagnostics. Package builds require
the default profile. `tools/verify_ps4_self.py` checks the exact signature record,
profile bytes and original program authentication ID before packaging and after
extraction. Source scans
retain per-path open/fstat/type/listing results, including numeric errno. Both initial
discovery and Refresh deliver those results to the UI through the storage worker.
An empty Sources view exposes Square → USB diagnostics; Up/Down scrolls all rows.
The application reads filesystems mounted by PS4 and does not filter out exFAT.

Free space uses descriptor-based **fstatfs** with native-layout translation. The
adapter decodes the eight-byte filesystem id, filesystem name at byte 280 and
88-byte mount names of the FreeBSD 9 layout; a 4096-byte scratch buffer accommodates
the kernel output without writing into the undersized public fields. The
installed statvfs/fstatvfs wrappers return success without populating output and
are explicitly rejected. Multiplication and string bounds are checked; failed
queries show unavailable space. PS4 displays the actual mount identifier when no
friendly-label API exists. macOS additionally uses bounded fgetattrlist volume
attributes. See `reports/filesystem-api-audit.json`; runtime PS4 values remain
unverified. A failed capacity query does not reject an otherwise readable source.

The prebuilt libc++ condition-variable object compares pthread timeouts with
Linux's 110 although target headers define ETIMEDOUT=60. Its wait/notify methods
are rebuilt from the target headers. The chrono object also uses Linux clock ids;
both chrono clocks are supplied using sceKernelGetProcessTime and sceKernelGettimeofday.
The application's clock uses the native process timer. The cross-link audit rejects
the old condition-variable/chrono objects and the unused broken filesystem stubs.
See [ps4-runtime-investigation.md](reports/ps4-runtime-investigation.md).

AvPlayer headers contain parameterless void placeholders, not a usable decoder
interface. Production video instead links pinned static FFmpeg 7.1.3 and dav1d
1.5.1 software decoders, covering H.264, HEVC, AV1 and other configured codecs.
PS4 ABI inspection checks that those decoder descriptors are retained in the
actual ELF. Hardware decoding remains unavailable. The POSIX clock entry point
used by FFmpeg is translated to native process/realtime clocks. Optional dav1d
affinity and thread naming are disabled for the OpenOrbis musl/FreeBSD mismatch.

## Browser, interface and workers

The UI is English only. Localized strings and language settings were removed;
legacy saved language fields are ignored. UTF-8 filenames, Arabic input/search
normalization and contextual Arabic glyphs remain. Shaping is intentionally
limited, not full Unicode bidi/HarfBuzz or universal font coverage.

Four colored tabs enter the root / Videos / Music / Photos source folders.
Category changes reset history, search and page cursors. USB Files includes
nonmedia documents with size/type and Cannot open; only supported media goes to
a decoder. The layout follows the reference's header, tabs, left list, right
information panel and colored controller footer. The global font scale is 0.82;
fonts/icons align by visible glyph bounds. The PlayStation logo is derived from
the supplied reference, while transport icons are original reproducible assets.

ReadOnlyFile walks components with openat/O_NOFOLLOW and exposes no mutation API.
DirectoryWorker retains at most 128 entries through keyset paging. SourceMonitor
checks known source identities every 500 ms independently of the displayed view.
Disconnect cancels photo/preview/index work, returns to Sources and stops playback
only when its source disappeared. Indexer keeps at most 64 open directories and
4096 items / 8 MiB of retained metadata. Preview/full-photo generations discard
stale results without blocking navigation.

Music has six compact 168×116 rounded controls, shared with pointer hit testing.
Icons are decoded once into 32-pixel transport images; artwork is displayed in a
400×400 frame. Back continues background audio, Stop releases media and returns,
and unavailable seek/previous/next controls are disabled. Queues/repeat/shuffle
use actual playlist state.

Video reuses the same loaded transport assets in six 64×64 rounded buttons,
with centered 28-pixel icons, no text labels and a translucent bottom gradient.
Back, aspect and Options use centered icons too. Shared video layout,
visibility and enabled-state helpers keep rendering and pointer actions aligned.
Previous/next, ±10-second seek, play/pause/replay and Stop dispatch the real media
commands; Stop/Back return to the browser and Square remains aspect selection.
Hidden pointer input first reveals the controls without dispatching a command.
Loading/error/EOF controls remain usable after the seven-second activity timeout;
paused video and menus keep the OSD visible. No decoder work is tied to OSD hiding.

StateStore protects source paths before creating private directories, serializes
bounded favorites/history/queues and owns only state.tsv/state.tmp. PS4 state
lives under the fixed `/data/PS4MediaCenter` directory. Native mkdir/rename/unlink
replace the SDK's nonfunctional *at mutation stubs for this directory only; its
identity is checked before absolute-path operations. Complete paths are validated
before any directory is created. Confirmation applies only to clearing internal
state. After source discovery, a previously denied store may reopen its private
directory. A persistent source-overlap flag prevents reopening a store forbidden
by media-source boundaries. Settings are reloaded only when the user has not
already changed them; otherwise the current state is saved after recovery. The
private startup log has a separately serialized reopening path. USB mutation
actions are absent.

## Media decoding and resource bounds

stb_image handles JPEG/PNG/BMP/GIF/TGA/PSD/HDR/PIC/PNM. libwebp 1.6.0 provides
static/animated WebP and demuxing; its supplied patch routes allocations through
the shared budget. libtiff 4.7.1 uses application-owned memory callbacks and
configured raw/LZW/PackBits/Deflate/CCITT codecs, with zlib 1.3.1 for Deflate.
Unix/Windows TIFF file-I/O backends and external TIFF codec dependencies are
excluded. ICO selects a bounded valid PNG or uncompressed DIB entry.

Pixel/codec allocations share 96 MiB. Compressed input vectors are separately
bounded to 32 MiB; image dimensions are capped at 8192 / 16 Mi pixels. GIF/WebP
animation stores at most 256 frames / 64 MiB, preserves frame delay/disposal and
loop counts, and uses a monotonic clock independent of slideshow. Thumbnails
use one frame and resize to at most 640. TIFF displays the first page. Images
are bounded whole-image decodes, not progressive scanline decoding; cancellation
can wait for a third-party CPU loop to finish but does not block the UI thread.

Audio uses dr_wav 0.14.7, dr_mp3 0.7.5, dr_flac 0.13.5 and stb_vorbis 1.24.
Bounded metadata parsing supports ID3 PIC/APIC, FLAC pictures, Vorbis picture
comments and WAV id3/INFO chunks. Tags are capped at 5 MiB, cover files at 4 MiB /
4 Mi pixels, with front covers preferred and a 640-pixel output cap. Bad artwork
does not fail audio. EOF retains the cover; stop/error releases it.

MediaEngine owns decoder/sink lifetimes on its worker. Pause freezes the consumed
sample clock; seek changes actual sample positions and timestamp-selected video frames. OSD
uses a separate activity timer: exactly seven seconds of no intentional input,
kept visible while paused or in menus. OSD never opens/stops a decoder.

PS4 framebuffer alignment totals roughly 18 MiB plus the ~8 MiB portable canvas;
glyph cache is bounded to ~4 MiB / 768 entries. These are code budgets, not measured
console memory. Video has separately checked input-pixel, allocation and display limits documented
in SUPPORTED_FORMATS.md. No decorative GPU effects, WebView or UI engine is used.

Video/audio each use a separate bounded demuxer over verified ReadOnlyFile
callbacks, avoiding unbounded cross-stream packet queues for poorly interleaved
files. Network protocols and external demuxer I/O are disabled. Video retains
one pending future frame and one selected frame; reference/B frames are decoded
but only the frame selected by presentation time is converted/scaled to RGBA.
Sample aspect ratio is preserved, and output fits 1920×1080 even for 4K input.

Audio uses container-relative timestamps, accounts for swresample delay, keeps
adjacent blocks contiguous within timestamp rounding precision and inserts
silence for actual offsets/gaps. Seeks reset codec/output states, decode audio
preroll and discard samples before the requested target. Unindexed valid files
fall back to reopening and decoding from the beginning. The selected display
frame changes only at its presentation time; no frame-rate estimate drives the
engine's frame revisions. FFmpeg reference-frame memory is separately bounded
from the application's bitmap budget.

## Native preview and verification

Cocoa/CoreAudio share the actual application core. A replaceable Core Animation
image presents an immutable copy of each revised canvas with explicit transactions
and per-presentation autorelease pools. This keeps native presentation storage
bounded and avoids retaining manually created drawing contexts. Resized/letterboxed
mouse positions are translated back into the shared 1920×1080 control geometry.
The host compositor change does not affect PS4 VideoOut.

Current verification is recorded in TEST_PLAN.md and the dated JSON reports.
The computer core tests also link the replacement condition-variable methods;
a separate suite exercises FreeBSD errno 60 and native stat/statfs byte layouts.
Native music previously verified real callbacks, seek, cover art and a 20-second
resident-memory sample (~71.7 MiB peak / ~1.5 MiB measured steady growth).
The user reported a failed v1 run on physical PS4 12.52 / GoldHEN 2.4b18.12 with
exFAT USB. PS4-Xplorer accesses the drive in Root / Jailbroken mode. The explicit
v8 trial opens the interface but blocks access preparation at the version query
and still reports ENOENT for `/mnt/usb1`. v9 corrects that reply interpretation;
its physical-console USB result must be recorded separately from host checks.

## Dependencies, package and delivery

Complete public dependency sources, version/hash manifests and notices accompany
the project. Font derivatives retain OFL notices; public OpenOrbis empty PRX modules
include their GPL source archive. PkgTool/LibOrbisPkg's complete modified LGPL source
and original crypto implementation are included as replaceable host tools.

The default build produces host tests and PS4 ELF/FSELF without packaging.
Explicit --pkg builds a new version, populates every GP4 payload directory,
verifies PKG signatures/hashes and extracts all staged files for hash comparison
before exclusive final-file creation. Detailed results are in BUILD_REPORT.md.
An earlier source delivery exists in the case-sensitive sibling `../PS4 Media Center`.
After two write-permission review timeouts, the user adopted the corrected current
working directory as the project. The sibling remains at its previous source;
no transfer was performed during this fix.
