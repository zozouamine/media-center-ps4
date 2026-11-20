# Third-party notices

Original PS4 Media Center source and the original application icon are licensed
under the MIT license in LICENSE. Third-party code and fonts retain the licenses
below. The complete project is not represented as entirely MIT.

| Component | Source/version | License and supplied source |
| --- | --- | --- |
| dr_wav 0.14.7, dr_mp3 0.7.5, dr_flac 0.13.5 | mackron/dr_libs, commit c2d31fed078120f67263fd36237c0dcaae6bc07b | MIT option; licenses/dr_libs-LICENSE.txt; complete headers in vendor/ |
| stb_image 2.32, stb_truetype 1.28, stb_vorbis 1.24 | nothings/stb, commit 9600522de53973336fa033a289858a4354cbb04c | MIT option; licenses/stb-LICENSE.txt; complete source in vendor/ |
| libwebp 1.6.0 decoder and animation demuxer | Public libwebp-1.6.0 release archive; hash and individual source hashes in vendor/manifest.json | BSD-style license and patent grant in licenses/libwebp-COPYING.txt and licenses/libwebp-PATENTS.txt; source in vendor/libwebp/. The supplied ps4mc-budget.patch routes codec allocations through the application budget. |
| libtiff 4.7.1 | Public v4.7.1 release, RELEASE-DATE 20250911; archive and file hashes in vendor/manifest.json | licenses/libtiff-LICENSE.md; source in vendor/libtiff/. This product includes software developed by the University of California, Berkeley and its contributors. Memory-only I/O and bounded allocations are supplied by the application; Unix/Windows file backends are excluded. |
| zlib 1.3.1 | Public madler/zlib v1.3.1 release | zlib license in licenses/zlib-LICENSE.txt; complete decoder/compression dependencies in vendor/zlib/. Used for TIFF Deflate and compressed video frames including PNG/ZMBV/TSCC. |
| FFmpeg 7.1.3 | Public FFmpeg release; all source hashes in vendor/video-codecs-manifest.json | Configured with --enable-gpl; GPLv2 or later, with LGPL notices retained in licenses/ffmpeg-LICENSE.md, ffmpeg-COPYING.GPLv2.txt and ffmpeg-COPYING.LGPLv2.1.txt. Full source is in vendor/ffmpeg-7.1.3/. The linked video application is distributed under GPLv3; its original MIT source remains available under MIT. A corresponding-source archive and the decoder build scripts accompany the package. |
| dav1d 1.5.1 | VideoLAN AV1 decoder release; individual hashes in vendor/video-codecs-manifest.json | BSD 2-clause; licenses/dav1d-COPYING.txt; complete source in vendor/dav1d-1.5.1/. Software AV1 decoding; no host or proprietary codec library is linked. |
| GoldHEN Plugin SDK access ABI | GoldHEN/GoldHEN_Plugins_SDK, commit bcea3c7ef01dac6d7f9f49ebf9e90fe66d86f5f7 | MIT; licenses/GoldHEN-SDK-LICENSE.txt. The original access/syscall headers and source are pinned in vendor/goldhen-sdk/. The application uses a restricted adapter for version, current-process filesystem access and restoration; it does not include hooking, process memory access or network services. |
| PS4MC-Sans / PS4MC-Arabic fonts | Renamed subsets of Noto Sans / Noto Sans Arabic from google/fonts commit 8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5 | SIL OFL 1.1; both original fonts, OFL notices and preparation script are supplied. Arabic glyphs support filenames/input, while application UI is English only. |
| musl libc | Installed OpenOrbis port; no independent library version manifest was found | MIT and component notices in licenses/musl-COPYRIGHT.txt; public source: https://github.com/OpenOrbis/musl |
| libc++, libc++abi, libunwind | Installed OpenOrbis llvm-project port; exact binary hashes are recorded in the environment audit | Apache 2.0 with LLVM exceptions and retained legacy notices in licenses/OpenOrbis-libcxx-LICENSE.txt, OpenOrbis-libcxxabi-LICENSE.txt, OpenOrbis-libunwind-LICENSE.txt; public source: https://github.com/OpenOrbis/llvm-project |
| OpenOrbis empty libc / libSceFios2 PRX compatibility modules | Installed public toolchain; CHANGELOG explicitly describes empty homebrew stubs | GPLv3; licenses/OpenOrbis-LICENSE.txt; corresponding module source, project files, startup source and link script in tools/openorbis-module-source/; a source archive also accompanies the modules inside PKG licenses. These are not retail Sony modules. |
| LibOrbisPkg / PkgTool (build host only) | maxton/LibOrbisPkg commit 643477263b2644e0803e0f58b8726ea4e3f3b7d4; modified for .NET 8 and portable PKCS#1 v1.5 RSA signing | LGPLv3 with accompanying GPLv3 text; licenses/LibOrbisPkg-LICENSE.txt, licenses/GPL-3.0.txt; complete modified source in tools/pkgtool-source/, original crypto source in Crypto.cs.upstream |
| LLVM 18 build tools (not redistributed) | Locally installed upstream LLVM 18.1.8 | licenses/LLVM-18-LICENSE.txt is retained for reference |

Precise source download URLs, SHA-256 values and sizes are in
vendor/manifest.json, vendor/video-codecs-manifest.json, assets/fonts/manifest.json,
tools/pkgtool-manifest.json and licenses/manifest.json.
The SDK directory is named OpenOrbis-PS4-Toolchain-v0.5.4; this name is not
independent proof of its release tag. Inspected installed artifact hashes are
the reproducibility evidence.

To rebuild or modify the LGPL build utility, use the complete supplied source:

    dotnet build tools/pkgtool-source/PkgTool.Core/PkgTool.Core.csproj \
      -c Release -o tools/pkgtool

PkgTool and LibOrbisPkg DLLs remain separate, replaceable host-side assemblies.
No restriction is placed on modifying or debugging these components.
The PS4 package uses public homebrew fake-signing conventions rather than an
official retail signing service. No proprietary SDK or private retail key was
used by this project.

The task document and design reference are user-supplied development inputs.
The header PlayStation logo was extracted from that reference at the user's
request; the PlayStation mark belongs to Sony Interactive Entertainment.
The transport SVGs/PNGs are original project assets generated by
tools/generate_ui_assets.py. The full reference image and generated test media,
including reference-derived sample album artwork, are excluded from the PS4
application package.
