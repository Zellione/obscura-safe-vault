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

## Phase 13 — Favorites ⬜

**Goal:** Mark images or galleries as *favorite* and browse them through two
dedicated screens — an **Image Favorites** section and a **Gallery Favorites**
section.

### Tasks
- [ ] **Index format extension** — a dedicated `favorite` `u8` flag on both gallery and image nodes (bump `INDEX_VERSION` again; pre-existing vaults read as not-favorited). A dedicated flag, *not* a reserved tag, keeps favorites out of the tag namespace and out of tag search.
- [ ] `Vault` API — `toggle_favorite(node_path)`; `list_favorite_images()` (flat, whole-tree) and `list_favorite_galleries()`; persisted via the crash-safe index swap.
- [ ] **Toggle UX** — a single key marks/unmarks the focused image or gallery (e.g. `B` for bookmark — `F`/`L`/`T` are already bound in the viewer); favorited tiles show a small star badge in the grid.
- [ ] **Two distinct screens** — `src/ui/favorites_images.{h,cpp}` (a flat grid of every favorited image across the vault, opens the viewer on activate) and `src/ui/favorites_galleries.{h,cpp}` (a list/grid of favorited galleries; activating one navigates to that gallery in the normal grid). Both reachable via keys from the gallery grid (and listed in the breadcrumb/nav).
- [ ] Update `CLAUDE.md` module layout + `mem:core`.
- [ ] `tests/` — favorite flag round-trip for images and galleries; favoriting images populates the image-favorites list across the tree; favoriting a gallery populates the gallery-favorites list; un-favorite removes from both; a pre-favorites vault opens with none favorited.

### Acceptance criterion
Favoriting images and galleries populates the two distinct favorites screens;
the flags survive a lock/reopen; opening a favorite gallery navigates to it and
opening a favorite image opens the viewer.

---

## Phase 14 — Multiple vaults ⬜

**Goal:** Manage and open several vaults; move images between them.

### Tasks
- [ ] **Recent-vaults registry** — `src/platform/vault_registry.{h,cpp}`: a config-dir list of known vault **paths only** (add/list/remove). It stores **no secrets** — no passwords, no keys, no keyfile contents.
- [ ] `src/ui/vault_manager.{h,cpp}` — becomes the app's first screen: lists known vaults, plus create / open-other (file dialog) / remove-from-list. Selecting a vault transitions to the unlock screen for that path.
- [ ] **Multiple unlocked vaults** — `App` owns a collection of unlocked `Vault` instances with one *active* vault driving the gallery, plus a switcher (key or manager UI) to change the active vault. Each vault keeps its own mlock'd master key; locking one wipes only its keys.
- [ ] **Move between vaults** — `move_image(src_vault, src_path, dst_vault, dst_path)`: `read_image` from the source into mlock'd `SecureBytes` → `add_image` into the destination → `remove_image` from the source; both indices committed via the crash-safe swap. Plaintext exists only in the locked buffer during transfer (invariant #1 holds). Thumbnail is carried over or regenerated.
- [ ] Update `CLAUDE.md` (vault manager as first screen; new platform module) + `mem:core`.
- [ ] `tests/` — registry add/list/remove and a "no secrets persisted" assertion; two vaults unlocked simultaneously; `move_image` yields a checksum-matching image in the destination and removes it from the source (verified across a reopen of both); both indices remain valid after the move.

### Acceptance criterion
The manager lists multiple vaults; two can be unlocked at once; an image moved
between them matches its checksum in the destination and is gone from the source
after reopen; the registry never persists secrets.

---

## Phase 15 — Video playback (frames + seek) ⬜

**Goal:** Store video files in the vault and play their **video** track,
streaming decode directly from encrypted chunks with **no temp file**. Audio is
added in Phase 16.

### Tasks
- [ ] **Vendor FFmpeg/libav** — add a decode-only static build (minimal codec set: H.264/H.265 video, plus the demuxers; encoders disabled). cmake/configure-built into the codec staging prefix like the Phase 9 codecs (needs **nasm**, like libaom); `scripts/build_codecs.{sh,bat}` and `premake5.lua` `link_image_codecs()`/a new `link_av()` updated.
- [ ] **Encrypted-chunk streaming** — `src/media/chunk_avio.{h,cpp}`: a custom `AVIOContext` with read + seek callbacks backed by the vault's `ChunkStore`. Bytes are decrypted on demand into mlock'd buffers and wiped after use; seeks map a byte offset to the right encrypted chunk(s). **No bytes are ever written to a temp file.**
- [ ] **Index/format extension** — a video node type (or a `media_kind` discriminator on the existing node), new format codes appended to the enum, version bump; import stores the original container chunked and a **first-frame poster** as the gallery thumbnail.
- [ ] `src/media/video_decoder.{h,cpp}` — demux + video decode → frames; YUV `SDL_Texture` upload path added to `gfx::Renderer`.
- [ ] `src/ui/video_viewer.{h,cpp}` (or extend `image_viewer`) — play/pause, a **seek bar**, frame stepping; poster preview in the gallery grid.
- [ ] Update `CLAUDE.md` tech table (FFmpeg/libav, nasm) + `mem:tech_stack`/`mem:core`.
- [ ] `tests/` — the AVIO callback returns the correct bytes from encrypted chunks and **creates no filesystem file** (fs-write assertion); a short H.264 clip decodes the expected frame count; seek lands on the correct timestamp; a truncated/malformed video is rejected without crashing (extend the fuzz corpus).

