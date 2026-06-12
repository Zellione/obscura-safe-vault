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
`.github/workflows/` — matrix covers Linux, Windows, macOS; ASAN job is Linux-only and does NOT provision SDL3.
