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
| FFmpeg/libav | 7.1.1 | Video decode-only (H.264/H.265; mov/mp4/m4v + matroska/webm demux; libswscale); configure-built static → `vendor/codecs-prefix`; needs **nasm**; linked by `link_av()` (avformat/avcodec/swscale/avutil) under `OSV_VENDORED_AV` (Phase 15) |

Image codecs are built by `scripts/build_codecs.{sh,bat}` (shared by `setup.{sh,bat}` and CI)
and installed into `vendor/codecs-prefix/`; premake's `link_image_codecs()` links them in
order `heif → de265 → aom → webp → sharpyuv`. The build passes
`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (libde265's pre-3.5 cmake_minimum under CMake 4.x).

**FFmpeg (Phase 15)** is a sibling vendored submodule (`vendor/ffmpeg`) built via **configure**
(not cmake) into the same `vendor/codecs-prefix` by `build_codecs.{sh,bat}`: decode-only
(`--disable-everything` then opt-in h264/hevc decoders, mov/mp4/matroska demuxers, swscale; no
encoders/muxers/protocols/network/programs). `link_av()` links avformat/avcodec/swscale/avutil and
defines `OSV_VENDORED_AV` **only when `lib/libavcodec.a` is present**, so non-FFmpeg builds stay
green. Index format is now `INDEX_VERSION = 4` (adds `Type::Video` + `VideoMeta`; v1–v3 read back-compat).

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
