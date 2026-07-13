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

## Phase 6 — Image viewer ✅

**Goal:** Full-screen image viewing with zoom/pan and the auto-scrolling thumbnail strip.

### Tasks
- [x] `src/ui/image_viewer.{h,cpp}`:
  - [x] Top ~75%: big image rendered via `gfx::Renderer::draw_image` with zoom + pan.
    - [x] Fit-to-window on first display.
    - [x] Mouse wheel / `+` / `-`: zoom in/out centred on cursor.
    - [x] Drag (LMB held) or arrow keys (when zoomed): pan.
  - [x] Bottom ~25%: horizontal thumbnail strip.
    - [x] Scrolls to centre the current image's thumbnail.
    - [x] Current thumbnail highlighted (border / tint).
    - [x] Click or `Left`/`Right` arrow: change current image; strip auto-scrolls.
  - [x] `Up` / `Esc`: back to gallery grid.
- [x] App state machine: add `Viewing` state.
- [x] `tests/ui/`:
  - [x] Zoom clamped to sane min/max (e.g., 5%–2000%).
  - [x] Pan clamped so image cannot be dragged entirely off-screen.
  - [x] `Left`/`Right` wrap correctly at gallery boundaries (first/last image).
  - [x] Thumbnail-strip scroll position is correct for galleries of various sizes.

### Acceptance criterion
Open a vault, navigate to a leaf gallery, click an image: viewer opens. Arrow keys navigate; zoom/pan work. Thumbnail strip scrolls and highlights correctly.

**Status:** ✅ 116/116 tests pass under `scripts/test.sh` and ASAN+UBSan+LSan; the app
builds and links. All zoom/pan/strip math is factored into a pure, SDL-free,
headlessly-tested unit (`src/ui/viewer_model.h`), mirroring the `nav_model` /
`unlock_logic` / `widgets` pattern; `ImageViewer` owns only the SDL plumbing.

