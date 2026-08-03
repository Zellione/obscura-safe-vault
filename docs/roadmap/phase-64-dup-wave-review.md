# Phase 64 — Wave-based duplicate review & review-screen memory fix ✅

**Goal:** Fix the review screen's memory churn and provide a scalable UX for large
duplicate sets. (1) Cull off-screen group rows and prune stale thumbnail fetches
so memory is bounded by visible tiles + the 256 MiB texture budget, independent
of group count. (2) Present the review in 20-group waves that are applied
incrementally, with post-apply span re-resolution for the Windows compact path.
No `.osv` format change, no `INDEX_VERSION` bump.

## 1. Review-screen memory churn

### Root cause (observed ~8 GB RSS with many duplicates)

`DuplicatesScreen::render_review()` drew **every** group row each frame with no
visibility culling, and `draw_member_tile()` submitted a thumbnail decode fetch
for every uncached member. The shared `gfx::TextureCache` is a 256 MiB LRU, so
once the total thumbnail set exceeds the budget the screen churns indefinitely:
decode → upload → evict → refetch, thousands of decrypt + decode round-trips per
pass, decoded RGBA buffers and mlock'd staging buffers continuously cycling
through the heap. The scan itself is **not** the problem — it streams through a
reused `SecureBytes` and video-reading caches one chunk, so scan-peak RAM ≈
largest single file.

### Fixes

- [x] `render_review` culls: only group rows intersecting the viewport are drawn.
  Row geometry is uniform (`dup_row_layout` / `dup_tile_height`), so the visible
  range is computable without iterating all groups.
- [x] Only **visible** member tiles submit `DecodeWorker::submit_fetch` requests.
- [x] Each frame, `worker_.retain(visible_keys ∪ inspect_key)` prunes queued
  decodes that scrolled out of view, reusing the pattern from the image viewer's
  thumbnail strip.
- [x] Result: RAM bounded by visible tiles + the 256 MiB texture budget,
  independent of total group count.
- [x] Tests: `render_review_culls_off_screen_rows` (verify row-range calculation),
  `scroll_large_review_stays_bounded` (memory regression; viewport scrolling with
  many groups stays within texture budget).

## 2. Wave-based review UX

### Design

Groups remain sorted largest-reclaimable-first (unchanged). The review is
partitioned into fixed, consecutive **waves of at most 20 groups**. Navigation,
toggles, marks, and the all-REMOVE invariant operate on the **current wave only**;
marks outside the wave are unreachable. This breaks up an overwhelming single
list into bite-sized chunks.

### Per-wave apply and advance

- `Ctrl+Enter` keeps exact semantics, scoped to the current wave: default-cancel
  confirm showing **this wave's** marked count + bytes → one `vault::remove_media_batch`
  call → advance to the next wave.
- **`N` skips the current wave** without applying. If touched marks exist, a
  default-cancel confirm guards the skip. Skipped files remain in the vault.
- **Post-apply span re-resolution:** `remove_media_batch` ends with
  `auto_reclaim_space()`. On Linux that hole-punches in place (offsets stable),
  but on Windows it falls back to `compact()`, which **rewrites and relocates
  surviving chunks** — the remaining groups' `data_spans` and thumb spans go
  stale. Therefore after every apply (on the main thread), each remaining member
  is re-looked-up by `node_path` and its `bytes`, `data_spans`, `thumb_offset` /
  `thumb_length` (and video `chunk_size` / `duration_us`) refreshed from the
  index. Members whose path no longer resolves are dropped; groups shrinking
  below 2 members are dropped. Wave order is preserved (no re-sort mid-review).
- `failed_` thumbnail memo (keyed by offset) is cleared on apply.
- After the final wave, the Done state reports totals **accumulated across all
  applied waves**: files removed, bytes reclaimed, waves applied/skipped.
- Leave semantics (`Esc`) unchanged: prompt only when the current wave has
  touched, unapplied marks. `blocks_idle_lock()` unchanged.

### Header, footer, and help

- Review header: `Duplicates — wave 3/12 · this wave: 20 groups · 84 MB
  reclaimable · 61 groups remaining`.
- Footer keybar gains `[N] skip wave`.
- `F1` help updated with the new key.

### Tests

- **Unit (pure):** wave partitioning (sizes, last partial wave), window
  navigation, per-wave `marked_*` / `can_apply` / `touched` scoping, advance and
  skip transitions, accumulated totals.
- **Integration (temp vault):** per-wave apply removes exactly the wave's marked
  files; re-resolution refreshes spans after an apply (forced compact path) and
  correctly drops vanished members / shrunken groups; thumbnails still decode
  correctly post-compact.
- Guard: the wave-apply's vault mutation must not trip the "vault changed under
  the review" stale banner — verified with an expected-change flag around the
  apply.

1799 tests / 0 failed (plain + ASan/UBSan).
