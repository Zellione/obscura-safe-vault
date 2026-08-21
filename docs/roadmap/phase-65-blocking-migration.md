# Blocking vault migration (Phase 65) — shipping specification

**Status:** ✅ shipped
**Date:** 2026-08-05

## Problem

Two vault-mutating repairs currently run lazily while the user browses, so a `.osv`
file changes on the filesystem during ordinary reading:

1. **Video metadata repair** — `src/ui/video_repair.cpp:10`, called from
   `GalleryGrid::refresh()` (`src/ui/gallery_grid.cpp:290`). Every video whose
   `vmeta.codec == VideoCodec::Unknown` (imported before a decoder for its codec
   existed) is fully decrypted, re-probed with `media::probe_video()`, and given a
   **newly appended poster chunk** plus an index rewrite. It runs **on the main
   thread**, so the UI stalls for the decrypt and probe.

   A failed probe is memoized for the session by `VideoMeta::probe_failed_session`
   (`src/vault/index.h:146`), so it does not repeat per gallery visit — but the flag is
   not persisted, so a still-undecodable video pays the full read + decrypt + probe
   again on **every unlock**, forever. Persisting that "gave up" verdict across sessions
   is precisely what the watermark provides.

2. **Animated-flag repair** — `src/ui/anim_repair.cpp:9`, called from `ImageViewer`
   (`src/ui/image_viewer.cpp:228`). Images from pre-`INDEX_VERSION 7` vaults carry
   `meta.animated == false` regardless of content; opening one re-sniffs the already
   decrypted bytes and persists a correction. Gated once-per-chunk-per-session by
   `AnimSniffGate`, but still an index write mid-browse.

Both exist purely because older vaults were written by a build with less capability.
They are upgrade work dressed as browsing.

## Goal

Detect pending upgrade work once, offer to do it as a single **blocking, parallel,
batch-committed** pass, and stop mutating the vault during browsing entirely.

## Scope

In scope: both repairs above, plus a compaction pass at the end to reclaim what the
repairs orphan. Out of scope: duplicate scanning (`src/ui/dup_scan.*` is an on-demand
analysis that persists nothing), and any change to the chunk or header format.

---

## 1. Data model

Two fields appended to `vault::VaultSettings` (`src/vault/index.h:250`), serialized at
the end of the existing vault-global settings block. `INDEX_VERSION` 9 → **10**; pre-v10
blobs read back `0, 0` and are therefore correctly treated as un-migrated.

```cpp
struct VaultSettings {
    // ...existing fields...
    uint8_t  migrated_index_version = 0;   // index-version-driven content backfills
    uint16_t migrated_probe_caps    = 0;   // codec-capability-driven video re-probe
};
```

The staleness gate compares against dedicated constants, **not** against `INDEX_VERSION`
itself:

```cpp
// src/vault/index.h — bump ONLY when a new index version adds a field needing a
// content backfill. Currently 7: the version that introduced ImageMeta::animated.
inline constexpr uint8_t MIGRATION_INDEX_VERSION = 7;

// src/media/video_probe.h — bump when decode capability expands (a new codec).
inline constexpr uint16_t PROBE_CAPS_GEN = 1;

pending = (settings.migrated_index_version < vault::MIGRATION_INDEX_VERSION)
       || (settings.migrated_probe_caps    < media::PROBE_CAPS_GEN);
```

Gating on raw `INDEX_VERSION` would re-trigger the animated backfill on every future
version bump — a new sort field or per-node flag would pointlessly re-offer a migration
that has nothing to do with animation. `MIGRATION_INDEX_VERSION` moves only when a
backfill is actually added.

Out-of-range bytes in the new fields are rejected on deserialise, not clamped, matching
the established rule for `sort_key`, `swatch`, and `animated`.

## 2. Detection

New SDL-free module `src/vault/migration.*`, unit-testable without a window or a
rendered frame. Detection is a **pure tree walk with zero I/O**, so the offer appears
instantly regardless of vault size:

- video arm: `is_video() && vmeta.codec == VideoCodec::Unknown`
- animated arm: `is_image() && vault::format_can_animate(meta.format) && !meta.animated`

It returns per-arm counts plus total bytes to be read.

The animated arm deliberately over-counts: a genuinely static GIF is indistinguishable
from an un-backfilled one without decrypting it, so it *is* real work that must be done
once. The watermark, not the detector, is what prevents recurrence.

## 3. The offer

After `UnlockJob` completes and before the gallery renders:

- Watermark stale **and** counts non-zero → a modal stating the counts and total bytes.
  No time estimate: an honest byte count beats a fabricated ETA.
- Watermark stale **and** counts zero → no modal. Write the watermark and commit silently.
- Watermark current → nothing happens.

**Accept** → the blocking progress modal (§4).
**Decline** → nothing runs. Because the lazy repair paths are removed entirely (§5),
the vault does not mutate while browsing at all; un-migrated videos simply render
without a poster, and un-backfilled animated images render as static. The migration is
re-offered at the next unlock, and is invocable manually from the settings overlay.

## 4. Execution

The blocking modal removes the need for Phase 50's staging dance. That rule
(main-thread-only tree, worker stages chunks, main thread calls `attach_staged`) exists
because background *import* runs concurrently with browsing. A blocking migration has no
such concurrency, so this follows `FileOpJob`'s contract instead: while `active()`, the
job owns the vault exclusively and the UI only polls progress and draws a modal.

New `ui::MigrationJob` (`src/ui/migration_job.*`), reusing `vault::OpProgress` for the
N/M bar and cooperative cancel:

- **One coordinator thread** owns the index tree and all writes to `fp_` (guarded by
  `write_mutex_`, per the Phase 50 append protocol). It builds the work list
  from the tree (stable — nothing else mutates it during the job), feeds the pool, drains
  results, applies metadata, and appends poster chunks.
- **A decode pool** of `max(1, hardware_concurrency() - 1)` workers performs the pure-CPU
  work: decrypt → `media::probe_video()` / `image::is_animated()` → **encode the poster**.
  Reads go through `vault::read_thumb_span` (`src/vault/vault.h:424`), the any-thread-safe
  path `DupScanJob` already relies on. Poster encoding inside the worker is where most of
  the parallel gain comes from; only the chunk *append* is serialized on the coordinator.
- **The results queue is bounded** at approximately 2× the pool size. This is a hard
  requirement, not hygiene: decoded frames and encoded posters live in `mlock`'d
  `SecureBytes`, and `platform::grow_secure_mem_budget()` sets a 256 MiB process budget.
  An unbounded queue would exhaust it and start failing locks.
- **One `commit_index()` at the end**, then the watermark write, then `compact(&progress)`
  as a third phase — `compact` already accepts an `OpProgress*` (`src/vault/vault.h:341`),
  so the coordinator calls it directly. One modal, three phases, no `FileOpJob`
  involvement. Compaction is skipped when `wasted_bytes()` (`src/vault/vault.h:328`) does
  not justify the rewrite.

FFmpeg use stays per-worker: each worker builds its own `AVFormatContext` /
`AVCodecContext` over its own `AVIOContext` on its own chunk spans, sharing no FFmpeg
state. Re-entrancy of the probe path must be confirmed against `src/media/video_probe.cpp`
during implementation.

### Crash and cancel semantics

Nothing commits until the end, so a crash mid-migration leaves the vault exactly as it
was — the active index slot is untouched. Poster chunks appended before the crash are
dead ciphertext reclaimed by `compact`. This is the tradeoff inherent in the batched
commit, and it is accepted: the pre-migration state is always intact and readable.

A user cancel commits the work applied so far (correct and durable), does **not** write
the watermark, and skips compaction. The migration is re-offered next unlock.

### Error handling

- A node that still cannot be probed is counted as skipped and left `Unknown`. The
  watermark still advances, because `migrated_probe_caps` records "attempted at this
  capability level" — it correctly will not retry until decode capability moves.
- A read or decrypt failure is counted as failed and logged without content, per
  invariant 5 (no plaintext or key material in logs).