> **Notes / decisions made during implementation**
> - **Pan clamp.** Rather than the looser "not entirely off-screen", the clamp keeps the
>   image *contained*: when scaled larger than the viewport it always covers it (no
>   background gap); when smaller it stays fully inside. Both reduce to the symmetric
>   bound `|pan| <= |scaled - view| / 2`.
> - **Arrow-key overloading.** `Left`/`Right` change image when fit-to-window, but pan
>   when zoomed in (per the ROADMAP's "arrow keys (when zoomed): pan"). `Esc` is always
>   "back"; `Up` is "back" only when not zoomed (it pans up when zoomed). `0` re-fits.
> - **Full-image texture** is owned by the viewer and rebuilt only when the current image
>   changes — it is kept out of the shared `TextureCache` so a single large decode can't
>   evict every gallery thumbnail. Thumbnails still share the cache (keyed by
>   `data_offset`) with the gallery grid. The decrypted original lives only in a transient
>   mlock'd `SecureBytes` during decode (invariant #1).
> - **Nav payload.** `ui::Nav` gained `path` + `index` so transitions carry context:
>   `ToViewer` opens a leaf gallery at an image index, and the viewer's `ToGallery` return
>   restores the grid at the same path with the viewed image selected.

---

## Phase 7 — Hardening & polish ✅

**Goal:** Close security gaps, handle edge cases, and add deletion + compaction.

### Tasks
- [x] **Crash-safe index swap** — verify double-buffer logic with injected write failures.
- [x] **Compaction** — `Vault::compact()`: copy live chunks to a new file, rewrite header and index; rename atomically. Run on request or when waste exceeds a threshold.
- [x] **Delete image** — `Vault::remove_image` + trigger compaction.
- [x] **Password change** — re-wrap master key with new Argon2id-derived KEK; no re-encryption of data chunks.
- [x] **Passphrase strength meter** — on vault creation; classify weak/medium/strong; offer random diceware passphrase generation.
- [x] **Keyfile flow** — full UX: create keyfile, select on unlock, clear error on wrong keyfile.
- [x] **Secure-memory audit** — review all code paths: no key material on the stack without wipe, no decrypted pixel data outside mlock'd buffers.
- [x] **Fuzz testing** — feed malformed `.osv` files to `Vault::open` and `decode_from_memory`; must not crash.
- [x] `tests/vault/` — add compaction, delete, and password-change tests.

### Acceptance criterion
Fuzz test runs 10,000 malformed inputs without crashing. Delete + compact cycle reduces file size. Password change succeeds without re-encrypting data chunks.

**Status:** ✅ 146/146 tests pass under `scripts/test.sh` and ASAN+UBSan+LSan. The seeded
fuzz suite covers 10,000+ malformed inputs (4,000 hostile `.osv` files against
`Vault::open`/`unlock`, 3,000 index blobs against `deserialize_index`, 3,000 image buffers
against `decode_from_memory`) and runs as part of the normal test suite. Compaction tests
verify file shrinkage, content checksums, thumbnail carry-over, and cold reopen; password
change is verified to leave data-chunk bytes on disk untouched.

> **Notes / decisions made during implementation**
> - **Injected write failures.** `fileutil::sync()` gained a tiny arm-once fault-injection
>   counter (`inject_sync_failure`) — there is no portable way to make a real fsync fail on
>   demand. Tests fail the commit at *every* sync step and verify the vault always reopens
>   with a valid index (the previous one, or the new one if the flip was already written).
>   On-disk corruption tests additionally cover torn index blobs and the
>   crash-between-step-B-and-C header state.
> - **Compaction copies ciphertext verbatim.** Live chunks are byte-identical copies
>   (`nonce|ciphertext|tag`) — same key, same plaintext, so no information leak and no
>   pointless decrypt/re-encrypt pass. The new file is fully written + fsync'd, then
>   atomically renamed over the original (plus best-effort parent-dir fsync), then the
>   vault's handle is reopened. `wasted_bytes()` reports reclaimable space;
>   `remove_image` auto-compacts when waste ≥ 256 KiB *and* ≥ ¼ of the file.
> - **Fuzzing found a real OOM-DoS:** chunk/index lengths from corruptible metadata were
>   used to size buffer allocations *before* any bounds check (`ChunkStore::read_chunk`/
>   `read_raw`), so a hostile length meant a `bad_alloc` abort. Reads are now bounds-checked
>   against the file size before allocating. Relatedly, `Header::parse` now rejects hostile
>   KDF parameters (the Argon2 work area is attacker-sized otherwise; bounds: t_cost ≤ 512,
>   m_cost ≤ 4 GiB, lanes ≤ 64, nb_blocks ≥ 8·lanes) and out-of-range `active_slot`, and
>   `derive_key` survives work-area allocation failure instead of throwing.
> - **Passphrase module** (`ui/passphrase.{h,cpp}`): char-class entropy estimate
>   (Weak < 50 bits ≤ Medium < 80 ≤ Strong) + generation from an embedded 256-word list —
>   one CSPRNG byte per word (zero modulo bias), 8 words ≈ 64 bits by default. Generated
>   bytes go straight into the mlock'd `SecureTextField`; the unlock screen renders the
>   revealed phrase via `string_view` over that buffer (no unlocked copy).
> - **Keyfile creation** (`platform::write_new_keyfile`): 64 CSPRNG bytes via
>   `SDL_ShowSaveFileDialog`; refuses to overwrite an existing file (clobbering a keyfile
>   would permanently lock every vault bound to it).
> - **Secure-memory audit fixes:** `platform::read_file` (carries keyfiles) now sizes the
>   buffer once instead of chunk-growing — reallocation used to strew stale key-material
>   copies across freed heap blocks, and a 64 KiB stack scratch went unwiped;
>   `derive_key`'s password‖keyfile scratch moved from a wiped-but-swappable heap vector
>   into mlock'd `SecureBytes`. *Accepted deviations:* decoded RGB pixels (stb_image
>   output) live in regular heap `ImageData` — mlocking them needs a custom `STBI_MALLOC`
>   allocator (revisit if warranted); index blobs (filenames/metadata) are plain vectors —
>   metadata, not key material; the keyfile passes through one wiped (non-mlock'd) vector
>   in the unlock flow.
> - **Known limitation (documented, not fixed):** the header's master-key wrap is a single
>   copy — a torn 4 KiB header write during `change_password` could destroy the wrap. A
>   crash-safe wrap swap would need a format change (double-buffered wrap), deferred.

---

## Phase 8 — Cross-platform ports ✅

**Goal:** Windows and macOS build configs and CI pipeline.

### Tasks
- [x] **Windows** — premake5.lua `filter "system:windows"` config; link against vendored SDL3 static build (cmake in `setup.bat`); tested on the windows-latest CI runner (Windows Server 2022+, same toolchain as Windows 10/11).
- [x] **macOS** — premake5.lua `filter "system:macosx"` (frameworks + native arch); SDL3 cmake build; HiDPI handling; tested on the macos-latest CI runner (macOS 14+, arm64).
- [x] **CI** — GitHub Actions matrix: Linux (gcc + clang), Windows (MSVC), macOS (AppleClang). All tests run on each.
- [x] **Packaging** — Linux: `.tar.gz` + `install.sh`; Windows: NSIS installer; macOS: `.app` bundle (ad-hoc signed).
- [x] `scripts/setup.bat` — Windows equivalent of `setup.sh`.
- [x] Update `CLAUDE.md` with platform-specific build notes.

### Acceptance criterion
CI passes on all three platforms. A developer can clone the repo on Windows or macOS and build a working app with a single setup script.

**Status:** ✅ CI green on Linux (gcc+clang × Debug/Release + ASAN gate), Windows
(MSVC × Debug/Release), and macOS (AppleClang × Debug/Release); Release legs upload a
Linux tarball, an NSIS installer, and a zipped `.app` bundle as artifacts.

> **Notes / decisions made during implementation**
> - **HiDPI** was already handled since Phase 0: every window is created with
>   `SDL_WINDOW_HIGH_PIXEL_DENSITY` (window.cpp), and the .app bundle's Info.plist sets
>   `NSHighResolutionCapable`.
> - **macOS generator:** CI uses `gmake2` (battle-tested) rather than the ROADMAP's
>   suggested Xcode4 generator; `premake5 xcode4` remains available for IDE users.
>   macOS builds the native architecture (no forced x86_64) so arm64 machines link
>   against the natively-built vendored SDL3.
> - **Windows:** built via `premake5 vs2022` + MSBuild. Release builds `osv` as a
>   `WindowedApp` (no console) keeping the standard `main()` entrypoint. The MSVC
>   branch of `crypto/random.cpp` had a latent compile error (invalid `{:lx}` format
>   spec, never built on Linux) — fixed.
> - **Asset path portability:** packaged apps aren't launched from the repo root, so
>   the font now falls back to `SDL_GetBasePath()` (exe dir; `Contents/Resources/` in
>   a mac bundle) when the cwd-relative path misses. install.sh symlinks `assets/`
>   next to the installed binary for the same reason.
> - **Code-signing:** the .app is ad-hoc signed (`codesign -s -`); Developer-ID
>   signing + notarisation need an Apple Developer account and are deferred.
> - **Script portability:** `nproc` (Linux-only) and `${VAR,,}` (bash 4; macOS ships
>   3.2) were removed from the shell scripts.

---

## Phase 9 — Extra image formats ✅

**Goal:** Add WebP and HEIC/AVIF support.

### Tasks
- [x] **WebP** — add `vendor/libwebp` submodule; cmake-build it into the codec prefix; add `decode_webp_from_memory`.
- [x] **HEIC/AVIF** — add `vendor/libde265`, `vendor/libaom`, `vendor/libheif` submodules; update build; add `decode_heif_from_memory` (one libheif entry point decodes both HEIC and AVIF).
- [x] Add `src/image/format_registry.{h,cpp}` to detect and dispatch by container magic (WebP RIFF, ISO-BMFF `ftyp` brands).
- [x] `tests/image/` — WebP/HEIC/AVIF decode tests with committed fixtures, malformed-buffer rejection, and a vault round-trip import/read-back.

### Acceptance criterion
WebP and HEIC/AVIF images can be imported and displayed. All existing tests still pass.

**Status:** ✅ Decode-only support via vendored-from-source static libs. `decode_from_memory`
dispatches on `format_registry::detect_format`: stb_image (JPEG/PNG/GIF/BMP/TGA/HDR),
libwebp (WebP), libheif (HEIC via libde265, AVIF via libaom). 154/154 tests pass under
`scripts/test.sh` and `--asan`; the decoders run under ASAN/UBSan on untrusted input in CI.

> **Notes / decisions made during implementation**
> - **Vendoring:** all four codecs (libwebp, libde265, libaom, libheif) are git submodules,
>   cmake-built static and `cmake --install`ed into one staging prefix
>   `vendor/codecs-prefix/` (gitignored). `scripts/build_codecs.{sh,bat}` does the build and
>   is shared by `setup.{sh,bat}` and CI; `premake5.lua`'s `link_image_codecs()` links the
>   set in dependency order (`heif → de265 → aom → webp → sharpyuv`).
> - **AVIF backend = libaom** (decoder-only, `-DCONFIG_AV1_ENCODER=0`), cmake-built like the
>   rest — no meson/dav1d toolchain added. **libaom bumped 3.9.1 → 3.14.1** because 3.9.1's
>   `test_nasm` probe rejects nasm 3.x. libaom needs **nasm** on PATH (added to CI + the
>   README prerequisites for every OS).
> - **CMake 4.x:** libde265 1.0.15 declares a pre-3.5 `cmake_minimum_required`, so the codec
>   build passes `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`.
> - **No new on-disk plaintext:** decoded pixels stay in the same transient `ImageData`
>   buffers as the stb path; the security invariants are unchanged.
> - **ASAN job:** now builds the codecs too (running the C decoders under the sanitisers is
>   the highest-value place to have them). The old "no SDL3 in the ASAN job" note was stale —
>   the ASAN leg has provisioned SDL3 since Phase 4 — and was removed from CLAUDE.md.

---

## UI Overhaul — modern restyle, strip toggle & fill-scroll ✅

**Goal:** Modernise the look of all screens and extend the image viewer. Shipped on
the `feat/ui-overhaul` branch (a feature, not a numbered phase).

### Tasks
- [x] `src/gfx/theme.h` — centralised "Refined Slate" colour tokens; replaced the
  scattered inline colour literals across every screen and widget.
- [x] `gfx::Renderer::draw_round_rect` + `draw_selection_glow` (via `SDL_RenderGeometry`)
  and a pure, unit-tested `round_rect_outline` helper. Tiles, fields, buttons and
  panels are now rounded; selection uses an accent glow.
- [x] `draw_button` finally renders its hover/active states; `ui::button_state`
  (pure, tested) wires live mouse hover/press on the unlock screen.
- [x] Image-viewer thumbnails halved (`src/ui/strip_layout.{h,cpp}`,
  `strip_thumb_size`).
- [x] **Strip-position toggle** (`T`): bottom (horizontal) ↔ left (vertical),
  orientation-aware layout/scroll/hit-testing; persists for the session.
- [x] **Fill-width continuous scroll** view mode (`F`): images scaled to viewport
  width, wheel scrolls vertically across the whole leaf gallery, active thumbnail
  tracks the viewport centre. Pure `src/ui/scroll_model.{h,cpp}` drives the maths;
  only the on-screen images + immediate neighbours are decoded (bounded texture
  set, decrypted into locked memory, wiped after GPU upload — never to disk).
- [x] **Gallery responsiveness & detailed list view**: `Window::width()/height()`
  now report the live renderer output size, so the grid reflows on resize and is
  centred; thumbnails are aspect-fit on black (no stretch); filenames are
  middle-elided to fit (`ui::elide_middle`). A `L` key toggles a detailed list
  view with very small thumbnails + columns (name, dimensions, size, type, date)
  formatted by pure `src/ui/meta_format.{h,cpp}`.
- [x] **Text centring**: `FontAtlas::text_top_for_center` centres text by real
  glyph extents (the font bakes at 28px with baseline = y + px), used by buttons,
  fields, list rows and headers.

### Acceptance criterion
All screens use the shared theme; the viewer supports half-size thumbnails, the
bottom/left strip toggle, and the fill-width scroll mode. All tests pass.

**Status:** ✅ Pure logic (strip layout, scroll model, button state, rounded-rect
tessellation) is TDD-covered; 174/174 tests pass under `scripts/test.sh` and `--asan`.
Security invariants unchanged — the fill-scroll texture window reuses the existing
`SecureBytes` decrypt-then-wipe path.

> **Notes / decisions made during implementation**
> - **Strip side & view mode** reset to defaults (`Bottom`, `Fit`) when the viewer
>   is re-entered from the gallery; they persist only while the viewer is open.
> - **Bottom strip** keeps its full bar height; the smaller thumbnails just sit
>   centred in it.
> - **Scroll heights** come from `ImageMeta.width/height`, so the scroll model is
>   built without decrypting any image up front.

---

## Phase 10 — Export (selective, hard-gated) ✅

**Goal:** Let the user deliberately extract decrypted images out of the vault to
ordinary files on disk, with explicit per-export consent. This is the **one
feature that intentionally breaks security invariant #1** ("no plaintext to
disk"); it is gated and documented as a deliberate deviation, never a silent or
bulk operation.

### Tasks
- [x] **Selection model** — multi-select in the gallery grid (`Space` toggles, `Esc` clears), plus an "export this image" action from the viewer. Pure, headlessly-tested selection state (no SDL).
- [x] **Consent dialog** — a modal confirmation widget that names the danger explicitly ("Exported files are written **decrypted** to disk, outside the vault's protection") and requires an explicit confirm; cancel/deny is the default focus. Reuse `gfx::Renderer` round-rect/panel primitives.
- [x] `src/platform/folder_dialog.{h,cpp}` — async destination-folder picker over `SDL_ShowOpenFolderDialog`, mirroring the existing `file_dialog` mutex-guarded result pattern.
- [x] **Export writer** — for each selected image: decrypt the **original stored bytes** into an mlock'd `SecureBytes`, write them verbatim to `dest/<original_filename>`, `crypto_wipe` the buffer immediately after the write. Thumbnails are never exported. Name-collision handling appends ` (n)` rather than overwriting.
- [x] **No bulk-tree export** — only the current explicit selection (or a single viewer image) is ever written; there is no "export entire vault" path.
- [x] Update `CLAUDE.md`: record export as a documented, gated deviation from invariant #1; add `src/platform/folder_dialog.*` and the export module to the module layout.
- [x] `tests/` — exported files are byte-identical to the originally-imported bytes; collision suffixing; declining the consent dialog (`ExportConsent::Cancel`) writes **zero** files; a wiped-buffer assertion after each write.

### Acceptance criterion
Selecting N images and confirming the export produces exactly N files on disk
whose checksums match the originally-imported bytes; declining the confirmation
writes nothing; thumbnails are never emitted.

**Status:** ✅ 193/193 tests pass under `scripts/test.sh` and `--asan` (11 new:
4 selection-model + 7 export). The export core is SDL-free and TDD-covered
(`src/ui/export.*`, `selection_model.*`); the consent modal (`consent_dialog.*`),
folder picker (`platform/folder_dialog.*`), gallery multi-select (`Space`/`X`),
and viewer single-image export (`X`) are the SDL plumbing on top.

> **Design note.** Export deliberately violates invariant #1. The mitigation is
> *consent + scope*: a per-export warning, default-cancel, and selection-only
> output. The decrypted bytes still live only in mlock'd memory right up to the
> `write()` call, and the buffer is wiped immediately after.

> **Notes / decisions made during implementation**
> - **Consent baked into the writer.** `export_images(..., ExportConsent)` is a
>   no-op returning `{0,0}` unless `consent == Confirm`, so "declining writes
>   nothing" is enforced in the one place that can write — and is unit-testable
>   headlessly without driving the SDL modal.
> - **Wipe is unconditional.** `export_one_image` `crypto_wipe`s the decrypted
>   `SecureBytes` after the write *whether or not the write succeeded*; a test
>   asserts the buffer is all-zero post-write.
> - **Collision suffixing** appends ` (n)` before the extension
>   (`a.png` → `a (1).png`), never overwriting an existing file; the resolver is
>   pure (`unique_export_path`, filesystem probe injected).
> - **Selection is session/listing-scoped:** `GalleryGrid::refresh()` clears it,
>   so indices are only ever interpreted against the current leaf listing. Click
>   still opens (navigates); `Space` toggles selection on image tiles.

---

## Phase 11 — Slideshow ✅

**Goal:** Auto-advancing full-screen viewing of a leaf gallery, with a
user-configurable dwell time and a clear on/off toggle.

### Tasks
- [x] `src/ui/slideshow_model.{h,cpp}` — pure, SDL-free state machine (mirrors `viewer_model`/`scroll_model`): running/paused state, elapsed-time accumulation, advance + wrap (loop at gallery end), optional shuffle (visits each image once per cycle), and cross-fade progress `0..1`. Fully unit-tested headlessly.
- [x] **Configurable dwell** — the per-image display duration is user-adjustable live (`[`/`]` or `+`/`-` to shorten/lengthen), session-scoped (not persisted into the vault), with a sane default (4 s) and clamp range (1–30 s).
- [x] **On/off toggle** — a single key starts/stops the slideshow (`P` play/pause); `Space` (and a mouse click) also pauses; `Esc`/`Up` exits to the viewer. The running state is reflected on-screen (a play/pause + interval indicator).
- [x] **Frame prefetch** — reuse the viewer's existing bounded neighbour-decode window to pre-decode the next image so advances are seamless; decrypted bytes stay in the existing mlock'd `SecureBytes` window (no disk, wiped after GPU upload).
- [x] **Cross-fade** — simple alpha blend between outgoing and incoming frames driven by the model's transition progress.
- [x] `tests/ui/` — advance timing fires at the configured dwell; wrap/loop at the last image; shuffle visits each index exactly once per cycle; pause halts the timer; dwell clamps to the valid range; transition progress clamps to `[0,1]`.

### Acceptance criterion
Starting a slideshow in a leaf gallery auto-advances at the configured dwell
time, cross-fades between images, loops at the end, and the on/off toggle plus
live dwell adjustment work; exiting returns to the viewer at the current image.

**Status:** ✅ 202/202 tests pass under `scripts/test.sh` and `--asan` (9 new
slideshow-model tests). The slideshow is a third `ImageViewer` view mode
(`P` to start; `Fit`/`FillScroll`/`Slideshow`) so exiting (`Esc`/`Up`) returns
to the viewer at the current image. All timing/wrap/shuffle/cross-fade maths live
in the pure, SDL-free `src/ui/slideshow_model.{h,cpp}` (mirroring
`viewer_model`/`scroll_model`); the viewer owns the SDL plumbing — the model is
driven from `update(dt)`, frames decode through the existing bounded
`acquire_full` neighbour window (mlock'd `SecureBytes`, wiped after GPU upload —
no disk), and the cross-fade reuses the renderer's per-draw alpha (textures get
`SDL_BLENDMODE_BLEND`).

