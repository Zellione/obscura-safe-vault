# Obscura-Safe-Vault — ROADMAP

> **Legend**
> - ✅ Done   🔄 In progress   ⬜ Not started
> - Each phase ends with a clear acceptance criterion that must pass before work on the next phase begins.

---

## Phase 0 — Skeleton & minimal window ✅

**Goal:** Establish the project structure, build system, and a compilable application that opens a window.

### Tasks
- [x] Choose and record the full technology stack (see `CLAUDE.md`).
- [x] Write `ROADMAP.md` and `CLAUDE.md`.
- [x] Add vendored git submodules: `vendor/SDL3`, `vendor/monocypher`, `vendor/stb`.
- [x] Write `premake5.lua` (workspace + monocypher static lib + osv app project).
- [x] Write `scripts/setup.sh` — init submodules, download `bin/premake5`, cmake-build SDL3.
- [x] Write `scripts/gen.sh` / `scripts/build.sh`.
- [x] Create module stub files: `src/crypto/`, `src/vault/`, `src/image/`, `src/ui/`, `src/platform/`.
- [x] Implement `src/gfx/window.{h,cpp}` — SDL3 window + SDL_Renderer init/shutdown/event loop.
- [x] Implement `src/app/app.{h,cpp}` + `src/app/main.cpp` — state machine, clear+present loop.

### Acceptance criterion
Running `build/bin/Debug/osv` opens a titled window that clears to a dark colour, responds to window-close and `Escape`, and exits cleanly with no errors printed to stderr.

---

## Phase 1 — Crypto core ✅

**Goal:** Implement and test the full cryptographic primitive layer.

### Tasks
- [x] `src/crypto/random.{h,cpp}` — platform CSPRNG shim: `getrandom` (Linux), `getentropy` (macOS), `BCryptGenRandom` (Windows). Exposed as `crypto::fill_random(std::span<uint8_t>)`.
- [x] `src/crypto/secure_mem.h` — `SecureBuffer<N>`: buffer with `mlock` on construction and `crypto_wipe` + `munlock` on destruction (header-only template; mlock failure is logged, not fatal).
- [x] `src/crypto/kdf.{h,cpp}` — `crypto::derive_key(password, keyfile_opt, salt, params, out_key)` wrapping `crypto_argon2` with `CRYPTO_ARGON2_ID`.
- [x] `src/crypto/aead.{h,cpp}` — `encrypt_chunk` / `decrypt_chunk` wrapping `crypto_aead_lock` / `crypto_aead_unlock` (XChaCha20-Poly1305). Generates fresh random nonce per encrypt call.
- [x] `tests/crypto/` — unit tests (run via `scripts/test.sh`):
  - [x] Known-answer tests for Argon2id (RFC 9106 §5.3 vector).
  - [x] Known-answer tests for XChaCha20-Poly1305 (draft-irtf-cfrg-xchacha-03 A.3.1 vector).
  - [x] Round-trip: encrypt → decrypt → compare plaintext (incl. empty + 1 MiB).
  - [x] Tamper detection: flip a byte in nonce / ciphertext / tag → `decrypt_chunk` fails.
  - [x] `SecureBuffer` wipe verification (storage zeroed after destruction; ASAN-clean).

### Acceptance criterion
All crypto unit tests pass. `valgrind --leak-check=full` (or ASAN + LSAN) reports no leaks. `mlock` failures are handled gracefully (logged, not fatal) on systems with low `RLIMIT_MEMLOCK`.

**Status:** ✅ 12/12 tests pass; `scripts/test.sh --asan` (ASAN+UBSan+LSan) is clean; `SecureBuffer` degrades gracefully when `mlock` is unavailable.

---

## Phase 2 — Vault container ✅

**Goal:** Implement the `.osv` file format: header, index tree, chunk store, and the core vault API.

