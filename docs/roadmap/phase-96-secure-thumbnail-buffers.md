# Secure thumbnail / decoded intermediates (Phase 96)

**Status:** 🔜 ready for review (PR 96a + PR 96b — Phase C delivered as two PRs)
**Date:** 2026-08-30

This is **Phase C** of the 2026-08-29 security audit remediation
(`AUDIT.md` / `AUDIT_IMPLEMENTATION.md`, phases A–G mapping to app phases
94–100). Phase C fixes **OSV-AUD-003** (High — decrypted media bypasses
secure-memory storage) for the *image and thumbnail* half. It is delivered
as **two PRs**: this document covers **96a** (secure growable buffer
primitive + thumbnail/staging/transfer/probe/migration conversion) and
**96b** (secure stb allocator). No `.osv` byte change; `INDEX_VERSION`
stays 12.

## Finding being fixed

**OSV-AUD-003 — decrypted media bypasses secure-memory storage.** Several
paths put decrypted or derived plaintext in ordinary, unwiped, non-mlock'd
allocations that never trigger the `mlock_failure_seen()` warning:

1. `make_thumbnail()` stored the resized RGB pixels **and** the derived JPEG
   in `std::vector<uint8_t>` objects, copied and moved through staging,
   import, migration, and transfer without guaranteed wiping.
2. `StagedThumb::thumb_jpeg` / `StagedVideoInfo::poster_jpeg`,
   `VideoProbeResult::poster_jpeg`, the migration worker's result buffers,
   and cross-vault `prestage_*_info` all held (or copied decrypted bytes
   into) `std::vector<uint8_t>`.

Anyone with read access to process memory (swap, hibernation, crash dumps, a
memory-disclosure bug anywhere in the process) could recover recognizable
thumbnails and post-resized pixel content that should have been locked and
wiped.

## What shipped (96a)

### `crypto::SecureBytes` growth API (`src/crypto/secure_mem.h`)

The thumbnail encoder cannot size its buffer up front, so `SecureBytes`
gained capacity-aware growth:

- `reserve(n)` — geometric growth, **wipes + unlocks + releases the old
  locked block** on every capacity bump (recorded by the wipe-observation
  seam) and honours `inject_secure_allocation_failure`; contents are
  preserved and OOM leaves the object exactly as it was.
- `append(span)`, `push_back(u8)` — grow-then-copy; OOM returns `false`
  without disturbing existing bytes.
- `clear()` — wipe + release the whole block (size and capacity both go to
  0; transient thumbnail buffers do not hold the lock budget).
- `capacity()` accessor; all locks now cover `capacity_` so the page-lock
  registry stays balanced across growth.

`resize(n)` keeps its exact-size semantics (still used by decoders and vault
reads); `assign`/`fill`/`operator[]`/`as_span` are unchanged.

### `make_thumbnail()` returns `crypto::SecureBytes`

`src/image/thumbnail.{h,cpp}` — the returned JPEG and the resized RGB pixels
are now `SecureBytes`. The `stbi_write_jpg_to_func` callback is
capture-less (stbi wants a function pointer), so the growable sink rides in
`ctx` as `{crypto::SecureBytes bytes; bool failed;}`; a mid-encode `append`
OOM marks `failed` and `make_thumbnail` returns `nullopt` — **never a
truncated JPEG**.

### Application-owned thumbnail/poster buffers → `SecureBytes`

