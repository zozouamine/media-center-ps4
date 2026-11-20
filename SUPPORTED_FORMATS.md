# Formats and verification

All playback results below come from the computer. The same decoding sources
cross-compile for PS4, but no format has been runtime-tested on a real PS4 or an
emulator. The program does not support every possible image or media format.

| Format / variant | Decoder | Computer evidence |
| --- | --- | --- |
| JPEG | stb_image 2.32 | Actual dimensions and pixel samples |
| PNG | stb_image | Exact generated pixels; corrupt/oversized input rejected |
| BMP RGB24 | stb_image | Actual pixel samples and dimensions |
| GIF animation | stb_image, bounded per-frame reconstruction | Three frame colors/delays; transparent subrectangles and restore-to-previous disposal match Pillow golden pixels; static thumbnails |
| WebP lossy/lossless and animation | libwebp 1.6.0 decode/demux | Lossless pixels match exactly; lossy samples; three real animation frames/delays and thumbnails |
| TGA RLE | stb_image | Exact generated RGBA pixels |
| PPM/PNM RGB and PGM grayscale | stb_image | Exact pixels and grayscale samples |
| PSD merged, uncompressed RGB8 | stb_image | Exact merged-image pixels |
| Radiance HDR RGBE | stb_image | Known source intensities converted to expected display RGB |
| TIFF raw/LZW/PackBits/Deflate RGB | libtiff 4.7.1 + zlib 1.3.1 | Exact generated pixels for each compression |
| TIFF monochrome CCITT Group 4 | libtiff | Actual black/white pixel samples |
| ICO PNG or uncompressed 32-bit DIB | Application container reader + stb_image | Exact pixels for both entry variants |
| WAV PCM16 stereo 48 kHz / mono 44.1 kHz | dr_wav 0.14.7 | Real PCM, resampling, seek and clean EOF; stereo output matches golden bytes |
| MP3 | dr_mp3 0.7.5 | Audible-sample energy, seek, clean EOF and ID3 text/cover extraction |
| FLAC | dr_flac 0.13.5 | Exact lossless PCM, seek and native PICTURE artwork |
| Ogg Vorbis | stb_vorbis 1.24 | Actual PCM/seek/EOF and METADATA_BLOCK_PICTURE artwork |
| AAC / M4A / Opus / WMA / AIFF | FFmpeg 7.1.3 + swresample | Genuine encoded files, non-silent stereo PCM, seek and application-level playback validation |
| AVI MJPEG + PCM | FFmpeg; legacy AVI reader retained as a test reference | 300 actual 320×180 frames at 24 FPS; PCM, pause, seek and OSD |
| AVI RGB24 / silent MJPEG | FFmpeg | Exact legacy RGB reference samples and actual production video playback |
| MP4 / MOV H.264 with AAC or ALAC | FFmpeg 7.1.3 | Genuine encoded fixtures, presentation-time frame comparisons, seek, pause, PCM and EOF validation |
| MKV HEVC 8/10-bit with AAC / FLAC | FFmpeg | Genuine 8/10-bit streams and a 3840×2160 HEVC fixture scaled to 1920×1080 |
| MKV AV1 with Opus | dav1d 1.5.1 through FFmpeg | Genuine SVT-AV1 encoding; software-decoded frames and audio |
| WebM VP8/Vorbis and VP9/Opus | FFmpeg | Real encoded video frames and companion audio |
| AVI MPEG-4/MP3, HuffYUV/PCM, MJPEG/PCM | FFmpeg | Real codecs, container duration and seeking; legacy AVI reader retained as a test reference |
| MPEG-PS MPEG-2/MP2 and MPEG-TS H.264/AAC/AC3 | FFmpeg | Actual MPG, TS and M2TS files with nonzero container start times |
| MOV ProRes/PCM | FFmpeg | Actual encoded fixtures and timestamp comparisons |
| MKV FFV1/FLAC, H.264/EAC3 and 5.1 AC3 | FFmpeg + swresample | Lossless frames, EAC3 and multichannel audio downmixed to stereo |
| AVI PNG + PCM | FFmpeg + vendored zlib | Genuine lossless PNG-encoded video frames and PCM |
| JPEG-compressed TIFF | Disabled external dependency | Genuine JPEG TIFF produces an explicit unavailable-compression error |
| HEIC/HEIF/AVIF/JPEG XL | Unavailable | Recognized as photos with explicit decoder-unavailable messages |
| Softimage PIC, other TIFF/ICO/PSD variants | Decoder paths exist | Not established by the fixture set; no broad compatibility claim |
| Subtitles / interactive audio-track selection | Unimplemented | The default/best video and audio streams are selected; subtitle rendering is not provided |