### Acceptance criterion
A short H.264 clip imported into the vault plays its video track, seeks
correctly, shows a poster thumbnail in the grid, and a test asserts that **no
decrypted bytes are ever written to disk** during playback.

---

## Phase 16 — Audio & A/V sync ⬜

**Goal:** Add the audio track to the video pipeline with proper
audio/video synchronisation.

### Tasks
- [ ] **Audio decode** — extend `src/media/video_decoder` to decode the audio track (AAC/Opus; enable those decoders in the FFmpeg build) into PCM frames.
- [ ] **Audio output** — route decoded PCM to an `SDL_AudioStream`; resample to the device format as needed.
- [ ] `src/media/av_sync.{h,cpp}` — pure, unit-tested sync logic: presentation timestamps tracked against the **audio clock**, with video frame drop/hold decisions returned as data (no SDL in the unit).
- [ ] **Viewer controls** — volume + mute; the seek bar now seeks both tracks; pause stops audio cleanly.
- [ ] **Memory hygiene** — decoded audio PCM is treated like decoded pixels (transient buffers, no disk); the streaming AVIO path is unchanged.
- [ ] `tests/` — the audio decoder produces the expected sample count for a known clip; `av_sync` holds/drops frames correctly for fabricated PTS streams (drift ahead/behind); seek re-aligns both tracks; volume/mute math.

### Acceptance criterion
A short H.264+AAC clip plays with synchronised audio and video, seeks both
tracks correctly, and volume/mute/pause behave correctly — still with no
decrypted bytes written to disk.

---

## Phase 17 — Remote vaults (read-only streaming) ⬜

**Goal:** Open an `.osv` file on a network share or cloud storage in
**read-only** streaming mode. Read-only by design — it sidesteps the atomicity
and concurrency problems of remote writes.

### Tasks
- [ ] **Source abstraction** — refactor chunk/header/index access behind a `RandomAccessSource` interface (`read(offset, len) -> bytes`, `size()`). A local-file implementation wraps today's code byte-for-byte (a targeted refactor that also tightens the I/O boundary).
- [ ] `src/vault/http_source.{h,cpp}` — an HTTPS **range-request** source (libcurl): fetches the header + index once, then individual encrypted chunks on demand via `Range:` requests, backed by a small **mlock'd LRU chunk cache**. HTTPS only.
- [ ] **Read-only vault path** — `Vault::open_readonly(source)`; import / delete / compact / tag-edit / favorite-toggle / move are disabled and visibly greyed in this mode. Decrypted bytes still live only in mlock'd memory; the crypto path is unchanged.
- [ ] **Dependency** — vendor or system libcurl; `premake5.lua` link + CI provisioning across all three platforms.
- [ ] Update `CLAUDE.md` tech table (libcurl, `RandomAccessSource`) + `mem:tech_stack`/`mem:core`, and document the **known limitation**: the remote host observes ciphertext **and chunk-access patterns** (which chunks are fetched, and when).
- [ ] `tests/` — `RandomAccessSource` parity (local impl is byte-for-byte identical to direct file reads); a local HTTP test server serves an `.osv` and the vault opens read-only and reads images with matching checksums; every write operation is rejected in read-only mode; the LRU cache returns bytes identical to the origin; a network-share path works unchanged through the local-file source.

### Acceptance criterion
An `.osv` served over local HTTP(S) range requests opens read-only, browses, and
reads images with matching checksums while performing **no writes**;
import/delete/compaction are disabled; a network-share path works via the
local-file source.

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

**Index tree node** (binary serialised; `INDEX_VERSION = 2`):
```
  node_type  u8  (0 = gallery, 1 = image)
  name_len   u16
  name       u8[name_len]  (UTF-8)

  tag_count  u16                     (Phase 12; v2+. Omitted entirely in v1 blobs.)
  tags       { tag_len u16; tag u8[tag_len] (UTF-8) } [tag_count]

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

> **Planned format extensions (Phases 12–16).** The index serialisation is
> versioned; each of these bumps `INDEX_VERSION` and reads older versions with
> the new fields defaulted, so existing vaults keep opening cleanly:
> - **Phase 12 (Tags):** ✅ shipped — a tag list (`u16 count` + length-prefixed
>   UTF-8) on **both** gallery and image nodes, written after `name`
>   (`INDEX_VERSION = 2`; v1 blobs read with empty tags). Gallery tags cascade to
>   descendants at read time (effective tags = own ∪ ancestors'); they are not
>   copied onto children.
> - **Phase 13 (Favorites):** a `favorite u8` flag on both node types.
> - **Phases 15–16 (Video):** a video node kind (a `media_kind` discriminator)
>   and new `format` codes appended after `8=AVIF` (e.g. `9=MP4/H.264`); video
>   nodes reuse the same `data_*`/`thumb_*` layout (thumb = poster frame), with
>   the container stored across multiple encrypted chunks.