| Buffer | Was | Now |
|---|---|---|
| `make_thumbnail` returned JPEG | `std::optional<std::vector<uint8_t>>` | `std::optional<crypto::SecureBytes>` |
| resized RGB / JPEG encoder sink | `std::vector<uint8_t>` | growable `crypto::SecureBytes` |
| `vault::StagedThumb::thumb_jpeg` (`staging.h`) | `std::vector<uint8_t>` | `crypto::SecureBytes` |
| `vault::StagedVideoInfo::poster_jpeg` (`staging.h`) | `std::vector<uint8_t>` | `crypto::SecureBytes` |
| `media::VideoProbeResult::poster_jpeg` (`video_probe.h`) | `std::vector<uint8_t>` | `crypto::SecureBytes` |
| migration `Result::{thumb_jpeg, poster_jpeg}` (`migration_job.cpp`) | `std::vector<uint8_t>` | `crypto::SecureBytes` |
| staging `DecodedThumb::thumb_bytes` (`staging.cpp`) | `std::vector<uint8_t>` | `crypto::SecureBytes` |
| transfer `prestage_image_info` / `prestage_video_info` | `assign(ptr, len)` into a vector | `assign(blob.as_span())` — SecureBytes→SecureBytes |

`import_queue.cpp` needed no change: it stores into `StagedThumb.thumb_jpeg`,
which is now secure by construction. Non-owning consumers take
`std::span<const uint8_t>` (`append_chunk`, `apply_image_thumb`,
`apply_video_poster`, `VideoProbeApply`) and read via `.as_span()`.

### Opaque codec internals stay library-owned

libwebp's animated-decoder canvas (`decode_animated_webp_first_frame`) and
libheif's decoded `heif_image` plane offer **no caller-supplied-buffer
decode API**, so their scratch remains library-owned — the application
copy into `SecureBytes` is secure, and the boundary is now explicit in the
AGENTS.md hardening notes. FFmpeg's packets/frames/audio are the Phase D
workstream (OSV-AUD-003 remainder).

## Tests

1. **`SecureBytes` growth** (7 new in `tests/crypto/test_secure_bytes.cpp`):
   append extends in order, push_back appends a byte, reserve-then-append
   keeps the data pointer stable, reserve(0) is a no-op, **every released
   capacity block is wiped** (observed by the wipe seam through 2000
   push_backs), **growth OOM preserves contents**, and `clear()` wipes +
   releases (capacity → 0).
2. **Thumbnail security** (3 new in `tests/image/test_image.cpp`):
   `thumbnail_output_is_secure_and_locked` (`is_locked()` on a real JPEG
   output), `thumbnail_pipeline_release_blocks_are_wiped` (resized RGB +
   encoder reallocations all observed zeroed), and
   `thumbnail_encoder_callback_allocation_failure_returns_nullopt`
   (injection at allocation #0 = resized, #1 = first encoder append; both
   `nullopt`, never a truncated JPEG).
3. **Staging teardown + transfer** (2 new in
   `tests/vault/test_transfer_prestaged.cpp`):
   `stage_image_precomputed_thumb_wiped_on_teardown` and
   `transfer_decrypted_thumb_never_plain_vectors` — decrypted thumbnail
   bytes ride SecureBytes end to end and every release is observed wiped.

Existing thumbnail, prestaged-add, transfer, video-probe, and migration
tests were updated to the secure types (test-local `secure_jpeg({...})`
helper replaces the removed `std::vector` initializer-list assignments).

2221 tests / 0 failed (baseline 2209 + 12); ASAN clean; TSan clean; Release
2221/0; no-FFmpeg parity leg 2043/0 (baseline 2031 + 12 — the AV-gated video
tests are naturally absent).

## What shipped (96b) — secure stb allocator

**OSV-AUD-003 point #1:** stb decodes the full-size RGB image into a buffer
allocated through `STBI_MALLOC`; before 96b that shim was `calloc` + `free`
— zero-initialised (the malformed-JPEG defence) but **never page-locked and
never wiped on release**. The full-size decode was copied into
`ImageData::pixels` (a `SecureBytes`) and the ordinary, un-locked original
was then `free()`'d.

### `src/image/stb_secure_alloc.h` (new)

A header-prefixed secure allocator backing `STBI_MALLOC` /
`STBI_REALLOC_SIZED` / `STBI_FREE`:

- The block header (`{size_t size; bool locked;}`) is stored immediately
  **before** the payload so `STBI_FREE` — which stb's C contract hands no
  size — recovers the length from the header rather than a global map
  (thread-safe: decode runs on both the main and the DecodeWorker thread).
