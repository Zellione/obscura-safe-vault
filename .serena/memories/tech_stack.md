# Tech Stack

## Language / standard
- C++23 — use `std::span`, designated initialisers, `[[nodiscard]]`, `constexpr` freely.

## Build system
- **premake5 beta8** → **Ninja** (gmake2 fallback). Binary at `bin/premake5` (downloaded by `scripts/setup.sh`, not committed).
- Lua config: `premake5.lua` at project root.
- Generated files: `osv.ninja`, `osv_tests.ninja`, `monocypher.ninja`, `build.ninja`, `compile_commands.json` (gitignored).

## Runtime deps (vendored git submodules under `vendor/`)
| Library | Version | How compiled |
|---|---|---|
| SDL3 | 3.4.10 | cmake once via `setup.sh`; static lib at `vendor/SDL3/build/libSDL3.a` (Linux) or `vendor/SDL3/build/SDL3-static.lib` / `Release/SDL3-static.lib` (Windows) |
| Monocypher | 4.0.2 | single `monocypher.c` compiled by premake |
| stb | head | header-only (`stb_image.h`, `stb_truetype.h`) |
| libwebp | 1.4.0 | WebP decode; cmake → `vendor/codecs-prefix` |
| libde265 | 1.0.15 | HEIC (HEVC) decode; cmake → `vendor/codecs-prefix` |
| libaom | 3.14.1 | AVIF (AV1) *stills* decode via libheif, decoder-only; needs **nasm**; cmake → `vendor/codecs-prefix`. Phase 40: also linked a second time into FFmpeg as the `libaom-av1` decoder for AV1 *video* (see FFmpeg row) — one vendored copy, two independent consumers |
| libheif | 1.18.2 | HEIC/AVIF container; one `decode_heif_from_memory` covers both |
| FFmpeg/libav | 7.1.1 | Video & audio decode-only (H.264/H.265 + ProRes/DNxHD-DNxHR/MJPEG for `.mov` pro codecs, Phase 28; VP8/VP9 for `.webm`, Phase 38; AV1 for `.webm`/`.mov` + QTRLE/Cinepak for `.mov`, Phase 40; aac/opus/mp3/vorbis/flac/ac3 audio; mov/mp4/m4v + matroska/webm demux; libswscale for video, swresample linked as transitive dependency of audio decoders — we do NOT use swresample for audio conversion, SDL_AudioStream handles that); configure-built static → `vendor/codecs-prefix`; needs **nasm**; linked by `link_av()` (avformat/avcodec/swscale/swresample/avutil, **then `aom` a second time** — see gotcha below) under `OSV_VENDORED_AV` (Phase 15–16). **AV1 gotcha (Phase 40):** FFmpeg's own native `av1` decoder is a hwaccel-dispatch-only shim (`AVERROR(ENOSYS)` without a HW accelerator — confirmed by direct testing, not documented in FFmpeg's own `--enable-decoder` help text); real software AV1 decode requires `--enable-libaom --enable-decoder=...,libaom_av1,...` (configure component name is `libaom_av1`, **underscore** — derived from the `ff_libaom_av1_decoder` extern symbol — while the runtime/display decoder name is `libaom-av1`, **hyphen**; passing the hyphenated form to `--enable-decoder` silently no-ops with a `did not match anything` warning easy to miss in a long build log). `PKG_CONFIG_PATH=$CODEC_PREFIX/lib/pkgconfig` points configure at the `aom.pc` `build_codec aom` already installed (Windows needs the `pkgconf` MSYS2 package, see ci.yml). Because the image-codec chain (`heif → de265 → aom → webp → sharpyuv`) links `aom` *before* `avformat`/`avcodec` in `premake5.lua`, and GNU ld's static-archive resolution is single-pass (a library is never re-scanned once ld has moved past it), avcodec's new `aom_codec_*` references go unresolved unless `aom` is **also** listed again after the `link_av()` block — `link_codecs()`'s occurrence still satisfies libheif's needs, the second one satisfies avcodec's. Also: **`ninja` does not detect a rebuilt `vendor/codecs-prefix/lib/libavcodec.a` as a relink trigger** (it's a prebuilt external file, not a ninja-generated build edge) — after rebuilding the vendored codecs with a changed decoder list, `rm -rf build/` before `scripts/gen.sh && scripts/test.sh`, or the test binary silently keeps running against the stale `.a`. |
| nlohmann/json | v3.12.0 | Archive `meta.json` parsing (Phase 27). Header-only single-header MIT lib; include path `vendor/json/single_include` (no build step, no premake project). Used exception-free: `json::parse(..., allow_exceptions=false)` → discarded value on malformed input. Only consumer: `src/ui/meta_json.cpp` |
| miniz | master commit `e78dfd2` | ZIP reader (Phase 17). Plain-C static lib compiled by premake from the modern split sources (`miniz.c`/`miniz_tdef.c`/`miniz_tinfl.c`/`miniz_zip.c`); the only release tags v112–v114 are ancient SVN snapshots, so pinned to a master commit. Built + consumed with `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` (else its `compress`/`crc32`/`inflate` clash with the libz avformat links). `vendor/miniz-shim/miniz_export.h` supplies the one CMake-generated header so the submodule stays pristine; consumers include the umbrella `"miniz.h"` (not `miniz_zip.h`, which lacks `mz_alloc_func`/`MZ_BEST_SPEED`) |
| zlib | 1.3.2 | gzip filter dep for libarchive (Phase 34). cmake → `vendor/codecs-prefix`; `-DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_TESTING=OFF`. Windows static-lib output name is `zs.lib` (`zlib_static_suffix="s"` under `if(WIN32)` in its own CMakeLists.txt) — `link_archive()` in premake5.lua branches on `system:windows` for this |
| xz / liblzma | 5.8.3 | LZMA2 filter dep for libarchive, covers `.7z`/`.txz` (Phase 34). cmake → `vendor/codecs-prefix`; `-DXZ_SANDBOX=no` is required for `--asan` builds — xz's own Landlock-sandboxing configure check hard-errors on seeing `-fsanitize=` in CFLAGS otherwise. Static lib output name is `lzma` on every platform (no suffix quirk, unlike zlib) |
| libarchive | 3.8.8 (BSD-2-Clause) | 7z/RAR/TAR read-only import (Phase 34). cmake → `vendor/codecs-prefix`, out-of-tree build dir `vendor/.libarchive-build` (NOT `vendor/libarchive/build`, which the submodule's own source tree already tracks — cmake helper modules that an out-of-tree build there would clobber). Finds zlib/liblzma via `CMAKE_PREFIX_PATH` (same pattern libheif uses for libde265/libaom); every optional codec/crypto backend disabled except zlib+lzma (no bzip2/lz4/lzo/zstd — bzip2 has no CMake build upstream, so `.tbz2` is out of scope; no OpenSSL/mbedTLS/Nettle/CNG; no libxml2/expat; no ACL/xattr/iconv; no bsdtar/bsdcpio/bsdcat/test binaries). Linked by `link_archive()` under `OSV_VENDORED_ARCHIVE`; static lib output name is `archive` on every platform |

Image codecs are built by `scripts/build_codecs.{sh,bat}` (shared by `setup.{sh,bat}` and CI)
and installed into `vendor/codecs-prefix/`; premake's `link_image_codecs()` links them in
order `heif → de265 → aom → webp → sharpyuv`. The build passes
`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (libde265's pre-3.5 cmake_minimum under CMake 4.x).

**FFmpeg (Phase 15–16)** is a sibling vendored submodule (`vendor/ffmpeg`) built via **configure**
(not cmake) into the same `vendor/codecs-prefix` by `build_codecs.{sh,bat}`: decode-only
(`--disable-everything` then opt-in h264/hevc/prores/dnxhd/mjpeg/vp8/vp9/libaom_av1/qtrle/cinepak
decoders (pro `.mov` codecs Phase 28; `.webm` VP8/VP9 Phase 38; AV1 + legacy `.mov` codecs Phase 40),
aac/opus/mp3/vorbis/flac/ac3 audio decoders, mov/mp4/matroska demuxers, `--enable-libaom`,
swscale + swresample; no encoders/muxers/protocols/network/programs). See the FFmpeg row above for
the `libaom_av1` naming/link-order gotchas.
`link_av()` links avformat/avcodec/swscale/swresample/avutil and defines `OSV_VENDORED_AV` **only
when `lib/libavcodec.a` is present**, so non-FFmpeg builds stay green. Index format is now
`INDEX_VERSION = 4` (adds `Type::Video` + `VideoMeta`; v1–v3 read back-compat). Audio samples decoded
by `AudioDecoder` (planar→interleaved F32) flow into `SDL_AudioStream` (SDL does rate/format/channel
conversion using its own resampler — swresample is a transitive dependency but not used by our code).

**libarchive + zlib + xz (Phase 34)** extend archive import beyond ZIP/CBZ (miniz, unchanged) to
`.7z`/`.rar`/`.tar`(`.gz`/`.xz`)/`.cbr`/`.cb7`/`.cbt`, cmake-built into the same `vendor/codecs-prefix`
by `build_codecs.{sh,bat}`. `link_archive()` defines `OSV_VENDORED_ARCHIVE` only when
`lib/libarchive.a` is present (same presence-gating pattern as `link_av()`), so a build without it
still links — `src/ui/archive_import.*` is declared unconditionally and returns a graceful "not
supported" outcome, and `.zip`/`.cbz` keep working via miniz regardless. `ArchiveReader`
(`src/ui/archive_reader.*`) wraps libarchive's streaming read API; because libarchive has no
random-access API, `extract(index, out)` re-opens and re-scans the stream from the start on every
call rather than caching all decompressed entries in memory. `scripts/build_codecs.sh`'s
idempotency check (`build_codec()`'s "already installed" skip) was fixed during Phase 34 to check
for the actual `lib${name}.a` file rather than a loose `find -name "*${name}*"` glob — the glob also
matched leftover pkgconfig/cmake-config files from a prior partial install, silently skipping a real
rebuild and leaving libarchive linked against the *host system's* zlib/liblzma instead of the
vendored static libs.

## Crypto
- AEAD: XChaCha20-Poly1305 (192-bit nonce, random per chunk)
- KDF: Argon2id (via Monocypher)
- RNG shim: `src/crypto/random.*` — `getrandom` (Linux), `BCryptGenRandom` (Windows)

## Platforms
- Primary: Linux x86_64 (Arch). Also: Windows x86_64. macOS is not supported
  (dropped from CI/build/source — see `#error` guard in `src/crypto/random.cpp`).
- Windows Release builds as `WindowedApp` (no console); Debug keeps console.

## Asset loading
App tries `assets/…` relative to cwd first, then `SDL_GetBasePath()` (packaged installs).

## CI
`.github/workflows/` — ci.yml matrix covers Linux and Windows (macOS support dropped).
release.yml runs on tag pushes (`v*`): rebuilds the two Release packages with OSV_VERSION
from the tag (mirrors ci.yml's Release legs + cache keys — keep in sync), runs tests, then
attaches packages + SHA256SUMS.txt to the tag's GitHub release (draft-created if absent).
The ASAN job (Linux-only) builds vendored SDL3 (since Phase 4) and the image codecs (since
Phase 9): running the C decoders under ASAN/UBSan on untrusted input is high value. nasm is
installed on every leg for libaom.