## Image and animation bounds

Compressed input is capped at 32 MiB, dimensions at 8192 each and 16 Mi pixels.
Pixel/codec allocations share a 96 MiB budget; compressed input vectors are
bounded separately. Animation is capped at 256 frames / 64 MiB of stored pixels.
Thumbnails decode one frame and resize to at most 640 pixels. Oversized/corrupt
animations and exhausted memory are tested for safe failure and cleanup.

GIF/WebP use decoded delays (minimum 40 ms), a monotonic animation clock and
finite/infinite loop counts. Photo Options pauses/resumes animation separately
from slideshow. Rotation/zoom affect display only. TIFF opens memory through
read-only callbacks, displays the first page and supports configured compression
only. ICO selects the largest valid PNG or 24/32-bit DIB entry within bounds;
24-bit decoding exists but the positive DIB fixture is 32-bit.

Images decode as bounded whole images on workers, not progressive scanlines.
Cancellation is checked around decode and between animation frames; some codec
CPU loops may finish before noticing it. Full EXIF orientation, ICC color
management, multilayer PSD and complete image-format coverage are not claimed.

## Audio and embedded artwork

Output is 48 kHz signed 16-bit stereo, with real sample-volume scaling. Sources
are one/two channels, 8–192 kHz, below 4 GiB and 12 hours with a known duration.
The WAV fixtures establish PCM16, not every bit depth accepted by dr_wav.

ID3 v2.2 PIC / v2.3-v2.4 APIC, FLAC PICTURE, Ogg picture comments and WAV `id3 `
are bounded to 5 MiB tags / 4 MiB artwork / 4 Mi pixels, resized to at most 640.
The fixture set verifies v2.3/v2.4 MP3, native FLAC, Ogg and WAV covers, including
Arabic text. Invalid artwork leaves audio playable and displays the fallback
icon. Clean EOF retains the displayed cover; stop/error releases it.

Music progress comes from consumed output frames. Pause, stop, previous/next,
±10-second seek, background playback and compact-button hit targets are tested.

## Video playback and bounds

Production video uses pinned static FFmpeg with all built-in software decoders,
demuxers and parsers available in this configuration, plus dav1d for AV1. Network
protocols, external files/playlists, devices, encoding and hardware accelerators
are disabled. Input uses the application's verified read-only descriptor and
seek callbacks. The browser recognizes MP4, MKV, AVI, MOV, WebM, M4V, MPEG/MPG
and TS/M2TS/MTS. Legacy and rarely used VOB, FLV, WMV/ASF, 3GP/3G2, OGV/OGM,
RM/RMVB, NUT, DV, MXF and similar extensions are intentionally not exposed as
video files. Recognizing an extension alone does
not guarantee that every variant is playable.

`reports/ffmpeg-ps4-configuration.txt` records the exact enabled components.
Configured native decoders also cover formats such as MPEG-1, H.263, VC-1,
Theora, VP6, DTS and TrueHD; variants absent from the fixture set have no
runtime compatibility claim. Files must contain a playable video stream and a
finite duration no longer than seven days. Raw streams with no determinable
duration require remuxing. Audio is converted to 48 kHz S16 stereo, including
resampling and multichannel downmixing.

Video uses decoded presentation timestamps, including B frames and variable
frame rates. The audio clock comes from consumed sink samples. Container origins,
stream offsets, real audio gaps, decoder preroll and resampler delay are accounted
for. Millisecond timestamp rounding does not insert/drop samples between adjacent
audio blocks. Negative encoder preroll is discarded before zero. Video seeks decode
a second of preroll to handle DTS-based indexes and use the preceding stream tick;
returning to zero reopens the file so TS/PS keeps its first GOP. Audio restores
codec/resampler state and permits only bounded incomplete-frame recovery until
the first valid post-seek frame. Seek flushes both codec states and output buffers; paused seeks
remain paused. Silent video uses a monotonic clock. OSD visibility is independent.

Decoded input is capped at 16 Mi pixels and four codec threads; each FFmpeg
allocation is capped at 64 MiB. FFmpeg's reference-frame memory is separate from
the application's 96 MiB bitmap budget. Only the selected display frame is
converted to RGBA and bounded to 1920×1080, preserving sample aspect ratio.
These are safety bounds, not measured console throughput. HDR tone mapping,
Dolby Vision processing and universal playback/performance are not established.
All decoding is software; actual PS4 playback, especially 4K/HEVC/AV1 performance,
still requires a physical console test.
