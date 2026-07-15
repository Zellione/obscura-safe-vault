# Obscura-Safe-Vault

> ⚠️ **This is an AI-driven project, vibe-coded for educational purposes.** Every
> line was written with AI assistance and the design decisions live in
> [`CLAUDE.md`](CLAUDE.md). Do not trust it with data you cannot afford to lose.

A multi-platform native encrypted photo gallery. All photos live inside a single `.osv` vault file — images are decrypted **into locked memory only**, never written to a temporary file or disk. The gallery is browsable with a freely-nestable folder tree, a zoomable full-screen image viewer, and a thumbnail strip navigable with arrow keys.

**Stack:** C++20 · SDL3 · SDL_Renderer · Monocypher (XChaCha20-Poly1305 + Argon2id) · stb_image · libwebp / libheif (WebP / HEIC / AVIF) · FFmpeg decode-only (H.264 / H.265 / ProRes / DNxHD / MJPEG / VP8 / VP9 video, AAC / Opus / MP3 / Vorbis / FLAC / AC-3 audio) · premake5 → Ninja

See [`CLAUDE.md`](CLAUDE.md) for all technology decisions and [`ROADMAP.md`](ROADMAP.md) for the full development plan.

---

## Building

### Prerequisites

`setup.sh` cmake-builds the vendored dependencies from source, so the following must be on `PATH`:

| Tool | Why | Install |
|---|---|---|
| C++20 compiler | building the app | gcc 14+ / clang 17+ / MSVC 2022 / AppleClang |
| `cmake`, `ninja` | configure + build vendored libs | Arch: `sudo pacman -S cmake ninja` · Debian/Ubuntu: `sudo apt install cmake ninja-build` · macOS: `brew install cmake ninja` · Windows: VS 2022 + `choco install ninja` |
| `nasm` | assembler for the vendored **libaom** (AVIF decode) | Arch: `sudo pacman -S nasm` · Debian/Ubuntu: `sudo apt install nasm` · macOS: `brew install nasm` · Windows: `choco install nasm` |

### First-time setup

Initialises git submodules, downloads the `premake5` binary, and cmake-builds the vendored static libraries (SDL3, plus the image codecs libwebp / libde265 / libaom / libheif into `vendor/codecs-prefix/`).

```bash
scripts/setup.sh         # Linux/macOS
scripts\setup.bat        # Windows (VS 2022 Developer prompt)
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
  libwebp/     git submodule — WebP decode
  libde265/    git submodule — HEIC (HEVC) decode
  libaom/      git submodule — AVIF (AV1) decode
  libheif/     git submodule — HEIC/AVIF container
tests/         Unit and integration tests (Phase 1+)
scripts/       setup.sh · build_codecs.sh · gen.sh · build.sh · test.sh
```

---

## Advanced search

The advanced-search screen (`Shift+/`) builds a structured tag query. A candidate
(image or gallery) is matched against its **effective tags** — its own tags plus
any inherited from parent galleries. Tag comparisons are **case-insensitive and
exact**; the name query is a **case-insensitive substring**.

A query has four independent clauses, and a candidate matches only if **all of
them** pass (they are AND-ed together):

| Clause | Role | Boolean shape |
|---|---|---|
| **Include** | positive filter **and** relevance ranking | flat **OR** of weighted tags |
| **Exclude** | hard negative filter (veto) | flat **NOR** |
| **Groups** | structured boolean expression | (AND/OR) of (AND/OR) |
| **Name** | display-name substring | substring match |

Formally, a candidate matches **iff**:

```
NOT(any Exclude tag present)                 ← any single match rejects outright
AND (Include empty OR ≥1 Include tag present)
AND (Name empty OR Name is a substring)
AND (Groups empty OR the top-level group join holds)
```

- **Include** is a flat **OR-gate**: `Include: cyberpunk(1) jinx(2)` means "has
  *cyberpunk* **or** *jinx*". It also drives **ranking** — each present include
  tag adds its weight to the candidate's score, so heavier tags float their
  matches to the top of the result list. An empty include list matches everything.
- **Exclude** is a hard veto: `Exclude: spoilers nsfw` rejects any candidate
  carrying *spoilers* **or** *nsfw*, regardless of the other clauses. It never
  contributes to the score — it only removes.
- **Groups** are where nested boolean logic lives. Each group combines its own
  tags with its own combinator (**AND** or **OR**), and the groups combine with
  each other via the top-level **Join groups** (**AND** or **OR**) — one level of
  nesting. For example, `Join groups: AND` over `Group A (OR): cyberpunk jinx`
  and `Group B (AND): fanart hires` evaluates as
  `(cyberpunk OR jinx) AND (fanart AND hires)`.

Include and Exclude are intentionally kept **outside** the groups as flat,
single-purpose controls for the common case ("show anything tagged X, but never
Y"), with Include additionally carrying the weights that rank results — something
groups have no concept of. Reach for **Groups** only when you need genuinely
structured logic like `(a OR b) AND (c OR d)`.

An all-empty query matches every candidate (mirroring a blank search box).
Queries can be persisted as **saved searches** (`Ctrl+S`); the scope toggle
restricts results to images, galleries, or both.

**Editing tags:** within a focused `Include`, `Exclude`, or `Groups` field, type to
add a tag (Up/Down pick an autocomplete suggestion, Enter adds it). With the edit
box empty, Up/Down highlight an already-added tag; **Del**/**Backspace** removes the
highlighted tag and **Enter** pulls it back into the box to edit (re-adding it
appends it at the end). In `Groups`, Left/Right switch between groups and Del (with
nothing highlighted) toggles the current group's AND/OR.

Tags are kept **unique** within each list (case-insensitively) — re-adding an
existing `Include` tag just updates its weight, and a `Group` ignores a tag it
already holds. `Include` and `Exclude` are **mutually exclusive**: adding a tag to
one removes it from the other, so a tag is never both included and excluded.

---

## Security model

- **Cipher:** XChaCha20-Poly1305 — 192-bit random nonce per chunk, no counter state
- **KDF:** Argon2id — memory-hard, protects against GPU/ASIC password brute-force
- **Auth:** password + optional keyfile (changing the password re-wraps the master key only)
- **In-memory only:** decrypted image data is held in `mlock`'d buffers and wiped with `crypto_wipe` on vault lock or app exit — no plaintext ever touches disk