- **Zero-initialised** via `calloc` (the malformed-JPEG defence is
  preserved; on grow the zeroed tail is preserved, `realloc`-style).
- **Best-effort page-locked** through the shared page registry
  (`crypto::detail::mem_lock`), with the once-per-process mlock warning on
  failure — so these allocations now participate in the F1 degraded-lock
  reporting.
- **`crypto_wipe`'d before every free/realloc** (observed by the wipe
  seam), honouring stb's realloc-failure contract (old block stays alive).
- Length arithmetic is validated (header + payload overflow checked) and
  allocation failures honour `inject_secure_allocation_failure` for
  deterministic tests.

`src/image/decode.cpp` now routes the three macros through these functions;
`decode_stb` needs no logic change. Inline codec-plane boundary comments were
added to `decode_webp.cpp` (animated canvas) and `decode_heif.cpp`
(`heif_image`) — both are library-owned with no caller-supplied-buffer API;
the app copy is secure and FFmpeg internals are Phase D.

### 96b tests (7 new, `tests/image/test_stb_alloc.cpp`)

`stb_secure_alloc_zero_initializes`,
`stb_secure_alloc_realloc_zeroes_tail_and_preserves_head`,
`stb_secure_alloc_wipes_on_free`,
`stb_secure_alloc_wipes_released_block_on_realloc`,
`stb_secure_alloc_locks_payload_best_effort` (page-registry refcount rises
on alloc, drops on free),
`stb_secure_alloc_realloc_failure_keeps_old_block`, and
`decode_stb_wipes_intermediate_raw_buffer` (a full stb decode's intermediate
is wiped on `stbi_image_free`).

**Peak transient locked memory during a decode roughly doubles** (the
full-size stb raw buffer is now locked alongside the `SecureBytes` pixel
copy before the raw is freed) — well inside the 256 MiB budget for a single
in-flight decode, and consistent with the existing "best-effort, degrade one
buffer" semantics.

## Verification notes

- 96a + 96b suites: `scripts/test.sh` / `--asan` / `--tsan` / `--release` all
  green at 2228/0 (96b adds 7); the no-FFmpeg leg (`premake5 --no-av` +
  direct binary run, since `test.sh` re-generates with AV) at 2050/0
  (2043 + 7); `git diff --check` clean.
- The README valgrind run targeting `decode_malformed_jpeg_returns_nullopt`
  keeps guarding the zero-init defence under the 96b allocator swap.
- The audit's `ulimit -l 0` degraded-lock smoke is manual: run the app with
  a zero memlock budget and confirm the F1 "some decoded data is swappable"
  line appears when image/thumbnail paths run — the mechanism is
  process-global (`mlock_failure_seen`) and covered by the Phase 90 status
  test at the unit level; the Phase 96/96b tests run with a normal budget.

## Deliberately unchanged

- **No `.osv` byte change; `INDEX_VERSION` stays 12.**
- **Ciphertext stays in ordinary vectors** — only decrypted/derived content
  needs secure storage (the audit's design note; `std::vector<uint8_t>`
  remains correct for ciphertext and non-secret metadata).
- **Opaque codec internals stay library-owned** — libwebp's animated-decoder
  canvas and libheif's decoded `heif_image` planes have no
  caller-supplied-buffer decode API (documented inline in 96b); the app copy
  is `SecureBytes`.
- **FFmpeg buffers** (`ChunkAvio`, packets, frames, audio, filters) are
  Phase D.

## Memory graph updates

- `mem:module/media` — *thumbnail/posters secure-buffer ownership* section
  with the conversion table and the codec-owned-plane boundary; 96b adds the
  secure stb allocator to the picture.
- AGENTS.md hardening notes — updated stb bullet (secure, zero-initialised,
  wiping allocator) + the Phase 96 thumbnail/poster bullet and the
  opaque-codec boundary.