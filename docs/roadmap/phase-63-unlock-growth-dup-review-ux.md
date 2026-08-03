# Phase 63 — Vault growth on unlock (Windows) & duplicate-review UX ✅

**Goal:** Fix two owner-reported defects: (1) on Windows, an old vault's file
size grows on every unlock and the writes make the app laggy; (2) the duplicate
review requires manual marking for every group, allows marking every copy of a
group for removal, and the full-screen inspect preview blanks to a black box
shortly after appearing. No `.osv` format change, no `INDEX_VERSION` bump.

## 1. Vault grows on every unlock (Windows)

### Root cause (traced before fixing)

`GalleryGrid::refresh()` runs `ui::repair_unknown_video_metadata` on every
gallery visit — including the root gallery right after unlock — and
`App::promote_pending()` has already started the ImportQueue's **CommitLane**
for the session. `Vault::repair_video_metadata()` appended the re-probed poster
chunk on `fp_` **without holding `write_mutex_`**, violating the Phase 50
locking protocol (staging and the CommitLane worker both hold it for every
`fp_` write). With ≥ 2 repairable videos, the main thread's poster append
(`seek_end` + `fwrite` + `fflush`, three separately-locked stdio calls) runs
concurrently with the lane worker committing the *previous* repair's index
blob on the same `FILE*`. The interleaving can land `write_header`'s payload at
EOF instead of offset 0 — the PR #109 failure class (the seek and the write are
separate stdio calls, and the shared file position moves between them) — so
the active-slot flip never persists:

- next unlock loads the **stale** slot → the repaired videos read as
  `codec = Unknown` again → the repair re-runs → new poster chunks and new
  index blobs are appended → **the file grows on every unlock, forever**;
- the repair work itself (whole-video reads + probes + commits on the main
  thread) makes every unlock and gallery visit laggy.

Windows amplifies both halves: `FlushFileBuffers` is slow (widening the race
window to near-certainty with several repairs back-to-back), and Windows has
no hole-punch reclaim, so the dead blobs accumulate until a manual compact.

### Fixes

- [x] `Vault::repair_video_metadata()` holds `write_mutex_` across the poster
  append + sync (`src/vault/vault.cpp`) — the one unguarded `fp_` write path.
- [x] `Vault::unlock()` logs via `platform::log_error` when the active index
  slot is unreadable and the previous slot is recovered — this fallback was
  silent, which is exactly why the corruption loop was invisible.
- [x] Repeated re-probe lag memoized: `VideoMeta::probe_failed_session`
  (transient, never serialized) marks a video whose probe failed this session,
  so gallery refresh stops re-reading the whole file on every visit; the
  repair retries on the next unlock.
- [x] Tests: `repair_video_metadata_persists_under_running_commit_lane`
  (8 forced-Unknown videos repaired while a CommitLane commits concurrently;
  cold reopen must see every repair persisted and every poster decrypt
  cleanly) and `repair_video_metadata_memoizes_failed_probe_per_session`.

## 2. Duplicate review UX

- [x] **Default marks keep the first member.** `DupReview`'s constructor now
  pre-marks every group `keep = members[0]`, REMOVE for the rest — an
  untouched review applied with `Ctrl+Enter` dedups straight down to one copy
  per group instead of requiring a manual `Space` on every member.
- [x] **One member always stays kept.** `DupReview::toggle` now returns `bool`
  and refuses to unmark a group's last keeper, so the all-REMOVE state is
  unreachable through the UI (the apply-time refusal stays as defense). The
  refused `Space` shows "At least one copy of each group stays kept".
- [x] Because marks now pre-exist, the leave-confirm on `Esc` and
  `blocks_idle_lock()` are gated on a new `marks_touched_` flag — untouched
  defaults are not invested work and must neither nag on exit nor suppress
  the idle auto-lock.
- [x] **Inspect preview blanking to a black box.** `Enter`'s full-screen
  inspect uploaded the decoded *full-resolution original* into the **shared
  thumbnail `TextureCache`** — the exact anti-pattern `FullTexCache` exists to
  prevent for the viewer ("a single large decode can't evict the gallery
  thumbnails"). The big upload evicted the review thumbnails (tiles flash
  back to their black placeholder), and the inspect texture itself could be
  evicted mid-inspect (image blanks to the dim backdrop). The inspect texture
  is now **owned by `InspectState`** (uploaded on first draw, destroyed by
  `close_inspect()` on close/replace/destruction) and never touches the
  shared cache; a failed upload (e.g. past the GPU's max texture size) closes
  the inspect with a status message instead of a silent black box.

1786 tests / 0 failed (plain + ASan/UBSan).