> **Notes / decisions made during implementation**
> - **Slideshow = a view mode, not a separate screen.** It reuses the viewer's
>   `acquire_full`/`evict_full_except` bounded texture cache and `update(dt)`
>   loop; entering snapshots nothing new, exiting just flips back to `Fit` at
>   `index()`. Keeps it consistent with the existing `Fit`/`FillScroll` toggle.
> - **Dwell + shuffle persist per-viewer (session-scoped), not in the vault.**
>   `[`/`]` (and `+`/`-`) adjust dwell by 1 s within a 1–30 s clamp; `S` toggles
>   shuffle (re-seeds the model, preserving run state). Both reset when the viewer
>   is re-opened from the gallery, matching the strip-side/view-mode convention.
> - **Cross-fade** draws the outgoing frame opaque then the incoming frame on top
>   at `alpha = fade_progress`, so the blend resolves to `in·p + out·(1−p)` over
>   any background. Fade always plays to completion even if paused mid-transition.
> - **Shuffle** keeps a permutation pinned so the first image shown is the one the
>   viewer was on; it reshuffles only at a true cycle boundary, guaranteeing every
>   image is visited exactly once per cycle.
> - **Structure (post-review).** To keep `ImageViewer` under SonarCloud's
>   field/method limits, the slideshow SDL plumbing was extracted into
>   `src/ui/slideshow_view.*` and the decode→GPU texture cache into a shared
>   `src/ui/full_tex_cache.*` (also now used by Fit/FillScroll). The slideshow's
>   `std::mt19937_64` only picks *display order* — never security material (all
>   crypto randomness stays on the CSPRNG) — so its weak-PRNG hotspots are
>   reviewed as Safe.

---

## Phase 12 — Tags & Search ✅

**Goal:** Per-node tags on **both images and galleries**, with gallery tags
cascading to descendants, and a scoped search across the whole vault.

### Tasks
- [x] **Index format extension** — add a tag list (`u16 count` + length-prefixed UTF-8 strings) to **both** gallery and image nodes; bump `INDEX_VERSION` (1 → 2). Deserialisation reads pre-tags vaults as having empty tag lists (back-compat). Enforce count/length bounds (`INDEX_MAX_TAGS = 4096`, per-tag length u16) so the Phase 7 fuzz suite stays crash-free.
- [x] **Cascade (read-time)** — a node's *effective tags* = its own tags ∪ the tags of all ancestor galleries, computed on the fly during traversal/search. Gallery tags are never copied onto descendants, so editing or removing a gallery tag stays consistent automatically.
- [x] `Vault` API — `set_tags(node_path, tags)` / `add_tag` / `remove_tag` for both node kinds, persisted via the existing crash-safe double-buffer index swap; `search(query, scope)` where `scope ∈ {Images, Galleries, Both}` walks the decrypted in-memory tree and matches `name` + effective tags (case-insensitive substring). No OCR, no disk access.
- [x] `src/ui/search_model.{h,cpp}` — pure query tokenisation + match/rank against name and effective tags; unit-tested.
- [x] **UI** — `/` opens a search overlay (`src/ui/search_overlay.*`) with a live-filtered result list and an **Images / Galleries / Both** scope toggle (`Tab`); a tag-editor widget (`src/ui/tag_editor.*`, add/remove tags via `G`) reachable from the viewer and from a gallery tile.
- [x] Update `CLAUDE.md` (index node now carries tags; `INDEX_VERSION` bump) and the relevant Serena `mem:core` memory.
- [x] `tests/` — tag round-trip across lock/reopen for images **and** galleries; a gallery tag is reported in a descendant image's effective tags; search scope correctly returns only images / only galleries / both; case-insensitive matching; a pre-tags vault opens with empty tags; the fuzz corpus is extended to tagged gallery + image nodes and stays crash-free.

### Acceptance criterion
Tags added to images and galleries survive a lock/reopen; a gallery's tags are
inherited by its descendant images; scoped search returns the correct set of
images or galleries across the whole tree; a pre-tags vault opens cleanly with no
tags; the extended fuzz suite passes.

**Status:** ✅ 252/252 tests pass under `scripts/test.sh` and `--asan` (50 new:
index tag round-trip/back-compat/bounds, 26 vault tag/cascade/scope/search,
23 `search_model` tokenize/match/rank). The index format bumped to
`INDEX_VERSION = 2` and reads v1 blobs back-compatibly with empty tag lists;
the fuzz corpus now seeds tagged gallery + image nodes. Tags + scoped search
live in the vault layer (`set_tags`/`add_tag`/`remove_tag`/`search`, cascade at
read time); the pure `ui::search_model` drives the overlay's live filter/rank.

