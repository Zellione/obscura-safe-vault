# Tech Stack

## Language / standard
- C++20 — use `std::span`, designated initialisers, `[[nodiscard]]`, `constexpr` freely.

## Build system
- **premake5 beta8** → **Ninja** (gmake2 fallback). Binary at `bin/premake5` (downloaded by `scripts/setup.sh`, not committed).
- Lua config: `premake5.lua` at project root.
- Generated files: `osv.ninja`, `osv_tests.ninja`, `monocypher.ninja`, `build.ninja`, `compile_commands.json` (gitignored).

## Runtime deps (vendored git submodules under `vendor/`)
| Library | Version | How compiled |
|---|---|---|
| SDL3 | 3.4.10 | cmake once via `setup.sh`; static lib at `vendor/SDL3/build/libSDL3.a` (Linux/macOS) or `vendor/SDL3/build/SDL3-static.lib` / `Release/SDL3-static.lib` (Windows) |
| Monocypher | 4.0.2 | single `monocypher.c` compiled by premake |
| stb | head | header-only (`stb_image.h`, `stb_truetype.h`) |
| libwebp | 1.4.0 | WebP decode; cmake → `vendor/codecs-prefix` |
| libde265 | 1.0.15 | HEIC (HEVC) decode; cmake → `vendor/codecs-prefix` |
| libaom | 3.14.1 | AVIF (AV1) decode, decoder-only; needs **nasm**; cmake → `vendor/codecs-prefix` |
| libheif | 1.18.2 | HEIC/AVIF container; one `decode_heif_from_memory` covers both |
| FFmpeg/libav | 7.1.1 | Video & audio decode-only (H.264/H.265 + ProRes/DNxHD-DNxHR/MJPEG for `.mov` pro codecs, Phase 28; aac/opus/mp3/vorbis/flac/ac3 audio; mov/mp4/m4v + matroska/webm demux; libswscale for video, swresample linked as transitive dependency of audio decoders — we do NOT use swresample for audio conversion, SDL_AudioStream handles that); configure-built static → `vendor/codecs-prefix`; needs **nasm**; linked by `link_av()` (avformat/avcodec/swscale/swresample/avutil) under `OSV_VENDORED_AV` (Phase 15–16) |
| nlohmann/json | v3.12.0 | Archive `meta.json` parsing (Phase 27). Header-only single-header MIT lib; include path `vendor/json/single_include` (no build step, no premake project). Used exception-free: `json::parse(..., allow_exceptions=false)` → discarded value on malformed input. Only consumer: `src/ui/meta_json.cpp` |
| miniz | master commit `e78dfd2` | ZIP reader (Phase 17). Plain-C static lib compiled by premake from the modern split sources (`miniz.c`/`miniz_tdef.c`/`miniz_tinfl.c`/`miniz_zip.c`); the only release tags v112–v114 are ancient SVN snapshots, so pinned to a master commit. Built + consumed with `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` (else its `compress`/`crc32`/`inflate` clash with the libz avformat links). `vendor/miniz-shim/miniz_export.h` supplies the one CMake-generated header so the submodule stays pristine; consumers include the umbrella `"miniz.h"` (not `miniz_zip.h`, which lacks `mz_alloc_func`/`MZ_BEST_SPEED`) |

Image codecs are built by `scripts/build_codecs.{sh,bat}` (shared by `setup.{sh,bat}` and CI)
and installed into `vendor/codecs-prefix/`; premake's `link_image_codecs()` links them in
order `heif → de265 → aom → webp → sharpyuv`. The build passes
`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (libde265's pre-3.5 cmake_minimum under CMake 4.x).

**FFmpeg (Phase 15–16)** is a sibling vendored submodule (`vendor/ffmpeg`) built via **configure**
(not cmake) into the same `vendor/codecs-prefix` by `build_codecs.{sh,bat}`: decode-only
(`--disable-everything` then opt-in h264/hevc/prores/dnxhd/mjpeg decoders (pro `.mov` codecs Phase 28), aac/opus/mp3/vorbis/flac/ac3 audio decoders,
mov/mp4/matroska demuxers, swscale + swresample; no encoders/muxers/protocols/network/programs).
`link_av()` links avformat/avcodec/swscale/swresample/avutil and defines `OSV_VENDORED_AV` **only
when `lib/libavcodec.a` is present**, so non-FFmpeg builds stay green. Index format is now
`INDEX_VERSION = 4` (adds `Type::Video` + `VideoMeta`; v1–v3 read back-compat). Audio samples decoded
by `AudioDecoder` (planar→interleaved F32) flow into `SDL_AudioStream` (SDL does rate/format/channel
conversion using its own resampler — swresample is a transitive dependency but not used by our code).

## Crypto
- AEAD: XChaCha20-Poly1305 (192-bit nonce, random per chunk)
- KDF: Argon2id (via Monocypher)
- RNG shim: `src/crypto/random.*` — `getrandom` (Linux), `getentropy` (macOS), `BCryptGenRandom` (Windows)

## Platforms
- Primary: Linux x86_64 (Arch). Also: Windows x86_64, macOS (native arch, arm64 on Apple Silicon).
- Windows Release builds as `WindowedApp` (no console); Debug keeps console.

## Asset loading
App tries `assets/…` relative to cwd first, then `SDL_GetBasePath()` (packaged installs).

## CI
`.github/workflows/` — matrix covers Linux, Windows, macOS. The ASAN job (Linux-only) builds
vendored SDL3 (since Phase 4) and the image codecs (since Phase 9): running the C decoders
under ASAN/UBSan on untrusted input is high value. nasm is installed on every leg for libaom.