### Tasks
- [x] `src/vault/header.{h,cpp}` — parse/write the fixed-size plaintext header (magic, version, KDF block, master-key wrap, index slot A/B, active slot).
- [x] `src/vault/index.{h,cpp}` — in-memory gallery tree:
  - [x] `IndexNode` tagged node: gallery `{name, children}` or image `{name, format, w, h, orig_size, created_ts, data_span, thumb_span}` (single tagged struct rather than a union).
  - [x] Serialise / deserialise to/from a flat binary blob (versioned, hand-rolled, bounds-checked + depth-limited for zero external deps).
- [x] `src/vault/chunk_store.{h,cpp}` — append-only encrypted blob region:
  - [x] `append_chunk(plaintext) -> ChunkSpan{offset, length}` — encrypt, write nonce+ciphertext+tag, fsync.
  - [x] `read_chunk(span)` — seek, read, verify tag, return plaintext (`std::vector` or mlock'd `SecureBytes` overload).
- [x] `src/vault/vault.{h,cpp}`:
  - [x] `Vault::create(path, password, keyfile_opt, kdf_params)` — write fresh header, empty index (returns an UNLOCKED vault).
  - [x] `Vault::open(path)` — parse header; stay locked.
  - [x] `Vault::unlock(password, keyfile_opt)` — Argon2id → KEK → unwrap master key → decrypt index (falls back to the inactive slot if the active index is corrupt).
  - [x] `Vault::lock()` — wipe master key + KEK from memory.
  - [x] `Vault::add_image(gallery_path, file_data, filename)` — chunk+encrypt image; update index with crash-safe double-buffer swap. (Thumbnails wired in Phase 3.)
  - [x] `Vault::read_image(node) -> SecureBytes` — decrypt to mlock'd buffer; never touches disk.
  - [x] `Vault::remove_image(gallery_path, filename)` — drop from index (chunk orphaned, reclaimed by Phase 7 compaction).
  - [x] `Vault::list(gallery_path) -> std::vector<const IndexNode*>`.
  - [x] `Vault::create_gallery(gallery_path)` — create (nested) galleries; enforces the leaf invariant (a gallery holds sub-galleries OR images, never both).
- [x] `tests/vault/`:
  - [x] Create vault → add 3 images → lock → re-open → unlock → read each image → BLAKE2b checksum matches original.
  - [x] Crash simulation: truncate file mid-write; re-open recovers the previous valid index via the inactive slot.
  - [x] Tampered vault: flip a ciphertext byte; `read_image` returns `AuthFailed`, not garbage data.
  - [x] Integration test: gallery nesting — create nested galleries, verify `list()` returns the correct tree across a reopen.

### Acceptance criterion
All vault tests pass. A vault file created by the test can be opened, unlocked, and all images read back with matching checksums. Crash-recovery test passes.

**Status:** ✅ 55/55 tests pass (`scripts/test.sh`); `scripts/test.sh --asan` (ASAN+UBSan+LSan) is clean. The double-buffered index swap (append+fsync → write inactive slot+fsync → flip `active_slot`+fsync) survives a truncated tail by falling back to the previous slot.

> **Notes / decisions made during implementation**
> - Crypto layer gained format-driven helpers (all tested): `crypto::seal`/`open`/`open_to` (detached, explicit-nonce AEAD for the header wrap + index slots), `crypto::decrypt_chunk_to` (decrypt into a caller buffer), and `crypto::SecureBytes` (runtime-sized mlock'd buffer for decrypted images).
> - `HEADER_SIZE` fixed at 4096 bytes; the data region begins there. The header layout follows the spec table verbatim, including the 8-byte reserved gap before slot B (158–165).
> - Checksums use Monocypher's BLAKE2b (no SHA-256 in Monocypher); tests also compare full bytes.

---

## Phase 3 — Image decode & thumbnails ✅

**Goal:** Decode images from decrypted memory buffers and generate encrypted thumbnails.

### Tasks
- [x] `src/image/image.{h,cpp}` — `ImageData{pixels, width, height, channels, format}`; owns heap pixel buffer.
- [x] `src/image/decode.{h,cpp}` — `decode_from_memory(std::span<const uint8_t> buf) -> ImageData` via `stb_image`. Detect format from buffer magic bytes.
- [x] `src/image/thumbnail.{h,cpp}` — `make_thumbnail(const ImageData&, int max_side) -> ImageData` — nearest/bilinear downscale using `stb_image_resize2`.
- [x] Wire thumbnail generation into `Vault::add_image`: decode → downscale (e.g., max 256 px) → re-encode to JPEG → encrypt → store as the image's thumb chunk.
- [x] `tests/image/`:
  - [x] Decode JPEG, PNG, BMP, GIF (static frame), TGA from memory buffers (ship small test fixtures).
  - [x] Thumbnail size is ≤ max_side in both dimensions.
  - [x] Decode of a malformed buffer returns an error, not a crash.
  - [x] Round-trip via vault: add image → read thumb chunk → decode thumb → verify dimensions.

### Acceptance criterion
All image tests pass. A vault with 10 images (mixed JPEG/PNG) can be added and all thumbnails decoded without errors.

**Status:** ✅ Merged in #4. Decode forces 3-channel RGB; `make_thumbnail` downscales with
`stb_image_resize2` and re-encodes to JPEG; `Vault::add_image` stores the thumbnail as a
separate encrypted chunk (best-effort: decode/thumb failure stores the image with
`thumb_length=0` rather than failing the add). Image tests pass under `scripts/test.sh` and ASAN.

---

## Phase 4 — Graphics layer ✅

**Goal:** Implement the GPU texture cache and text atlas needed by the UI.

### Tasks
- [x] Download and commit an OFL-licensed TrueType font to `assets/fonts/`.
- [x] `src/gfx/texture_cache.{h,cpp}` — upload `ImageData` to `SDL_Texture`; LRU eviction by GPU memory budget.
- [x] `src/gfx/text.{h,cpp}` — bake a glyph atlas from the bundled font using `stb_truetype`; `draw_text(renderer, x, y, text, colour)`.
- [x] `src/gfx/renderer.{h,cpp}` — expand stub: `draw_image`, `draw_rect`, `draw_text`, `draw_thumbnail_strip`.
- [x] `tests/gfx/` — headless smoke tests:
  - [x] Font atlas bakes without crash for all printable ASCII.
  - [x] Texture upload for a 1×1 pixel RGBA image succeeds.

### Acceptance criterion
App opens, clears, and can draw a text label and a coloured rectangle. Font atlas is visible.

**Status:** ✅ 10 new gfx tests (font bake/measure/coverage/garbage-reject + draw, texture-cache
upload/LRU-eviction/MRU-touch/clear, thumbnail-strip layout) — 88/88 total pass under
`scripts/test.sh` and ASAN+UBSan+LSan. The gfx units are tested headlessly against an SDL
software renderer (`SDL_CreateSoftwareRenderer`), so they need no display in CI. `App` now bakes
the font atlas on init and draws a coloured rectangle + text label each frame.

> **Notes / decisions made during implementation**
> - **Bundled font:** the environment had no network access, so the bundled OFL/permissive font is
>   **Noto Sans Regular** (`assets/fonts/NotoSans-Regular.ttf`, license in `NotoSans-LICENSE.txt`)
>   rather than Inter. Swappable via the `OSV_DEFAULT_FONT` compile define.
> - `FontAtlas::bake()` is pure CPU (8-bit alpha coverage bitmap + per-glyph metrics) and grows a
>   square atlas 256→2048 until all printable ASCII (32–126) fits; the `SDL_Texture` is created
>   lazily on the first `draw_text()`. `measure()` rounds each glyph advance independently so it is
>   exactly additive.
> - `TextureCache` keys on a caller-supplied `uint64_t`, accounts each entry as `w*h*4` GPU bytes,
>   and evicts least-recently-used entries past the budget (default 256 MiB).
> - premake: SDL3 linkage was factored into a shared `link_sdl3()` helper; `osv_tests` now compiles
>   the headless-testable gfx units (`texture_cache`, `text`, `renderer`) and links SDL3.

---

## Phase 5 — Unlock screen & gallery grid ✅

**Goal:** Connect the vault layer to the UI; the app can create/open/unlock a vault and browse galleries.

### Tasks
- [x] `src/platform/paths.{h,cpp}` — `config_dir()`, `default_vault_path()`, `read_file()`. The open-file dialog became its own async wrapper, `src/platform/file_dialog.{h,cpp}`, over `SDL_ShowOpenFileDialog`.
- [x] `src/ui/input.{h,cpp}` — `InputAction` enum + `map_key()` mapping SDL keycodes → actions.
- [x] `src/ui/widgets.{h,cpp}` — pure layout/hit-testing (`grid_columns`, `grid_cell_rect`, `grid_hit`, `fit_rect`, `point_in_rect`) + thin `draw_button`/`draw_text_field` helpers. Masked password entry lives in `src/ui/secure_text_field.{h,cpp}` (an mlock'd buffer). (`ProgressBar`/`ScrollView` deferred — not needed by the Phase 5 screens.)
- [x] `src/ui/unlock_screen.{h,cpp}`:
  - [x] Password field + keyfile picker button.
  - [x] "Create New Vault" flow. (Passphrase-strength meter + random generation deferred to Phase 7, per `CLAUDE.md`.)
  - [x] "Open Existing Vault" flow (with "Open other…" file picker).
  - [x] Error display for wrong password / bad keyfile.
  - [x] Submit validation extracted to a pure `src/ui/unlock_logic.{h,cpp}` (`decide_submit`).
- [x] `src/ui/gallery_grid.{h,cpp}`:
  - [x] Tile grid (sub-gallery tiles or thumbnail tiles, never mixed — enforces the leaf invariant on import/create).
  - [x] Breadcrumb navigation bar (path state in `src/ui/nav_model.{h,cpp}`).
  - [x] Keyboard: `Enter`/`Space` = open, `Backspace`/`Esc` = up; `I` = import, `N` = new gallery.
  - [x] Import button → file dialog → `Vault::add_image` → grid refresh.
- [x] App state machine: `src/ui/screen.h` `Screen` interface + `Nav` transitions; `App` owns one active screen and swaps unlock ↔ gallery on `take_nav()`.
- [x] `tests/ui/`:
  - [x] Submit-logic scoring (`decide_submit`): empty / mismatch / unlock / create cases.
  - [x] `NavModel` path split/join, enter/up, selection clamp (headless).
  - [x] Widget layout/hit-test (`grid_columns`/`grid_cell_rect`/`grid_hit`/`fit_rect`), input mapping, and `SecureTextField` wipe verification.

### Acceptance criterion
App starts in the Locked (unlock) state. Creating a vault, adding images, and navigating the gallery tree works end-to-end with keyboard and mouse.

**Status:** ✅ Merged in #6. 110/110 tests pass under `scripts/test.sh` and ASAN+UBSan+LSan;
the SonarCloud quality gate is green with zero open issues. The UI is built around a small
`Screen` state machine (`UnlockScreen` ↔ `GalleryGrid`), with all decision logic factored into
pure, headlessly-tested units (`unlock_logic`, `nav_model`, `widgets`, `input`,
`secure_text_field`).

> **Notes / decisions made during implementation**
> - **Async file dialogs:** `SDL_ShowOpenFileDialog` is callback-based and may fire on another
>   thread, so `platform::FileDialog` parks results in a mutex-guarded slot delivered to the main
>   thread via `take_result()` once per frame. It is owned by `App` for the whole run because the
>   callback captures `this`.
> - **Password buffer:** `SecureTextField` holds the typed password in an mlock'd buffer and
>   `crypto_wipe`s it on `clear()` (invariant #2), so plaintext passwords never land in a plain
>   `std::string`.
> - **Passphrase-strength meter** and **random passphrase generation** are intentionally deferred
>   to Phase 7 (Hardening & polish), matching the deferral table in `CLAUDE.md`.

---

## Phase 6 — Image viewer

**Goal:** Full-screen image viewing with zoom/pan and the auto-scrolling thumbnail strip.

### Tasks
- [ ] `src/ui/image_viewer.{h,cpp}`:
  - [ ] Top ~75%: big image rendered via `gfx::Renderer::draw_image` with zoom + pan.
    - [ ] Fit-to-window on first display.
    - [ ] Mouse wheel / `+` / `-`: zoom in/out centred on cursor.
    - [ ] Drag (LMB held) or arrow keys (when zoomed): pan.
  - [ ] Bottom ~25%: horizontal thumbnail strip.
    - [ ] Scrolls to centre the current image's thumbnail.
    - [ ] Current thumbnail highlighted (border / tint).
    - [ ] Click or `Left`/`Right` arrow: change current image; strip auto-scrolls.
  - [ ] `Up` / `Esc`: back to gallery grid.
- [ ] App state machine: add `Viewing` state.
- [ ] `tests/ui/`:
  - [ ] Zoom clamped to sane min/max (e.g., 5%–2000%).
  - [ ] Pan clamped so image cannot be dragged entirely off-screen.
  - [ ] `Left`/`Right` wrap correctly at gallery boundaries (first/last image).
  - [ ] Thumbnail-strip scroll position is correct for galleries of various sizes.

### Acceptance criterion
Open a vault, navigate to a leaf gallery, click an image: viewer opens. Arrow keys navigate; zoom/pan work. Thumbnail strip scrolls and highlights correctly.

---

## Phase 7 — Hardening & polish

**Goal:** Close security gaps, handle edge cases, and add deletion + compaction.

### Tasks
- [ ] **Crash-safe index swap** — verify double-buffer logic with injected write failures.
- [ ] **Compaction** — `Vault::compact()`: copy live chunks to a new file, rewrite header and index; rename atomically. Run on request or when waste exceeds a threshold.
- [ ] **Delete image** — `Vault::remove_image` + trigger compaction.
- [ ] **Password change** — re-wrap master key with new Argon2id-derived KEK; no re-encryption of data chunks.
- [ ] **Passphrase strength meter** — on vault creation; classify weak/medium/strong; offer random diceware passphrase generation.
- [ ] **Keyfile flow** — full UX: create keyfile, select on unlock, clear error on wrong keyfile.
- [ ] **Secure-memory audit** — review all code paths: no key material on the stack without wipe, no decrypted pixel data outside mlock'd buffers.
- [ ] **Fuzz testing** — feed malformed `.osv` files to `Vault::open` and `decode_from_memory`; must not crash.
- [ ] `tests/vault/` — add compaction, delete, and password-change tests.

### Acceptance criterion
Fuzz test runs 10,000 malformed inputs without crashing. Delete + compact cycle reduces file size. Password change succeeds without re-encrypting data chunks.

---

## Phase 8 — Cross-platform ports

**Goal:** Windows and macOS build configs and CI pipeline.

### Tasks
- [ ] **Windows** — premake5.lua `filter "system:windows"` config; link against vendored SDL3 static build (cmake in `setup.bat`); test on Windows 10/11.
- [ ] **macOS** — premake5.lua `filter "system:macosx"` + Xcode4 generator; SDL3 cmake build; HiDPI handling; test on macOS 13+.
- [ ] **CI** — GitHub Actions matrix: Linux (gcc + clang), Windows (MSVC), macOS (Clang/AppleClang). Run all tests on each.
- [ ] **Packaging** — Linux: AppImage or `.tar.gz` + `install.sh`; Windows: NSIS or WiX installer; macOS: `.app` bundle.
- [ ] `scripts/setup.bat` — Windows equivalent of `setup.sh`.
- [ ] Update `CLAUDE.md` with platform-specific build notes.

### Acceptance criterion
CI passes on all three platforms. A developer can clone the repo on Windows or macOS and build a working app with a single setup script.

---

## Phase 9 — Extra image formats

**Goal:** Add WebP and HEIC/AVIF support.

### Tasks
- [ ] **WebP** — add `vendor/libwebp` submodule; update `scripts/setup.sh` to cmake-build it; add `decode_webp_from_memory`.
- [ ] **HEIC/AVIF** — add `vendor/libheif` and codec submodules; update build; add `decode_heic_from_memory` / `decode_avif_from_memory`.
- [ ] Update `src/image/format_registry.{h,cpp}` to detect and dispatch by format magic.
- [ ] `tests/image/` — add WebP and HEIC/AVIF decode tests with small test fixtures.

### Acceptance criterion
WebP and HEIC/AVIF images can be imported and displayed. All existing tests still pass.

---

## Phase 10+ — Future ideas

These are intentionally unscoped. Each deserves its own planning session before work begins.

| Idea | Notes |
|---|---|
| **Video playback** | ffmpeg/libav; streaming decode directly from encrypted chunks (no temp file); audio output; seek bar. Large scope. |
| **Tags & search** | Per-image metadata; tag-based filtering; full-text search over filenames/tags. |
| **Multiple vaults** | Vault manager screen; open several vaults simultaneously; move images between vaults. |
| **Export** | Selectively export decrypted images to a directory (with explicit user consent). |
| **Slideshow mode** | Auto-advance with configurable interval and transition. |
| **Remote vaults** | `.osv` file on a network share or cloud storage; read-only streaming mode. |

---

## Container format spec (reference)

Reproduced from `CLAUDE.md` for quick access during vault implementation work.

```
Offset  Size  Description
──────  ────  ───────────────────────────────────────────────────────────────
0       8     magic: "OSVAULT\0"
8       2     version (u16, currently 1)
10      2     header_size (u16, total header length in bytes)
12      4     flags (u32, reserved)

16      1     kdf_algo  (0 = Argon2id)
17      4     t_cost    (u32, Argon2id time cost)
21      4     m_cost_kib (u32, Argon2id memory cost in KiB)
25      4     parallelism (u32)
29      16    salt      (u8[16], random)
45      1     keyfile_required (u8, 0 or 1)

46      24    master_key_nonce (u8[24])
70      32    wrapped_master_key (u8[32], XChaCha20-Poly1305 ciphertext)
102     16    master_key_tag (u8[16], Poly1305 tag)

118     8     slot_a_offset (u64)
126     8     slot_a_length (u64)
134     24    slot_a_nonce  (u8[24])
166     8     slot_b_offset (u64)
174     8     slot_b_length (u64)
182     24    slot_b_nonce  (u8[24])
206     1     active_slot   (u8, 0 = A, 1 = B)

207     N     reserved padding (zeroes, up to header_size)
```

**Data region** starts at `header_size`. Each encrypted chunk is laid out as:
```
  nonce[24] | ciphertext[plaintext_len] | tag[16]
```

**Index tree node** (binary serialised):
```
  node_type  u8  (0 = gallery, 1 = image)
  name_len   u16
  name       u8[name_len]  (UTF-8)

  if gallery:
    child_count  u32
    children     node[child_count]  (recursive)

  if image:
    format       u8  (0=JPEG, 1=PNG, 2=GIF, 3=BMP, 4=TGA, 5=HDR, 6=WebP, 7=HEIC, 8=AVIF)
    width        u32
    height       u32
    orig_size    u64  (plaintext bytes)
    created_ts   u64  (Unix timestamp, seconds)
    data_offset  u64
    data_length  u64
    thumb_offset u64
    thumb_length u64
```