> **Notes / decisions made during implementation**
> - **Uniform tag block.** Tags serialise the same way for both node kinds —
>   written immediately after the node `name`, before the gallery-children /
>   image-meta branch — so the reader parses them version-gated (`version >= 2`)
>   without branching on node type.
> - **Cascade is read-only.** Effective tags are computed during the search
>   DFS (own ∪ inherited, case-insensitively de-duplicated); the root gallery's
>   tags are global. Gallery tags are never copied onto descendants, so a tag
>   edit is instantly consistent everywhere.
> - **Two matchers, clean boundaries.** `Vault::search` does cascade + a small
>   local case-insensitive substring match (vault can't depend on `ui`); the
>   richer multi-token AND-match + ranking lives in the pure `ui::search_model`,
>   which the overlay applies on top of the in-scope candidate set
>   (`vault.search("", scope)`).
> - **Overlays, not screens.** The search overlay and tag editor are modal modes
>   inside `GalleryGrid` / `ImageViewer` (mirroring `consent_dialog` /
>   `export_ui`), so no new `NavKind`/`Screen` was needed. Keys: `/` search,
>   `G` tag editor, `Tab` cycles search scope, `Esc` closes.

---

## Phase 13 — Favorites ✅

**Goal:** Mark images or galleries as *favorite* and browse them through two
dedicated screens — an **Image Favorites** section and a **Gallery Favorites**
section.

### Tasks
- [x] **Index format extension** — a dedicated `favorite` `u8` flag on both gallery and image nodes (bump `INDEX_VERSION` again; pre-existing vaults read as not-favorited). A dedicated flag, *not* a reserved tag, keeps favorites out of the tag namespace and out of tag search.
- [x] `Vault` API — `toggle_favorite(node_path)`; `list_favorite_images()` (flat, whole-tree) and `list_favorite_galleries()`; persisted via the crash-safe index swap.
- [x] **Toggle UX** — a single key marks/unmarks the focused image or gallery (`B` for bookmark — `F`/`L`/`T` are already bound in the viewer); favorited tiles show a small gold star badge in the grid (and a gold bar in the list view).
- [x] **Two distinct screens** — `src/ui/favorites_images.{h,cpp}` (a flat grid of every favorited image across the vault, opens the viewer on activate) and `src/ui/favorites_galleries.{h,cpp}` (a list/grid of favorited galleries; activating one navigates to that gallery in the normal grid). Both reachable via keys from the gallery grid (`F` images, `Shift+F` galleries).
- [x] Update `CLAUDE.md` module layout + `mem:core`.
- [x] `tests/` — favorite flag round-trip for images and galleries; favoriting images populates the image-favorites list across the tree; favoriting a gallery populates the gallery-favorites list; un-favorite removes from both; a pre-favorites vault opens with none favorited.

### Acceptance criterion
Favoriting images and galleries populates the two distinct favorites screens;
the flags survive a lock/reopen; opening a favorite gallery navigates to it and
opening a favorite image opens the viewer.

**Status:** ✅ 273/273 tests pass under `scripts/test.sh` and `--asan` (8 new
vault favorites tests + 5 new index favorite round-trip/back-compat tests). The
index format bumped to `INDEX_VERSION = 3` and reads v1/v2 blobs back-compatibly
with `favorite = false`; the fuzz corpus now seeds favorited gallery + image
nodes. The favorite flag lives on `IndexNode` (gallery + image); the vault layer
adds `toggle_favorite` + flat whole-tree `list_favorite_images`/
`list_favorite_galleries` (persisted via the crash-safe double-buffer swap). The
two favorites screens are first-class `Screen`s (`NavKind::ToFavoriteImages`/
`ToFavoriteGalleries`); `B` toggles the focused tile in the grid and the current
image in the viewer.

> **Notes / decisions made during implementation**
> - **Favorite byte placement.** The `favorite u8` is written immediately after the
>   tag block, uniform for both node kinds, so the reader parses it version-gated
>   (`version >= 3`) without branching on node type — mirroring the Phase 12 tag block.
> - **Favorites lists reuse `SearchHit`.** `list_favorite_*` return `SearchHit`
>   (path + node + kind) for consistency with `search`, leaving `effective_tags`
>   empty (favorites don't compute the tag cascade).
> - **Toggle doesn't disturb selection.** In the grid, `B` flips the flag on the
>   same in-memory node the tile points at and repaints on the input event — no
>   `refresh()`, so the export multi-selection is preserved.
> - **Favorites images open a favorites-scoped viewer.** Activating a favorited
>   image opens the viewer over the *whole* favorites set (`NavKind::ToFavoriteViewer`),
>   so `<-`/`->`, the strip, and the slideshow iterate the favorites rather than one
>   gallery; `Esc`/`Up` returns to the favorites grid. The viewer gained a "collection
>   mode" — an explicit image set with a per-image full path and an exit target — so it
>   is no longer tied to a single `gallery_path_`. Favorited galleries navigate the
>   normal grid.

---

## Phase 14 — Multiple vaults ✅

**Goal:** Manage and open several vaults; move images between them.

### Tasks
- [x] **Recent-vaults registry** — `src/platform/vault_registry.{h,cpp}`: a config-dir list of known vault **paths only** (add/list/remove). It stores **no secrets** — no passwords, no keys, no keyfile contents.
- [x] `src/ui/vault_manager.{h,cpp}` — becomes the app's first screen: lists known vaults, plus create / open-other (file dialog) / remove-from-list. Selecting a vault transitions to the unlock screen for that path.
- [x] **Multiple vaults, single-active** — `App` owns one *active* unlocked `Vault` at a time (`unique_ptr`) driving the gallery, plus a `` ` `` quick-switch overlay + the manager to change vaults. **Deliberate deviation from the original "collection of unlocked vaults":** single-active / lock-on-switch (a second vault is unlocked only transiently during a transfer) keeps the in-memory key blast-radius to one — see the design spec §2.1. Idle auto-lock wipes the active key after 5 min.
- [x] **Move/copy between (and within) vaults** — `vault::transfer_image` / `transfer_gallery` with `TransferMode {Move, Copy}`: `read_image` into mlock'd `SecureBytes` → `add_image` into the destination → (Move only) `remove_image`/`remove_gallery` from the source; destination commits first (crash = recoverable duplicate). `&src == &dst` supported (same-vault), with a cycle guard for gallery moves. Plaintext exists only in the locked buffer (invariant #1).
- [x] Update `CLAUDE.md` (vault manager as first screen; new platform/ui modules) + `mem:core`.
- [x] `tests/` — registry add/list/remove + "no secrets persisted"; two vaults unlocked at once during a transfer; `transfer_image` checksum-matches in the destination and is gone from the source (verified across a reopen of both); both indices valid; `transfer_gallery` recursive round-trip; Copy keeps the source; same-vault move/copy + cycle rejection; `IdleTimer` unit tests.

### Acceptance criterion
✅ The manager lists multiple vaults; a vault can be unlocked, browsed, and
switched (manager or `` ` `` overlay); an image moved between vaults matches its
checksum in the destination and is gone from the source after reopen; copy
leaves the source intact; the registry never persists secrets; the active vault
auto-locks when idle.

### Delivered as 5 stacked PRs
1. Registry + manager + single-active App (#23)
2. Move images between vaults — `transfer_image` (#24)
3. Move whole galleries — `transfer_gallery` + `Vault::remove_gallery` (#25)
4. Copy mode + same-vault transfers — `TransferMode` (#26)
5. Idle auto-lock + `` ` `` quick-switch overlay

---

## Phase 15 — Video playback (frames + seek) ✅

**Goal:** Store video files in the vault and play their **video** track,
streaming decode directly from encrypted chunks with **no temp file**. Audio is
added in Phase 16.

### Tasks
- [x] **Vendor FFmpeg/libav** — decode-only static build (H.264/H.265 video; mov/mp4/m4v + matroska/webm demuxers; libswscale; encoders/muxers/protocols/network disabled). configure-built into `vendor/codecs-prefix` by `scripts/build_codecs.{sh,bat}`; `premake5.lua` `link_av()` links avformat/avcodec/swscale/avutil under `OSV_VENDORED_AV`. Needs **nasm** (like libaom).
- [x] **Encrypted-chunk streaming** — `src/media/chunk_avio.{h,cpp}` wraps `media::VideoSource` (decrypt-on-demand over `ChunkStore`, mlock'd one-chunk cache) in a read+seek `AVIOContext`. Seeks map a byte offset to the spanning chunk(s). **No bytes are ever written to a temp file** (fs-write assertion test).
- [x] **Index/format extension** — `IndexNode::Type::Video` + `VideoMeta` (container/codec/w/h/duration/orig_size/chunk list/poster), `INDEX_VERSION = 4` (v1–v3 read back-compat); `add_video` chunks the container + stores a first-frame JPEG poster; `read_thumbnail` returns the poster.
- [x] `src/media/video_decoder.{h,cpp}` — demux + H.264/H.265 decode → `DecodedFrame` (yuv420p/nv12 direct, swscale fallback); keyframe-anchored seek; `gfx::YuvTexture` streaming upload (`SDL_UpdateYUVTexture`/`UpdateNVTexture`).
- [x] **Viewer integration** — `src/ui/playback_model.{h,cpp}` (pure transport maths) + `src/ui/video_playback.{h,cpp}` (decoder + YUV texture + seek bar) hosted by `ImageViewer` when the current item `is_video()`: opens paused, `Space` play/pause, `J`/`L` ∓5 s, `,`/`.` frame-step, click/drag seek bar. Poster + play-badge on grid/list video tiles (`draw_tile_thumb`); list view shows duration + codec.
- [x] Update `CLAUDE.md` tech table (FFmpeg/libav, nasm) + `mem:tech_stack`/`mem:core`.
- [x] `tests/` — AVIO byte-exact read across chunk boundaries + SET/CUR/END/SIZE seek + **no-fs-write** assertion + auth-failure surfaced as `AVERROR(EIO)`; decoder frame-count/seek/swscale/malformed-reject; index v4 round-trip + v1–v3 back-compat; `add_video` reopen checksum + poster; transfer/search/favorites over video; `playback_model` transport maths; a headless `VideoPlayback` open→play→seek cycle that asserts **zero disk writes**.

### Acceptance criterion
✅ A short H.264 clip imported into the vault plays its video track, seeks
correctly, shows a poster thumbnail in the grid, and a test asserts that **no
decrypted bytes are ever written to disk** during playback.

### Delivered as 5 stacked PRs
1. Vendor FFmpeg (decode-only static build) (#28)
2. Video storage model — `Type::Video` + index v4 (#29)
3. Encrypted-chunk streaming — `VideoSource` + `chunk_avio` (#30)
4. Video decoder + YUV upload + poster/metadata (#31)
5. Viewer integration — `playback_model` + `video_playback` + grid/list wiring

**Status:** ✅ 379 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean). Video streams frame-by-frame from its encrypted chunks (custom AVIO over
`ChunkStore`, mlock'd decrypt cache) — no temp file, ever. `ImageViewer` hosts a
fit-only `VideoPlayback` (opens paused on frame 0; `Space`/`J`/`L`/`,`/`.` +
draggable seek bar); the grid/list show the poster with a play badge. Pure
transport maths live in the SDL-/FFmpeg-free `ui::playback_model`; the live
playback glue is verified headlessly with a no-disk-write assertion.

---

## Phase 16 — Audio & A/V sync ✅

**Goal:** Add the audio track to the video pipeline with proper
audio/video synchronisation.

### Tasks
- [x] **Audio decode** — extend `src/media/video_decoder` to decode the audio track (AAC/Opus/MP3/Vorbis/FLAC/AC3; FFmpeg build enables those decoders) into PCM frames.
- [x] **Audio output** — route decoded PCM to an `SDL_AudioStream`; SDL converts to device format as needed.
- [x] `src/media/av_sync.{h,cpp}` — pure, unit-tested sync logic: presentation timestamps tracked against the **audio clock**, with video frame drop/hold decisions returned as data (no SDL in the unit).
- [x] **Viewer controls** — volume + mute (`M`, `[`/`]`); the seek bar now seeks both tracks; pause stops audio cleanly.
- [x] **Memory hygiene** — decoded audio PCM is treated like decoded pixels (transient buffers, no disk); the streaming AVIO path is unchanged.
- [x] `tests/` — the audio decoder produces the expected sample count for a known clip; `av_sync` holds/drops frames correctly for fabricated PTS streams (drift ahead/behind); seek re-aligns both tracks; volume/mute math; headless no-disk-write assertion.

### Acceptance criterion
A short H.264+AAC clip plays with synchronised audio and video, seeks both
tracks correctly, and volume/mute/pause behave correctly — still with no
decrypted bytes written to disk.

**Status:** ✅ 390 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean). FFmpeg build enables six audio decoders (aac, opus, mp3, vorbis, flac,
ac3). `VideoDecoder` owns a shared demuxer feeding both video and audio via
per-stream packet queues; `AudioDecoder` decodes planar→interleaved F32; audio
samples flow into an `SDL_AudioStream` (which handles rate/format/channel
conversion — we do NOT use swresample for our resampling). Audio-clock
synchronisation via pure `av_sync::decide(audio_clock, frame_pts, ...)` drives
frame Present/Hold/Drop decisions. `VideoPlayback` hosts a paused-capable
`SDL_AudioStream` (master clock), mute/volume via `SDL_SetAudioStreamGain`,
controls `M`/`[`/`]`. Seek flushes both tracks and re-aligns. A headless
assertion verifies zero disk writes across open→play→seek→play.

---

## Phase 17 — Import ZIP archives ✅

**Goal:** Import a `.zip` of photos/videos into the vault in one action — either
as a **new gallery subtree** or **appended** to an existing gallery —
decompressing **into locked memory only**, never to a temp file (invariant #1
holds).

### Tasks
- [x] **Vendor miniz** — add `vendor/miniz` git submodule (single-file, public-domain/MIT); compiled directly by premake like monocypher/stb (no system zlib dependency). Update `setup.{sh,bat}` submodule init + `premake5.lua`.
- [x] `src/ui/zip_import.{h,cpp}` — walk the archive's central directory, build a planned gallery tree from entry paths (via the pure `src/ui/zip_plan.*`), and import each entry. Decompress **one entry at a time** into an mlock'd `SecureBytes`; dispatch by `image::detect_format` (image → `Vault::add_image`, else `Vault::add_video`, which validates the container); wiped on scope exit. **No entry is ever extracted to disk.** (Lives in `src/ui/` like `export.*`: it depends on vault + image, so placing it in `vault/` would invert the `image → vault` dependency.)
- [x] **Contents = all supported media** — import every entry whose format is a supported image (JPEG/PNG/GIF/BMP/TGA/HDR/WebP/HEIC/AVIF) or video (H.264/H.265 in mov/mp4/m4v/mkv/webm); skip everything else silently, reporting a skipped-count in a post-import summary.
- [x] **Folder mapping (mirror, with prompt)** — recreate the zip's directory hierarchy as nested galleries; media lands in leaf galleries (a flat zip → one gallery). If a single directory **mixes media files and subfolders** (would break the leaf invariant), a modal prompts the user to resolve it (flatten that directory's media into a sibling leaf, or skip it).
- [x] **Two destinations** — *Create new gallery* (preserves the mirrored subtree) or *Append to existing gallery*. **Append flattens:** it only adds the archive's media (ignoring subfolders) into the chosen leaf gallery. Filename collisions reuse `add_image`'s existing handling.
- [x] **UI** — a zip-import entry point from the gallery grid (file dialog over `SDL_ShowOpenFileDialog` with a `.zip` filter, reusing the `platform::file_dialog` async pattern), destination/mode choice, the mixed-folder resolution prompt, and a post-import summary (imported / skipped counts).
- [x] Update `CLAUDE.md` (miniz in the tech table, `src/ui/zip_plan.*` + `zip_import.*` in the module layout) + `mem:tech_stack`/`mem:core`.
- [x] `tests/` — a fixture `.zip` imports as a new gallery with the mirrored tree and matching per-file checksums; append-flatten adds only media into a leaf; unsupported entries are skipped + counted; a malformed/truncated zip is rejected without crashing (extend the fuzz mindset); a mixed-folder zip triggers the resolution path; a **no-fs-write assertion** proves nothing is extracted to disk during import.

**Out of scope (YAGNI):** password-protected/encrypted zips, zip *export*, other
archive formats (tar/7z).

### Acceptance criterion
Importing a fixture `.zip` as a new gallery reproduces its folder tree as nested
galleries with every supported file's checksum matching the original; append
mode adds only (flattened) media into the chosen leaf; unsupported entries are
skipped and reported; a test asserts **no decrypted or archive bytes are written
to disk** during import.

**Status:** ✅ 424 tests pass under `scripts/test.sh` and `--asan` (11 new: 5 zip-plan + 5 zip-import + 1 miniz linkage). miniz git submodule pinned to master commit `e78dfd2` (modern split-source build: `miniz.c`/`miniz_tdef.c`/`miniz_tinfl.c`/`miniz_zip.c`), built with `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` to avoid clashing with libz linked by avformat; compiled by premake like monocypher/stb. Shim header `vendor/miniz-shim/miniz_export.h` keeps the submodule pristine. Archive entries decompress one-at-a-time into mlock'd `SecureBytes` → dispatched to `Vault::add_image`/`add_video` by `image::detect_format` → wiped on scope exit; **zero bytes ever written to disk** (invariant #1 upheld). The executor + pure planner live in `src/ui/` (`zip_import.*`, `zip_plan.*`) like `export.*`, since they depend on vault + image. Folder tree mirrored as nested galleries; mixed-folder directories (media + subdirs) trigger a Flatten/Skip modal; Append mode flattens all media into the current leaf gallery; skipped entries (unsupported format, mixed-folder skip) counted and reported post-import.

---

## Phase 18 — Advanced search (dedicated screen) ✅

**Goal:** A dedicated search screen for galleries and media with **weighted
tags**, **include/exclude**, **AND/OR-grouped** clauses, **tag autocomplete**,
and **saved, reusable searches** — coexisting with the Phase 12 `/`
quick-overlay.

### Tasks
- [x] **Query model (grouped clauses)** — `src/ui/advanced_search_model.{h,cpp}`: a serialisable query of **include tags** (each with an optional **weight**, default 1, contributing to a relevance **score**), **exclude tags** (hard filter — any match removes the hit), **named groups** of tags each combined **AND**/**OR**, the groups joined by a top-level **AND**/**OR**, plus **gallery-name** substring matching and a **scope** (Images / Galleries / Both). Pure, SDL-free, evaluates a candidate's name + effective tags → `{matched, score}`; headlessly unit-tested (mirrors `search_model`).
- [x] **Tag autocomplete** — `Vault::all_tags()` returns the vault's distinct tag vocabulary (deduplicated case-insensitively across the whole tree); a pure `tag_suggestions(prefix, vocabulary)` helper (case-insensitive, ranked, unit-tested) drives a typeahead dropdown in every include/exclude/group field (`Tab`/`Enter` accept, arrows select).
- [x] **Saved searches in the vault (encrypted)** — extend index serialisation with a **vault-global saved-searches block** (name + serialised query) alongside the tree root; bump **`INDEX_VERSION` 4 → 5** (v1–v4 read back-compat with an empty list); persisted via the crash-safe double-buffer index swap. `Vault` API: `save_search` / `list_saved_searches` / `delete_saved_search` / `run_search(query, scope)`. Enforce count/length bounds so the Phase 7 fuzz suite stays crash-free.
- [x] **UI** — a first-class `Screen` (`NavKind::ToAdvancedSearch`, opened with `Shift+/` from the grid) hosting the clause/group builder, a live result list (reusing grid tile/list rendering), and a saved-searches sidebar (save current / load / delete). Activating a result opens the collection-mode viewer (like favorites) or navigates to a gallery. The Phase 12 `/` overlay is unchanged.
- [x] Update `CLAUDE.md` (new ui module, `Vault::all_tags`/saved-search API, `INDEX_VERSION = 5`) + `mem:core`, and extend the index-format reference for the saved-searches block.
- [x] `tests/` — query evaluation: weighted ranking, exclude filtering, group AND/OR + top-level join, name match, scope; tag-suggestion prefix matching/ranking; `all_tags` dedup across the tree; saved-search round-trip across lock/reopen; a pre-v5 vault opens with no saved searches; the fuzz corpus is extended with a saved-searches block and stays crash-free.

**Out of scope (YAGNI):** freeform query-language parser, regex, date/size-range
filters (candidates for a later phase).

### Acceptance criterion
The advanced-search screen filters and ranks images/galleries by a grouped,
weighted, include/exclude query with working tag autocomplete; searches can be
saved, listed, re-run, and deleted, surviving a lock/reopen; a pre-v5 vault opens
with no saved searches; the extended fuzz suite passes; the `/` quick-overlay
still works.

**Status:** ✅ 458 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean) — 23 new (13 `advanced_search_model` + 4 `index` v5 + 6 vault search; the
fuzz corpus also grew a v5 saved-searches block). The query model
(`src/ui/advanced_search_model.*`) is pure/SDL-
free/vault-free: `evaluate()` returns `{matched, score}` for weighted-include
(OR gate + scorers) ∧ exclude (hard filter) ∧ name-substring ∧ AND/OR groups
(top-level join); `serialize_query`/`deserialize_query` give an opaque saved-
search blob; `tag_suggestions()` ranks prefix-over-substring, deduped. The index
gains a vault-global saved-searches block (`SavedSearch{name, opaque query}`),
`INDEX_VERSION` 4→5 (v1–v4 read back-compat → empty list, bounds-checked against
the fuzz corpus). `Vault` gains `all_tags`, `run_search(AdvancedQuery)` (ranked
by score then path, scope from the query), and `save_search`/`list_saved_searches`/
`delete_saved_search` (upsert by name, persisted via the crash-safe index swap;
carried through `compact()`). `AdvancedSearchScreen` (`Shift+/`, `NavKind::
ToAdvancedSearch`) is a keyboard-driven builder + live results + saved sidebar
(`Ctrl+S` save, `Enter` load/open, `Del` delete); image results open the gallery
viewer, gallery results navigate. The Phase 12 `/` overlay is untouched.

---

## Phase 19 — Gallery cover thumbnails ✅

**Goal:** Replace the generic folder icon on gallery tiles with a representative
**cover** derived from the gallery's contents, so the grid is browsable at a
glance.

### Tasks
- [x] `src/ui/gallery_cover.{h,cpp}` — pure, SDL-free cover resolution:
  - [x] **Leaf gallery** → the **first image's** thumbnail span (a leaf video's poster span if the first child is a video).
  - [x] **Non-leaf gallery** → an ordered list of **up to 4 sub-gallery covers** (one per sub-gallery, in child order), each resolved **recursively** (a sub-gallery's single cover = its own first-image/poster, or its first sub-gallery's cover). Depth-bounded (reuse the index depth limit, `INDEX_MAX_DEPTH`) and cycle-free.
  - [x] Returns thumb chunk spans only — selects nothing requiring a full decode; an empty subtree yields no covers (→ folder-icon fallback).
- [x] `src/ui/cover_layout.{h,cpp}` — pure montage geometry: given a tile rect and 1–4 covers, return the sub-rects (single fill for 1; 2×2 arrangement for 2/3/4, graceful for 1–3). Unit-tested.
- [x] **Grid rendering** — `GalleryGrid` draws a gallery tile as either a single cover (leaf) or the montage (non-leaf), falling back to the existing folder icon when no cover resolves. Reuses the existing thumbnail texture cache via a new free friend `vault::read_thumb_span` (kept off `Vault` to respect the cpp:S1448 method cap; keyed by thumb offset; decrypt → off-thread decode → GPU upload — **no new disk path**). Cover resolution + the per-cover texture fetch are kept as free functions so `GalleryGrid` stays within the SonarCloud method budget (cpp:S1448).
- [x] `tests/ui/` — cover selection/order/recursion/fallback (leaf, folder-of-folders, mixed depths, empty, capped at 4, depth-limit); montage geometry for 0/1/2/3/4/clamped covers; `tests/vault/` round-trips `read_thumb_span` against `read_thumbnail`. Cover resolution touches no decode and no disk (it only walks the in-memory index).

### Acceptance criterion
A leaf gallery tile shows its first image (or first video's poster); a
folder-of-folders tile shows a 2×2 montage of up to four sub-gallery covers; an
empty gallery shows the folder icon. Browsing decrypts only the small thumbnail
blobs (no full-image decode) and writes nothing to disk.

---

## Phase 20 — Advanced-search list/grid result views ✅

**Goal:** Let the Phase 18 advanced-search screen toggle its result panel between
the existing **list view** and a **thumbnail grid view**.

### Tasks
- [x] **Result view mode** — extend `AdvancedSearchScreen` with a session-scoped result-view state (`List` | `Grid`), defaulting to `List` (current behaviour). Toggle on a non-text key (`Ctrl+L`) so it never collides with typing into the query/group fields.
- [x] **Grid rendering** — render results as thumbnail tiles reusing the gallery grid's `draw_tile_thumb` (including the Phase 19 covers for gallery results and the video play-badge); the list view is unchanged. Result activation (Enter → viewer over the containing gallery for media, navigate for galleries) behaves identically in both modes.
- [x] **No vault/format change** — purely a presentation toggle over the existing `run_search` result set.
- [x] `tests/ui/` — view-mode toggle state machine (default, cycle, persistence within the screen's lifetime); navigation maps to the correct index in both list and grid modes.

### Acceptance criterion
On the advanced-search screen, `Ctrl+L` switches the live results between a list
and a thumbnail grid; selecting a result opens/navigates the same target in both
modes; the Phase 12 `/` overlay and the rest of Phase 18 are unchanged.

**Status:** ✅ 488 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean) — 7 new `result_grid` tests (toggle state machine + List/Grid move deltas +
clamped/empty navigation). The result-view state machine is the pure, SDL-free
`src/ui/result_grid.{h,cpp}` (`ResultView{List,Grid}` + `toggle_result_view` +
`result_move_delta`/`result_move`); the screen stores a session-scoped
`result_view_` (default List) toggled by `Ctrl+L`, and `handle_results_key` drives
the cursor through `result_move` (List: ±1 row; Grid: ±1 / ±cols). The Phase 19
tile-thumbnail draw (gallery covers + montage + video play-badge) was extracted
verbatim into a shared `src/ui/tile_thumb.{h,cpp}` (`ThumbContext` bundle +
`draw_tile_thumb`/`tile_thumb_texture`/`tile_cover_tex`), now reused by both
`GalleryGrid` (delegating) and the new `render_result_grid` free friend. The
screen gained a `TextureCache&` ctor arg + its own off-thread decode worker +
`update()` pump (mirroring the gallery), so grid thumbnails decrypt → off-thread
decode → GPU upload through the existing pipeline — no vault/format change, no new
disk path. The Phase 12 `/` overlay and the rest of Phase 18 are untouched.

**Follow-up — session-preserved search + clear:** the advanced-search state
(query, builder buffers, cursor, focus, view mode) now persists across visits
within one unlocked-vault session via a session-scoped `ui::AdvancedSearchState`
(`src/ui/advanced_search_state.h`) that `App` owns and the screen restores in
`on_enter` / saves in `on_exit`; results are re-derived (not stored, so
`SearchHit::node` pointers can't dangle). `App` resets it whenever the active
vault changes (lock / switch / idle auto-lock, via `promote_pending`). `Ctrl+R`
clears the search behind a `Y/N` confirmation modal (resets the query to its
default and re-runs).

---

## Phase 21 — Import a tag list onto a gallery ✅

**Goal:** Bulk-add tags to a gallery from a plain-text file (one tag per line),
so large tag sets don't have to be typed one at a time in the tag editor.

### Tasks
- [x] `src/ui/tag_list_parse.{h,cpp}` — pure parser: raw file bytes → normalised tag list. Splits on LF/CRLF, trims surrounding whitespace, drops blank lines, de-duplicates **case-insensitively**, caps the count at `INDEX_MAX_TAGS` and each tag at the u16 length bound. Unit-tested.
- [x] **UI entry point** — `Shift+G` in the gallery grid (with a focused gallery tile) opens a `.txt` file dialog. Reuse the `platform::file_dialog` async pattern **with a `Purpose` tag** (Phase 17 fix) so its result can't be stolen by another in-flight dialog (e.g. image import / zip import).
- [x] **Apply** — read the file via `platform::read_file`, parse, then `Vault::add_tag` each tag onto the focused gallery node (merging with its existing tags), persisted via the crash-safe double-buffer index swap; refresh and show a post-import summary (added / skipped counts). The text file carries tag **metadata** (like filenames) — not key material and not decrypted vault content — so reading it is consistent with the security invariants.
- [x] `tests/` — parser edge cases (CRLF, blank lines, duplicates, surrounding whitespace, count/length bounds, non-UTF-8 bytes handled without crash); a fixture list applied to a gallery survives a lock/reopen and merges (not replaces) existing tags; the `file_dialog` `Purpose` isolation holds.

### Acceptance criterion
Selecting a gallery, choosing a `.txt` file of one-tag-per-line, imports the
de-duplicated tags onto that gallery; they merge with any existing tags, survive
a lock/reopen, and a summary reports the added/skipped counts.

**Status:** ✅ 501 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean) — 10 new `tag_list_parse` parser tests, 2 new `file_dialog` `Purpose`-isolation
tests, and a vault merge/reopen integration test. The parser is the pure, SDL/vault-free
`src/ui/tag_list_parse.{h,cpp}` (`parse_tag_list(span<const uint8_t>)` → splits on LF,
trims a trailing CR + surrounding whitespace, drops blanks, de-dupes case-insensitively
keeping first casing, truncates each tag to `TAG_MAX_BYTES` = 0xFFFF, caps at
`INDEX_MAX_TAGS`; bytes are opaque so non-UTF-8 input never crashes). `FileDialog` gained
`Purpose::TagList` + `open_tag_list()` (filtered to `.txt`). In `GalleryGrid`, `Shift+G`
on a focused **gallery** tile stashes the target path and opens the dialog; `update()`
drains the `TagList` result, reads the file (`platform::read_file`), parses, and applies
each tag via a free `apply_tag_list()` helper (counts added/skipped by the node's
tag-count delta — no UI-side case-folding), showing a `"Tag import: N added, M skipped"`
summary. Both the `Shift+G` entry and the result pump are inlined (no new `GalleryGrid`
methods) to stay under the S1448 method cap, mirroring the Phase 17 `Z` zip-import inline.

---

## Phase 22 — Tag overview screen ✅

**Goal:** A dedicated screen showing every distinct tag in the vault with how
many galleries and images carry it; activating a tag opens a galleries-only view
of the galleries with that tag.

### Tasks
- [x] `VaultSearch::tag_overview()` — walk the decrypted in-memory tree and return, per distinct tag (deduplicated case-insensitively, reusing the `all_tags` vocabulary), a `{tag, gallery_count, image_count}` tally. Counts use **direct tags on each node** (a gallery or image is counted only if it *directly* carries the tag — not the Phase 12 read-time cascade, which would inflate every descendant). No disk access. **Placed on the `VaultSearch` facade rather than `Vault` itself** (the roadmap's original `Vault::tag_overview()`) to keep `Vault` under the cpp:S1448 method cap — same rationale and home as `all_tags`/`run_search`, whose vocabulary it reuses.
- [x] `src/ui/tag_overview_model.{h,cpp}` — pure sort/filter of the overview list (by name or by count; optional typed prefix filter). Unit-tested.
- [x] **UI** — a first-class `Screen` (`NavKind::ToTagOverview`, opened with `Shift+T` from the gallery grid): a scrollable list of `tag — N galleries, M images`, keyboard-navigable, with `Enter` opening a **galleries-only** view of the galleries directly carrying that tag. That view reuses the `favorites_galleries` pattern (a flat list/grid whose activation navigates to the chosen gallery in the normal grid); `Esc` returns to the overview, then to the grid.
- [x] Update `CLAUDE.md` (new ui modules + `VaultSearch::tag_overview`) + `mem:core`.
- [x] `tests/` — `tag_overview` direct-tag counts are correct across a nested tree (a gallery tag does **not** inflate descendant image counts); sort/filter ordering; `Enter` yields exactly the galleries directly carrying the tag; an empty/untagged vault produces an empty overview; counts are stable across a lock/reopen.

### Acceptance criterion
The tag overview lists every distinct tag with its direct gallery and image
counts; selecting a tag opens a view of exactly the galleries carrying it, and
activating one navigates to that gallery; the screen is stable across a
lock/reopen.

**Status:** ✅ 521 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean) — 8 new `tag_overview_model` sort/filter tests + 5 new vault tests
(direct-tag counts with no cascade, galleries-with-tag direct-only, untagged →
empty, locked → empty, stable across lock/reopen). The counting + galleries-only
lookup live on the **`VaultSearch` facade** (`tag_overview()` → `std::vector<ui::TagTally>`
via direct-tag walk, reusing `collect_tags`' vocabulary; `galleries_with_tag()` →
galleries directly carrying a tag) so `Vault` stays under its method cap. The pure
`src/ui/tag_overview_model.{h,cpp}` owns the sort (name / count-desc) + case-insensitive
prefix filter. `TagOverviewScreen` (`Shift+T`, `NavKind::ToTagOverview`) renders the
keyboard-navigable list (Up/Down, Enter, Tab = toggle sort, type = filter); `Enter`
opens `TagGalleries` (`NavKind::ToTagGalleries`, the tag carried in `Nav::path`),
a thin `FavoritesScreen` subclass whose `go_back()` returns to the overview. `image_count`
counts non-gallery media (images + videos) so a video-only tag isn't a phantom 0/0 row.

---

## Phase 23 — UI colour schemes ✅

**Goal:** Offer several selectable UI colour themes, switchable at runtime and
remembered across launches in global config (no secrets).

### Tasks
- [x] **Runtime theme** — refactor `src/gfx/theme.h` from compile-time constants into a runtime `Theme` value (a struct of the existing colour tokens) plus a table of **built-in presets**: Refined Slate (current, default), a light theme, a high-contrast theme, and one more (e.g. sepia/midnight). A `gfx::active_theme()` accessor + `gfx::set_theme(id)` back the active selection; every `theme::…` call site reads the active theme (broad but mechanical — the token set is defined once and each preset fills it).
- [x] `src/platform/theme_pref.{h,cpp}` — persist the chosen theme id in the config dir (atomic write, **stores no secrets**, mirroring `vault_registry`); loaded at startup, saved on change; an unknown/absent id falls back to the default.
- [x] **UI** — a theme picker reachable from the **vault manager** (proposed key `C`) listing the presets with live apply-on-select; the choice persists immediately.
- [x] Update `CLAUDE.md` (runtime theme + `platform/theme_pref.*`) + `mem:core`.
- [x] `tests/` — every preset defines every colour token (no missing/zeroed tokens); `theme_pref` round-trip (save id → reload id; unknown id → default); a pure "next/select theme" helper if one is added. The broad refactor is additionally validated by the existing test suite staying green and the app building on all platforms.

### Acceptance criterion
The user can pick from several built-in colour themes; the choice applies
immediately, persists across restarts via global config (no secrets stored), and
all existing screens render correctly under every preset.

**Status:** ✅ 539/539 tests pass (`scripts/test.sh`); `scripts/test.sh --asan` clean.
`gfx::theme.h` is now a runtime `Theme` value with four built-in presets (Refined
Slate / Light / High Contrast / Midnight); `gfx::set_theme()` swaps the active one
and the existing `gfx::theme::X` tokens are references into it, so every call site
picks up a switch with no change. `platform::ThemePref` persists the chosen theme's
stable slug to `config_dir()/theme.conf` (atomic temp+rename, no secrets); it is
loaded in `App::init()` and saved live as the picker selection moves. The `ui::ThemePicker`
overlay (`C` in the vault manager, QuickSwitch-style) previews each preset on Up/Down —
the preview *is* the choice, persisted immediately; Enter/Esc just close.

---

## Phase 24 — Import CBZ archives ✅

**Goal:** Import a `.cbz` comic archive as a single gallery of pages, reusing the
Phase 17 miniz/ZIP pipeline — decompressing **into locked memory only**, never to
a temp file (invariant #1 holds).

### Tasks
- [x] **Natural-order sort** — a pure filename comparator (`src/ui/natural_sort.{h,cpp}`) so pages order `1 < 2 < 10` rather than lexicographically. Unit-tested.
- [x] **CBZ plan** — a fixed plan distinct from the Phase 17 mirrored-tree plan: **one leaf gallery** named after the archive (sans `.cbz`), containing every supported **image** entry (videos/other formats skipped + counted), **flattening** any internal subfolders, ordered by the natural-order sort over the full entry path.
- [x] **Executor reuse** — reuse the Phase 17 miniz central-directory reader and the per-entry **decompress-one-at-a-time into mlock'd `SecureBytes` → `Vault::add_image` → wipe on scope exit** path. **No entry is ever extracted to disk.**
- [x] **UI** — extend the existing `Z` zip-import file dialog filter to also accept `.cbz`; a picked `.cbz` routes to the CBZ plan (the `.zip` path is unchanged), with the same post-import summary (imported / skipped counts).
- [x] Update `CLAUDE.md` (CBZ handled by the miniz/zip path; new natural-sort unit) + `mem:core`.
- [x] `tests/` — a fixture `.cbz` imports as one gallery named after the file with pages in natural order and matching per-page checksums; non-image entries are skipped + counted; internal subfolders are flattened; a malformed/truncated `.cbz` is rejected without crashing; a **no-fs-write assertion** proves nothing is extracted to disk during import.

**Out of scope (YAGNI):** CBR/CB7/CBT (RAR/7z/tar variants), nested archives, per-page reordering UI.

### Acceptance criterion
Importing a fixture `.cbz` produces a single gallery named after the file whose
pages are in natural reading order with checksums matching the originals;
unsupported entries are skipped and reported; a test asserts **no decrypted or
archive bytes are written to disk** during import.

**Status:** ✅ 553/553 tests pass (`scripts/test.sh`); `scripts/test.sh --asan` clean.
New pure `ui::natural_sort` (digit runs by value, letters case-insensitive) orders
pages; `ui::build_cbz_plan` emits one leaf gallery (named after the archive) of every
image entry, videos/other skipped+counted, subfolders flattened with basename
collisions disambiguated by source dir, in natural reading order. `ui::import_cbz`
reuses the Phase 17 miniz → mlock'd `SecureBytes` → `add_image` path verbatim — no
disk extraction (asserted by a directory-count test). The `Z` file dialog filter now
accepts `zip;cbz`; `GalleryGrid` routes a picked `.cbz` to a fixed one-leaf import
(prompt prefilled with the archive stem) while the `.zip` mirror/append flow is
unchanged.

---

## Phase 25 — Bugfixes & housekeeping ✅

**Goal:** Fix layout-dependent keybindings, give every file operation the same
background-progress UX the Phase 24 import got, and tidy the repo (drop committed
planning docs, flag the project as AI-driven).

### Tasks
- [x] **Layout-independent keybindings** — the in-viewer video **volume** keys `[` / `]` don't fire on non-US layouts (e.g. German QWERTZ, where those glyphs live behind AltGr), so volume can't be changed at all. Rebind volume to **layout-independent** keys — SDL **scancodes** (physical key position) or non-punctuation keys — and audit the other punctuation shortcuts (`/` search, `?` advanced search, `` ` `` quick-switch, `[` / `]`) for the same defect. (Letter/digit and named keys like `M` mute, arrows, Enter, Esc are unaffected.)
- [x] **Background file operations with progress** — move/copy (transfer within/between vaults), delete (a gallery subtree or a single item), and export currently block the UI and only surface a final one-line message. Run each on a worker thread with a live **“N / M items” progress modal + cancel**, reusing the Phase 24 `ZipImportJob` / `ImportProgress` pattern — preserving the single-thread vault-handle invariant and suppressing the idle auto-lock during the op (`Screen::blocks_idle_lock()`). Export keeps its consent modal; its background write is still the one gated plaintext-to-disk deviation.
- [x] **Remove committed docs dir** — delete the only committed doc (`docs/superpowers/plans/2026-06-12-phase8-cross-platform.md`) and add `docs/` to `.gitignore` so AI planning artifacts stay out of the tree.
- [x] **README note** — add a note at the very top of `README.md` stating this is an **AI-driven project, vibe-coded for educational purposes**.
- [x] Update `CLAUDE.md` / `mem:*` if the keybindings or the transfer/delete/export flow change.
- [x] `tests/` — unit-test the layout-independent key mapping (scancode → action, independent of layout); test the background-op progress/cancel reporting the way `ZipImportJob` is tested.

**Out of scope (YAGNI):** fully user-remappable keybindings; UI text localisation; reworking the export consent/scope model (threading only).

### Acceptance criterion
Volume can be changed on a non-US (German) keyboard layout; a large gallery
move/copy/delete/export shows live progress without freezing the UI and can be
cancelled; `docs/` is gone from the tree and gitignored; the README carries the
AI-driven note.

**Status:** ✅ 581/581 tests pass (`scripts/test.sh`); `scripts/test.sh --asan` clean.
Slideshow dwell binds `[`/`]` by **scancode** (`ui::bracket_key_for_scancode`, the
keys right of `P`) plus `-`/`+`. Video **volume** uses `ui::volume_dir`, which
accepts the `[`/`]` produced character (resolved via `SDL_GetModState` so German
QWERTZ **AltGr+8/9 works** — the initial scancode-only fix missed this, as AltGr+8
reports the physical `8` scancode), the `-`/`+`/`=` glyph keys (the intuitive pair,
now advertised in the HUD as `[-/+] Vol`), and the physical bracket scancodes — all
unit-tested. The
`/`, `?`, `` ` `` character shortcuts were already layout-robust via
`SDL_GetKeyFromScancode`; that logic is now centralised as `is_search_key` /
`is_advanced_search_key` / `is_quick_switch_key` in the same header. Export, delete
and move/copy now run on a background worker via a reusable `ui::FileOpJob`
(mirrors `ZipImportJob`): a shared `vault::OpProgress` (which `ui::ImportProgress`
now aliases) drives an "N / M" progress modal (`ui::draw_op_progress`) + Esc-cancel,
gated so the UI never touches the vault while a job runs, with the idle auto-lock
held off (`blocks_idle_lock()`). `vault::transfer_images` (new bulk driver),
`transfer_gallery`, and `export_images` take an optional `OpProgress*`; a cancelled
gallery Move leaves the source intact (recoverable duplicate, never a loss). The
`docs/` plan doc is removed and gitignored; the README carries the AI-driven note.

---

## Phase 26 — Transparent vault compression (adaptive, store-if-smaller) ✅

**Goal:** Compress each chunk's plaintext **before encryption**, adaptively —
keep the compressed form only when it is meaningfully smaller, otherwise store
raw — so compressible payloads (BMP/TGA/HDR images, index blobs) shrink the
vault while already-compressed media (JPEG/PNG/WebP/HEIC/AVIF, video) stores raw
with **zero size regression**. Fully transparent to every caller; invariant #1
(no plaintext to disk) holds throughout.

> **Evaluated 2026-07-03:** the vault does not compress today —
> `ChunkStore::append_chunk` encrypts the raw plaintext verbatim; the only
> compression code in the tree is miniz *decompression* for ZIP/CBZ import.

### Tasks
- [x] **Chunk plaintext framing** — prefix the chunk *plaintext* (inside the AEAD, so the method flag is authenticated) with `method u8` (0 = raw, 1 = deflate) and, for compressed chunks, an `orig_len` field. Bound `orig_len` against the max chunk plaintext size **before allocating** (decompression-bomb guard — the Phase 7 OOM lesson).
- [x] **Vault flag / back-compat** — a bit in the header's reserved `flags` u32 marks "chunks are method-prefixed". Existing vaults keep the bit clear and are read **and appended** in legacy raw mode forever (no migration, no mixed chunk interpretation within one vault). New vaults set the bit.
- [x] **Compressor** — miniz `tdefl`/`tinfl` (already vendored + premake-compiled with `MINIZ_NO_ZLIB_COMPATIBLE_NAMES`); **no new dependency**. Compress in `append_chunk`; keep the compressed form only when ≤ ~95% of the original, else store raw.
- [x] **Decompress path** — decrypt into an mlock'd `SecureBytes` → inflate into a second mlock'd buffer sized by the validated `orig_len` → wipe the intermediate. No disk, ever.
- [x] **Index blobs framing** — sealed via `crypto::seal` + `append_raw`, then framed explicitly at `commit_index`/`unlock`/`compact` sites; the method byte is inside the sealed blob, so compressibility is authenticated. Gallery names/tags on large vaults benefit from compression without extra code.
- [x] **Document the side channel** — ciphertext length now reveals plaintext *compressibility*; record it as accepted for the local-attacker threat model (CLAUDE.md hardening notes).
- [x] Update `CLAUDE.md` (chunk layout, deferred-decisions table) + the container-format reference section below + `mem:core`.
- [x] `tests/` — round-trips (compressed + raw) with checksums; a BMP/TGA vault is measurably smaller while a JPEG vault stores raw (no growth); a legacy (flag-clear) vault fixture opens, reads, and appends unchanged; hostile `method`/`orig_len` fuzz corpus; export/transfer/compact still checksum-match on compressed vaults; ASAN clean.

**Out of scope (YAGNI):** migrating/recompressing legacy vaults (even via compaction); zstd/LZ4; per-file or user-facing compression settings; compressing the plaintext header.

### Acceptance criterion
A vault of BMP/TGA fixtures is measurably smaller than the summed plaintext; a
vault of JPEGs is no larger than today (raw fallback); a legacy vault opens,
reads, and appends unchanged; export/transfer checksums match on compressed
vaults; the extended fuzz suite and ASAN pass.

**Status:** ✅ 660/660 tests pass; `scripts/test.sh --asan` clean. Pure adaptive store-if-smaller deflate framing (miniz `tdefl`/`tinfl`) wraps chunk plaintext before encryption; method byte + bounded orig_len live inside the AEAD for authentication. New vaults set the framing flag; legacy vaults read and append raw forever. Index blobs are framed at three sites (`commit_index`, `unlock`, `compact`), and the compact-index-blob write path revealed a real bug (now fixed). Ciphertext length reveals plaintext compressibility — accepted for the local-attacker threat model.

---

## Phase 27 — `meta.json` metadata on archive import 🔜

**Goal:** When a zip/cbz archive contains a top-level `meta.json`, use it to **title**
and **tag** the imported gallery instead of falling back to the filename.

Expected shape (all fields optional; unknown keys ignored):

```json
{
  "title": { "english": "English Title", "japanese": "日本語タイトル" },
  "tags":  [ { "type": "tag", "name": "awesome tag" },
             { "type": "artist", "name": "someone" } ]
}
```

### Tasks
- [x] **Parser** — a pure, SDL/vault-free `meta.json` parser (`src/ui/meta_json.{h,cpp}`) → `{ title_english, title_japanese, tags[] }`. Needs a JSON reader (the project has none today): **vendor a single-header MIT JSON lib** (e.g. `nlohmann/json`) rather than hand-rolling UTF-8/escape handling — decision confirmed: **nlohmann/json v3.12.0** vendored as `vendor/json`, used exception-free (`parse(..., allow_exceptions=false)`). Tolerant of missing/partial fields; ignores unknown keys.
- [x] **Mapping** (agreed decisions):
  - Gallery **name** = `title.english`, falling back to `title.japanese`, then the archive filename.
  - `title.japanese`, when present, is added as a **tag** (so it stays searchable).
  - Each `tags[]` entry becomes a **type-prefixed** tag `"<type>:<name>"` (e.g. `artist:someone`, `tag:awesome tag`), applied through the existing `Vault::add_tag` merge (case-insensitive de-dupe).
- [x] **Wire into import** — `build_zip_plan` / `build_cbz_plan` + `import_zip` / `import_cbz` detect a top-level `meta.json`; if present it overrides the default gallery name (the top gallery for zip, the single leaf for cbz) and seeds that gallery's tags. `meta.json` itself is **not** imported as a page. No `meta.json` → today's behaviour is byte-for-byte unchanged.
- [x] Update `CLAUDE.md` / `mem:core`.
- [x] `tests/` — a fixture archive with `meta.json` imports under the english title, tagged with the japanese title and each `type:name`; a missing / partial / malformed `meta.json` degrades gracefully to a filename-named, untagged import.

**Out of scope (YAGNI):** PDF metadata (PDFs carry no archive `meta.json` — a later phase could read the PDF info dict); writing `meta.json` back out; nested/per-folder `meta.json`; acting on fields beyond title + tags (the parser ignores extras without error).

### Acceptance criterion
Importing a fixture zip/cbz containing `meta.json` yields a gallery named after the
english title, tagged with the japanese title and each `type:name` tag; a malformed
`meta.json` never blocks the import.

**Status:** ✅ 677/677 tests pass; `scripts/test.sh --asan` clean. `ui::parse_meta_json` (nlohmann/json v3.12.0, vendored `vendor/json`, exception-free parse) + `meta_gallery_name`/`meta_gallery_tags` mapping; `find_meta_entry` excludes the top-level `meta.json` from all three planner paths (never placed, never counted skipped); `import_zip` (NewGallery) and `import_cbz` seed the created gallery's tags via `Vault::add_tag`; Append only excludes the file. Extraction goes to mlock'd memory with a 1 MiB sanity cap; a malformed `meta.json` degrades to the filename-named, untagged import.

**Follow-up (owner feedback):** the meta title no longer silently overrides the
name typed in the popup. Instead `ui::peek_archive_meta` reads the archive's
`meta.json` when the file is picked and the gallery-name popup is **prefilled**
with `meta_gallery_name(meta, filename-stem)` — the text the user confirms is
authoritative for the import (zip NewGallery and cbz alike). Tag seeding is
unchanged.

**Follow-up 2 (owner feedback):**
- The generic tag type `tag`/`tags` (case-insensitive) no longer gets a prefix —
  `{"type":"tag","name":"ponytail"}` imports as `ponytail`, not `tag:ponytail`.
  Real types (`artist:`, `character:`, `parody:`, …) keep their prefix.
- The tag editor now shows the ancestor-gallery tag cascade: a read-only
  "Inherited from gallery" section (pure `ui::inherited_tags`, new
  `src/ui/tag_inherit.{h,cpp}`) below the own-tags list, so meta.json tags on a
  gallery are visible when a page's editor is opened. Del/selection only ever
  touch the node's own tags.

---

## Phase 28 — Broaden `.mov` / video codec support 🔜

**Goal:** Decode the codecs commonly found in `.mov` containers beyond H.264/H.265.
The `.mov` container, `mov` demuxer, and `ftyp` detection already work — the gap is
files whose video stream uses a codec the vendored FFmpeg doesn't currently decode.

### Tasks
- [x] **Add FFmpeg decoders** — extend the `--enable-decoder` set in `scripts/build_codecs.sh` with the codecs common in `.mov`: **ProRes** (`prores`), **DNxHD/DNxHR** (`dnxhd`), **MJPEG** (`mjpeg`) + the `dnxhd`/`mjpeg` parsers (FFmpeg has no prores parser; the mov demuxer frames its packets). The build stays **decode-only** (no encoders/muxers). `build_codecs.bat` is untouched — Windows never builds FFmpeg (video is `OSV_VENDORED_AV`-gated to Linux/macOS).
- [x] Confirmed no format-detection change is needed — `vault::detect_video_container` maps any `ftyp` box to the ISO-BMFF/MP4 path (the probe tests assert `container == MP4` for the `.mov` fixtures), and `.mov` was already in the import dialog filter.
- [x] Size impact measured: `libavcodec.a` 19.1 → 20.2 MiB (+1.0 MiB); linked Release `osv` 16.44 → 16.62 MiB (+172 KiB, ~1%).
- [x] Update `CLAUDE.md` (FFmpeg decoder list) + the README stack line (+ `docs/VENDORED_DEPS.md`, `mem:tech_stack`).
- [x] `tests/` — small `.mov` fixtures (`tiny_prores.mov`, `tiny_dnxhr.mov`, `tiny_mjpeg.mov` — 160/256×120, 10 frames, generated with ffmpeg `testsrc`) probe with poster + full decode through the encrypted-chunk path; gated behind `OSV_VENDORED_AV`.

**Out of scope (YAGNI):** encoding/transcoding; professional codecs beyond the above (CineForm, DNxHR HQX variants) unless a real fixture proves the need; new audio codecs.

### Acceptance criterion
A ProRes (and an MJPEG) `.mov` imports, probes, shows a poster, and plays; existing
H.264/H.265 playback and A/V sync are unchanged.

**Status:** ✅ 685/685 tests pass; `scripts/test.sh --asan` clean. `VideoCodec` gains `ProRes`/`DNxHD`/`MJPEG` (raw-u8 index field — no format bump; older builds show new values as plain "Video"), `VideoDecoder::open` maps the three FFmpeg codec ids instead of rejecting them, and `video_codec_name` labels them in the viewer metadata. Their native pixel formats (yuv422p10le / yuv422p / yuvj422p) flow through the existing swscale → I420 conversion, so playback and A/V sync are untouched.

---

## Phase 29 — Tag autosuggest in the tag editor 🔜

**Goal:** While typing a tag in the `G` tag editor (galleries and images alike),
suggest tags that already exist anywhere in the vault. A suggestion can be
selected with the arrow keys — or ignored by simply continuing to type; the
typed text always wins unless a suggestion is explicitly highlighted.

### Tasks
- [x] **Pure model** — new `src/ui/tag_suggest.{h,cpp}` (test-target unit like `tag_inherit`): `editor_tag_suggestions(buffer, vocabulary, own_tags)` → trims the buffer, delegates ranking to the Phase 18 `ui::tag_suggestions` (prefix matches first, then substring, ci de-dupe), filters out tags the node already carries (`tag_ci_equal` — suggesting them would be a no-op merge), caps at 5. Cursor movement reuses the tested `ui::move_tag_cursor` (−1 = editing the buffer).
- [x] **Vocabulary** — `VaultSearch::all_tags()` fetched in `TagEditor::open()` and re-fetched after each successful add/remove, so a just-created tag is immediately suggestible.
- [x] **TagEditor wiring** — buffer empty ⇒ behaviour unchanged (Up/Down scroll own tags, Del removes). Buffer non-empty ⇒ a dropdown of ≤5 suggestions under the input box; Up/Down move the suggestion highlight; **Enter adds the typed text** unless a suggestion is highlighted (then it adds the suggestion); **Esc deselects** a highlighted suggestion instead of closing (nothing highlighted ⇒ closes as today); typing/backspace recomputes suggestions and clears the highlight. The dropdown draws *over* the tags-list area (combobox style, drawn last); the fixed modal does not resize.
- [x] Update `CLAUDE.md` (tag_editor + new module entry) + `mem:core`.
- [x] `tests/` — unit tests for `editor_tag_suggestions`: ranking passthrough, own-tag exclusion (ci), empty buffer → empty, cap at 5, first-casing de-dupe.

**Out of scope (YAGNI):** changing the advanced-search screen's existing autocomplete; fuzzy matching beyond the Phase 18 prefix/substring ranking; suggestion counts/frequency ranking; a shared combobox widget.

### Acceptance criterion
Typing in the tag editor of a gallery or image shows up to 5 existing vault tags
matching the typed text; Down+Enter adds the highlighted suggestion, plain Enter
adds exactly what was typed, and tags already on the node are never suggested.

**Status:** ✅ 691/691 tests pass; `scripts/test.sh --asan` clean. Pure `ui::editor_tag_suggestions` (tag_suggest.{h,cpp}) over the Phase 18 ranking + `move_tag_cursor`; TagEditor overlays a ≤5-row dropdown while typing — Enter adds the typed text unless a suggestion is highlighted, Esc deselects before it closes.

---

## Phase 30 — Import PDF as a gallery of pages 🔜

**Goal:** Import a `.pdf` as a gallery of page images (like CBZ), rendering each
page **into locked memory only** — never a temp file (invariant #1 holds).

### Tasks
- [ ] **Vendor PDFium** — add **PDFium** (BSD-3-Clause, permissive — fits the vendored-static model) as a git submodule built into `vendor/codecs-prefix/` by `scripts/build_codecs.{sh,bat}` (render-only). Gate the dependent code behind a build define (`OSV_VENDORED_PDFIUM`), mirroring `OSV_VENDORED_AV`, so a non-PDF build still links.
- [ ] **Render pipeline** — read the picked file into an mlock'd buffer, load the document from memory, render each page to an RGBA bitmap at a sensible target resolution, and feed the bitmap into the existing `add_image` / thumbnail path. **No page bytes touch disk.**
- [ ] **Plan/executor** — a PDF import plan mirroring `build_cbz_plan`: one leaf gallery named after the file, one image per page in page order, reusing the import-summary UX and the Phase 25 background-progress modal.
- [ ] **UI** — the file dialog accepts `.pdf` (its own `Purpose`), routed to the PDF importer (distinct from the `Z` zip/cbz path).
- [ ] Update `CLAUDE.md` / README (new vendored lib + any build prerequisite) + `mem:tech_stack`.
- [ ] `tests/` — a small fixture `.pdf` imports as a gallery with the correct page count and a **no-fs-write** assertion; a malformed / password-protected PDF is rejected without crashing.

**Out of scope (YAGNI):** extracting embedded images verbatim; text/searchable-PDF handling; per-page DPI/quality UI; PDF export; non-PDF “more formats” (revisit per-format as needed).

### Acceptance criterion
A fixture `.pdf` imports as one gallery of page images in page order, asserted to
write nothing to disk; a corrupt or encrypted PDF fails gracefully with a message.

**Status:** 🔜 Planned.

---

## Phase 31 — Fullscreen viewing + edge-click navigation ✅

**Goal:** Let the image viewer (and its in-viewer video playback) expand to a
borderless fullscreen window, with normal navigation unaffected, plus
left/right-edge click navigation between images.

### Tasks
- [x] `src/gfx/window.{h,cpp}` — `Window::set_fullscreen(bool)` /
  `is_fullscreen()`: borderless-maximized toggle that saves/restores the
  windowed position and size (`SDL_GetDisplayUsableBounds` of the window's
  current display; a lookup failure logs and leaves the window unchanged).
- [x] `src/ui/viewer_model.h` — pure `edge_nav_hit(x, vp_x, vp_w) -> int`
  (`EDGE_NAV_FRAC = 0.12f`), unit-tested alongside the existing zoom/pan/strip
  helpers in this file.
- [x] `src/ui/image_viewer.cpp` wiring:
  - `F11` and a double left-click both toggle fullscreen, for images and
    in-viewer video alike (the double-click check runs after the thumbnail-
    strip hit test, so double-clicking a thumbnail still just selects it).
  - `Esc` exits fullscreen on the first press (stays in the viewer); a second
    `Esc` then returns to the gallery as before.
  - Clicking the left/right 12% edge of a non-zoomed image in Fit mode steps
    to the previous/next image (images only — video keeps its seek-bar/
    play-pause click targets; FillScroll already has its own scroll-based
    navigation).

**Out of scope (YAGNI):** the standalone slideshow view (already presents
full-screen-ish by design); true exclusive `SDL_SetWindowFullscreen`
display-mode switching; edge-click navigation for video or FillScroll mode.

### Acceptance criterion
From the image viewer, `F11` or a double-click expands the window to a
borderless fullscreen covering the display and back again, for both images
and in-viewer video; `Esc` exits fullscreen on the first press and returns to
the gallery on the second; arrow-key/thumbnail-strip navigation and video
controls are unaffected. Clicking the left/right 12% edge of a non-zoomed
image in Fit mode steps to the previous/next image, windowed or fullscreen.

**Status:** ✅ All tests pass (`scripts/test.sh`); `scripts/test.sh --asan`
clean. `gfx::Window`'s fullscreen SDL calls are thin wrappers verified by
manual smoke test (not unit-tested — matches the existing
`test_window_visibility.cpp` precedent of only testing pure helpers under
headless CI).

---

## Phase 32 — Background multi-file import ✅

**Goal:** The multi-select file-picker import (already working at the
dialog level since before this phase) no longer blocks the UI thread: it
runs on a background `FileOpJob`, with a progress bar, cooperative
Esc-cancel, and an aggregated success/failure summary — matching every
other bulk vault operation in this codebase.

### Tasks
- [x] `src/ui/file_op_job.{h,cpp}` — `FileOpKind::Import` +
  `FileOpJob::start_import(vault, base_gallery, files)`: reads and imports
  each picked file on the worker thread (dispatching to `add_video`/
  `add_image` by extension), tallying successes/failures. Per-file failures
  never turn into a hard `oc.error` (mirrors `start_export`'s convention);
  the finished-import status names up to 3 failed files plus a "+N more"
  suffix beyond that (the footer is one unwrapped text line).
- [x] `src/ui/gallery_grid.{h,cpp}` — `do_import`'s synchronous per-file loop
  is gone; `pump_import()` launches `naming_.file_op.start_import(...)`
  instead. Every other piece of the job machinery (progress veil, Esc→
  cancel, vault-busy gating, the shared `draw_file_op_progress` modal) was
  already generic over `FileOpKind` and needed only one new wording case.

**Out of scope (YAGNI):** the file-picker dialog itself (multi-select
already worked — `allow_many = true` predates this phase); drag-and-drop
import; recursive folder import; any change to the already-backgrounded
ZIP/CBZ import path.

### Acceptance criterion
Picking multiple files via the existing import dialog no longer freezes the
UI: a progress bar tracks "N / M files", Esc cancels cooperatively leaving
already-imported files intact, and the finished-import status line reports
how many imported and — capped at 3 names plus a "+N more" suffix — which
ones failed.

**Status:** ✅ All tests pass (`scripts/test.sh`); `scripts/test.sh --asan`
clean.

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

**Framed vaults (header flags bit 0, Phase 26):** the AEAD plaintext of every
chunk AND of the sealed index blob is a chunk_codec frame:
```
  method u8 (0 = raw, 1 = deflate)
  if raw:     payload bytes
  if deflate: orig_len u64 LE | zlib-wrapped deflate stream
```
Flag clear (legacy): the plaintext is the payload verbatim, read and appended that
way forever.

**Index blob** (binary serialised; `INDEX_VERSION = 5`):
```
  version    u8
  root       node              (the tree, recursive — see below)
  saved_searches (v5+):                (Phase 18; omitted in v1–v4 blobs → empty)
    count    u16
    entries  { name_len u16; name u8[name_len];
               query_len u32; query u8[query_len] } [count]
```

**Index tree node** (binary serialised):
```
  node_type  u8  (0 = gallery, 1 = image, 2 = video)
  name_len   u16
  name_len   u16
  name       u8[name_len]  (UTF-8)

  tag_count  u16                     (Phase 12; v2+. Omitted entirely in v1 blobs.)
  tags       { tag_len u16; tag u8[tag_len] (UTF-8) } [tag_count]

  favorite   u8                      (Phase 13; v3+. Omitted in v1/v2 blobs → 0.)

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

> **Format extensions (Phases 12–18).** The index serialisation is
> versioned; each of these bumps `INDEX_VERSION` and reads older versions with
> the new fields defaulted, so existing vaults keep opening cleanly:
> - **Phase 12 (Tags):** ✅ shipped — a tag list (`u16 count` + length-prefixed
>   UTF-8) on **both** gallery and image nodes, written after `name`
>   (`INDEX_VERSION = 2`; v1 blobs read with empty tags). Gallery tags cascade to
>   descendants at read time (effective tags = own ∪ ancestors'); they are not
>   copied onto children.
> - **Phase 13 (Favorites):** ✅ shipped — a `favorite u8` flag on both node types,
>   written after the tag block (`INDEX_VERSION = 3`; v1/v2 blobs read as
>   not-favorited).
> - **Phases 15–16 (Video):** a video node kind (a `media_kind` discriminator)
>   and new `format` codes appended after `8=AVIF` (e.g. `9=MP4/H.264`); video
>   nodes reuse the same `data_*`/`thumb_*` layout (thumb = poster frame), with
>   the container stored across multiple encrypted chunks.
> - **Phase 18 (Advanced search):** ✅ shipped — a **vault-global saved-searches
>   block** serialised after the tree root (`u16 count` + per-entry `{ name,
>   serialised query }`, the query an opaque `ui::AdvancedQuery` blob), bumping
>   `INDEX_VERSION` to **5**; pre-v5 blobs read with an empty saved-searches list.
>   The block is not part of any node — it is vault-level metadata, persisted via
>   the same crash-safe double-buffer index swap and bounded against the fuzz suite.
