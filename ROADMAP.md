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

## Phase 2 — Vault container

**Goal:** Implement the `.osv` file format: header, index tree, chunk store, and the core vault API.

### Tasks
- [ ] `src/vault/header.{h,cpp}` — parse/write the fixed-size plaintext header (magic, version, KDF block, master-key wrap, index slot A/B, active slot).
- [ ] `src/vault/index.{h,cpp}` — in-memory gallery tree:
  - [ ] `IndexNode` tagged union: `GalleryNode{name, children}` or `ImageNode{name, format, w, h, orig_size, created_ts, data_span, thumb_span}`.
  - [ ] Serialise / deserialise to/from a flat binary blob (versioned, hand-rolled for zero external deps).
- [ ] `src/vault/chunk_store.{h,cpp}` — append-only encrypted blob region:
  - [ ] `append_chunk(plaintext) -> ChunkSpan{offset, length}` — encrypt, write nonce+ciphertext+tag, fsync.
  - [ ] `read_chunk(span) -> std::vector<uint8_t>` — seek, read, verify tag, return plaintext.
- [ ] `src/vault/vault.{h,cpp}`:
  - [ ] `Vault::create(path, password, keyfile_opt, kdf_params)` — write fresh header, empty index.
  - [ ] `Vault::open(path)` — parse header; stay locked.
  - [ ] `Vault::unlock(password, keyfile_opt)` — Argon2id → KEK → unwrap master key → decrypt index.
  - [ ] `Vault::lock()` — wipe master key + KEK from memory.
  - [ ] `Vault::add_image(gallery_path, file_data, filename)` — chunk+encrypt image + thumbnail; update index with crash-safe double-buffer swap.
  - [ ] `Vault::read_image(node) -> SecureBuffer` — decrypt to mlock'd buffer; never touches disk.
  - [ ] `Vault::remove_image(node)` — mark as deleted in index (space reclaimed by compaction).
  - [ ] `Vault::list(gallery_path) -> std::vector<IndexNode>`.
- [ ] `tests/vault/`:
  - [ ] Create vault → add 3 images → lock → re-open → unlock → read each image → SHA-256 checksum matches original.
  - [ ] Crash simulation: truncate file mid-write; re-open should recover the previous valid index.
  - [ ] Tampered vault: flip a ciphertext byte; `read_image` must return an authentication error, not garbage data.
  - [ ] Integration test: gallery nesting — create nested galleries, verify `list()` returns correct tree.

### Acceptance criterion
All vault tests pass. A vault file created by the test can be opened, unlocked, and all images read back with matching checksums. Crash-recovery test passes.

---

## Phase 3 — Image decode & thumbnails

**Goal:** Decode images from decrypted memory buffers and generate encrypted thumbnails.

### Tasks
- [ ] `src/image/image.{h,cpp}` — `ImageData{pixels, width, height, channels, format}`; owns heap pixel buffer.
- [ ] `src/image/decode.{h,cpp}` — `decode_from_memory(std::span<const uint8_t> buf) -> ImageData` via `stb_image`. Detect format from buffer magic bytes.
- [ ] `src/image/thumbnail.{h,cpp}` — `make_thumbnail(const ImageData&, int max_side) -> ImageData` — nearest/bilinear downscale using `stb_image_resize2`.
- [ ] Wire thumbnail generation into `Vault::add_image`: decode → downscale (e.g., max 256 px) → re-encode to JPEG → encrypt → store as the image's thumb chunk.
- [ ] `tests/image/`:
  - [ ] Decode JPEG, PNG, BMP, GIF (static frame), TGA from memory buffers (ship small test fixtures).
  - [ ] Thumbnail size is ≤ max_side in both dimensions.
  - [ ] Decode of a malformed buffer returns an error, not a crash.
  - [ ] Round-trip via vault: add image → read thumb chunk → decode thumb → verify dimensions.

### Acceptance criterion
All image tests pass. A vault with 10 images (mixed JPEG/PNG) can be added and all thumbnails decoded without errors.

---

## Phase 4 — Graphics layer

**Goal:** Implement the GPU texture cache and text atlas needed by the UI.

### Tasks
- [ ] Download and commit an OFL-licensed TrueType font (e.g. [Inter Regular](https://github.com/rsms/inter)) to `assets/fonts/`.
- [ ] `src/gfx/texture_cache.{h,cpp}` — upload `ImageData` to `SDL_Texture`; LRU eviction by GPU memory budget.
- [ ] `src/gfx/text.{h,cpp}` — bake a glyph atlas from the bundled font using `stb_truetype`; `draw_text(renderer, x, y, text, colour)`.
- [ ] `src/gfx/renderer.{h,cpp}` — expand stub: `draw_image`, `draw_rect`, `draw_text`, `draw_thumbnail_strip`.
- [ ] `tests/gfx/` — headless smoke tests:
  - [ ] Font atlas bakes without crash for all printable ASCII.
  - [ ] Texture upload for a 1×1 pixel RGBA image succeeds.

### Acceptance criterion
App opens, clears, and can draw a text label and a coloured rectangle. Font atlas is visible.

---

## Phase 5 — Unlock screen & gallery grid

**Goal:** Connect the vault layer to the UI; the app can create/open/unlock a vault and browse galleries.

### Tasks
- [ ] `src/platform/paths.{h,cpp}` — `config_dir()`, `default_vault_path()`, `show_open_file_dialog()` wrapping `SDL_ShowOpenFileDialog`.
- [ ] `src/ui/input.{h,cpp}` — `InputAction` enum + `InputState` mapping SDL events → actions.
- [ ] `src/ui/widgets.{h,cpp}` — reusable: `Button`, `TextInput` (masked for passwords), `ProgressBar`, `ScrollView`.
- [ ] `src/ui/unlock_screen.{h,cpp}`:
  - [ ] Password field + keyfile picker button.
  - [ ] "Create New Vault" flow with passphrase-strength meter; offer random passphrase generation.
  - [ ] "Open Existing Vault" flow.
  - [ ] Error display for wrong password / bad keyfile.
- [ ] `src/ui/gallery_grid.{h,cpp}`:
  - [ ] Tile grid (sub-gallery tiles or thumbnail tiles, never mixed).
  - [ ] Breadcrumb navigation bar.
  - [ ] Keyboard: `Enter`/`Space` = open, `Backspace`/`Esc` = up.
  - [ ] Import button → file dialog → `Vault::add_image` → grid refresh.
- [ ] App state machine: add `Locked` and `Browsing` states; transitions on unlock/lock.
- [ ] `tests/ui/`:
  - [ ] Password-strength scoring returns expected scores for weak/medium/strong inputs.
  - [ ] Gallery grid `list()` → tile generation smoke test (headless, no render).
  - [ ] File-dialog wrapper integration test (mock SDL dialog).

### Acceptance criterion
App starts in the Locked state. Creating a vault, adding images, and navigating the gallery tree works end-to-end with keyboard and mouse.

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
