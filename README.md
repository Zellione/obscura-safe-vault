# Obscura-Safe-Vault

A multi-platform native encrypted photo gallery. All photos live inside a single `.osv` vault file — images are decrypted **into locked memory only**, never written to a temporary file or disk. The gallery is browsable with a freely-nestable folder tree, a zoomable full-screen image viewer, and a thumbnail strip navigable with arrow keys.

**Stack:** C++20 · SDL3 · SDL_Renderer · Monocypher (XChaCha20-Poly1305 + Argon2id) · stb_image · premake5 → Ninja

See [`CLAUDE.md`](CLAUDE.md) for all technology decisions and [`ROADMAP.md`](ROADMAP.md) for the full development plan.

---

## Building

### First-time setup

Initialises git submodules, downloads the `premake5` binary, and cmake-builds vendored SDL3 as a static library. Requires `cmake`, `ninja`, and a C++20 compiler.

```bash
scripts/setup.sh
```

> **Development shortcut:** if SDL3 is already installed system-wide (e.g. `sudo pacman -S sdl3` on Arch), you can skip `setup.sh` and go straight to generating build files. The system SDL3 is used as a fallback automatically.

### Generate build files

```bash
scripts/gen.sh           # Ninja (recommended)
scripts/gen.sh --gmake   # GNU Make fallback
```

### Compile

```bash
scripts/build.sh             # Debug
scripts/build.sh --release   # Release
```

### Run

```bash
build/bin/Debug/osv
build/bin/Release/osv
```

---

## Testing

Tests live in `tests/` and are added phase by phase alongside each feature (see `ROADMAP.md`). Phase 1 adds crypto known-answer + round-trip/tamper tests; Phase 2 adds vault round-trip tests.

`scripts/test.sh` generates build files, compiles the `osv_tests` target, and runs it. It exits non-zero if any test fails (CI-friendly).

```bash
scripts/test.sh             # Debug build, run all tests
scripts/test.sh --asan      # build + run under AddressSanitizer + UBSan/LSan
scripts/test.sh --release   # optimised build
scripts/test.sh --gmake     # GNU Make instead of Ninja
```

You can also run the binary directly after a build:

```bash
build/bin/Debug/osv_tests
```

Both the test suite and the ASAN/UBSan run are enforced in CI on every pull request (see `.github/workflows/ci.yml`).

---

## Debugging

### ASAN + UBSAN (recommended for development)

Pass `--asan` to the generator to add AddressSanitizer + UndefinedBehaviorSanitizer to every native project:

```bash
scripts/test.sh --asan           # build + run the tests under ASAN/UBSan/LSan
bin/premake5 --asan ninja        # ...or regenerate, then build any target by hand
ninja Debug_x64 && build/bin/Debug/osv
```

### Valgrind (memory leaks / secure-wipe verification)

```bash
valgrind --leak-check=full --track-origins=yes build/bin/Debug/osv_tests
valgrind --leak-check=full --track-origins=yes build/bin/Debug/osv
```

### GDB

```bash
gdb build/bin/Debug/osv
(gdb) run
(gdb) bt       # backtrace on crash
```

---

## Project structure

```
src/
  app/       State machine + event loop
  crypto/    Monocypher wrappers: XChaCha20-Poly1305, Argon2id, secure memory
  vault/     .osv container: header, index tree, chunk store
  image/     stb_image decode from memory + thumbnail generation
  gfx/       SDL3 window, SDL_Renderer, texture cache, text atlas
  ui/        Unlock screen, gallery grid, image viewer, widgets
  platform/  Config paths, SDL3 file dialogs
vendor/
  SDL3/        git submodule (3.4.10)
  monocypher/  git submodule (4.0.2-RC1)
  stb/         git submodule
tests/         Unit and integration tests (Phase 1+)
scripts/       setup.sh · gen.sh · build.sh · test.sh
```

---

## Security model

- **Cipher:** XChaCha20-Poly1305 — 192-bit random nonce per chunk, no counter state
- **KDF:** Argon2id — memory-hard, protects against GPU/ASIC password brute-force
- **Auth:** password + optional keyfile (changing the password re-wraps the master key only)
- **In-memory only:** decrypted image data is held in `mlock`'d buffers and wiped with `crypto_wipe` on vault lock or app exit — no plaintext ever touches disk