- A write failure at commit is a hard stop: the error surfaces in the outcome and the
  watermark is **not** written.

## 5. Removing the lazy paths

`src/ui/video_repair.*` and `src/ui/anim_repair.*` are deleted, along with their call
sites at `src/ui/gallery_grid.cpp:290` and `src/ui/image_viewer.cpp:228`, and the
`AnimSniffGate` member at `src/ui/image_viewer.h:267`. Import always sniffs the real
bytes, so a migrated vault cannot acquire a new un-backfilled node through normal use.

### The transfer hole this opens

Cross-vault transfer (`src/vault/transfer.*`) can move a node from an **un-migrated**
vault into a **migrated** one. The destination's watermark then claims "done" while an
un-backfilled node has just arrived — and with the lazy paths deleted, nothing would
ever correct it.

Fix: on transfer-in, lower the destination's watermark to the source's whenever the
source is behind, which re-offers the migration at the destination's next unlock. The
exact insertion point must be verified against `src/vault/transfer.cpp` during
implementation rather than assumed.

## 6. Testing

TDD per the project workflow — tests first, then code.

**Unit (no I/O):**
- Detection counts over synthetic trees: videos with/without `Unknown`, animatable and
  non-animatable image formats, empty vault, deeply nested galleries.
- Watermark serialize/deserialize round-trip at v10.
- A pre-v10 blob reads back `0, 0`.
- Out-of-range watermark bytes are rejected, not clamped.
- The new fields added to the existing index fuzz suite.

**Integration:**
- `test_only_force_video_codec_unknown` (`src/vault/vault.h:224`) → migrate → codec,
  dimensions, duration and poster are filled; watermark set; a second unlock offers
  nothing.
- A static GIF stays `animated == false`; an animated GIF flips to `true`; the watermark
  suppresses any re-scan.
- Cancel mid-pass leaves a readable vault with the watermark unset and partial work
  committed.
- A still-undecodable video is skipped, the watermark still advances, and it is not
  retried on the next unlock.
- Transfer from an un-migrated vault lowers the destination watermark.

**Sanitizers:**
- `scripts/test.sh --asan` for the crypto/vault surface.
- The existing TSAN leg (Phase 42) must cover the new pool. This is the **first worker
  pool in the codebase**, so TSAN is the real gate here, not a formality.

## 7. Files

**New**
- `src/vault/migration.{h,cpp}` — detection, watermark staleness, pure and SDL-free
- `src/ui/migration_job.{h,cpp}` — coordinator + decode pool
- `tests/vault/test_migration.cpp`, `tests/ui/test_migration_job.cpp`

**Modified**
- `src/vault/index.{h,cpp}` — watermark fields, `INDEX_VERSION` 10, `MIGRATION_INDEX_VERSION`, serialize/deserialize
- `src/media/video_probe.h` — `PROBE_CAPS_GEN`
- `src/app/app.cpp` — post-unlock offer hook
- `src/ui/settings_overlay.cpp` — manual trigger
- `src/vault/transfer.cpp` — watermark lowering on transfer-in
- `src/ui/gallery_grid.cpp`, `src/ui/image_viewer.{h,cpp}` — call-site removal
- `ROADMAP.md`, `docs/roadmap/phase-65-blocking-migration.md`

**Deleted**
- `src/ui/video_repair.{h,cpp}`, `src/ui/anim_repair.{h,cpp}` and their tests

`scripts/gen.sh` must be re-run so `compile_commands.json` and clangd stay accurate.

## 8. Memory graph updates

Per `AGENTS.md`, this phase requires updating:
- `mem:module/vault` — new `migration.*`, watermark fields, transfer change
- `mem:module/ui` — new `migration_job.*`, removal of `video_repair.*` / `anim_repair.*`
- `mem:vault_format` — `INDEX_VERSION 10` and the settings-block layout
- `mem:ui_spec` — the unlock-time offer and the three-phase progress modal
- `mem:core` — the worker-pool concurrency model alongside the Phase 50 notes
