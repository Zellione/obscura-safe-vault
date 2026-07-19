## Phase 45 — Organization, security & fullscreen polish ✅

**Goal:** Six independent UX/organization gaps, grouped into one phase (same
multi-part shape as Phases 40/43/44).

Full design rationale: `docs/superpowers/specs/2026-07-18-phase45-organization-ux-polish-design.md`
(kept local/untracked per this repo's `docs/`-is-gitignored convention — this
file is the durable summary).

### Part 1 — Extend rename to remaining screens
- `FavoritesScreen` (`src/ui/favorites_screen.*`, shared base for
  `FavoritesImages`/`FavoritesGalleries` and the tag-overview drilldowns
  `TagImages`/`TagGalleries`) gains an `R` rename hook + a `RenameDialog`
  member, covering all four concrete screens at once.
- `AdvancedSearchScreen` (`src/ui/advanced_search_screen.*`): `R` renames the
  focused result when `Focus::Results` is active.
- Both reuse the existing Phase 44 `vault::rename_node`/`ui::RenameDialog`
  unmodified; each screen reloads its list from the vault after a successful
  rename.

### Part 2 — Mass tag apply (add + remove)
- `GalleryGrid`'s `G` key branches on `sel_.count()`: `<= 1` keeps today's
  single-node `TagEditor`; `>= 2` opens a multi-node mode operating on
  `sel_.indices()`.
- Multi-node mode shows the union of tags across the selection with a
  `(k/N)` count per tag; `Enter` on typed text adds the tag to every selected
  node missing it; the existing delete key removes a selected tag from every
  node that has it. Works uniformly on images and galleries.

### Part 3 — Copy password / passphrase to clipboard
- A "Copy" action next to the password field in `UnlockScreen` (unlock and
  create-vault modes) calls `SDL_SetClipboardText` on a temporary decode of
  `SecureTextField::bytes()`, `crypto_wipe`'d immediately after.
- "Generate passphrase" auto-copies immediately in addition to filling the
  fields.
- A ~25s auto-clear timer wipes the clipboard afterward, but only if
  `SDL_GetClipboardText()` still holds exactly what was last set (never a
  blind clear).

### Part 4 — Fullscreen hides the thumbnail strip
- `ImageViewer::viewport_rect()`/`strip_rect()` (SDL-aware wrappers over the
  pure, unchanged `strip_layout.h` helpers) return the full window / an empty
  rect while `win_.is_fullscreen()`; strip draw and hit-test are skipped
  entirely in that state, for both images and in-viewer video. Reappears
  only on leaving fullscreen — no hover-reveal.
- Escape-exits-fullscreen-first already works in the existing code
  (verified); treated as a regression-guard test, not a new fix.

### Part 5 — Bigger clickable video progress bar
- `src/ui/video_playback.cpp`: `TRACK_H` (currently `6.0f`) grows for visual
  thickness; the seek-bar's hit-test padding grows independently and more
  generously, so the draggable target is noticeably taller than the visible
  bar.

### Part 6 — Auto-lock-off badge visibility window
- `App` (`src/app/app.cpp`) adds a `badge_shown_until_` time point, reset to
  `now + 10s` only when the `U` key handler (`ToggleKeepUnlocked`) runs and
  leaves `keep_unlocked_` true. `draw_keep_unlocked_badge` draws only while
  `keep_unlocked_ && now < badge_shown_until_`, then stops outright (no
  opacity animation) until the next `U` press. Fixes the badge permanently
  covering the in-viewer volume control.

### Testing
Headless unit tests: multi-node tag union/count computation and add/remove
fan-out (vault-level, alongside existing `tags_search` tests); the clipboard
auto-clear equality check as a pure function; `should_show_badge(keep_unlocked,
now, shown_until) -> bool` (same shape as `should_auto_lock`). Fullscreen
strip-hiding, the seek-bar hit-test growth, and the actual `SDL_*Clipboard*`
calls are manual/smoke-checked (thin platform wrappers, Phase 31 precedent).

### Acceptance criterion
`scripts/test.sh` and `scripts/test.sh --asan` both green. Manual check:
rename from the favorites screen, a tag-overview drilldown, and an
advanced-search result; multi-select 3+ images and 2+ galleries and bulk add
then remove a tag; generate a passphrase and confirm it lands on the
clipboard, then confirm it clears ~25s later if untouched; enter fullscreen
on an image and on a video and confirm the thumbnail strip is gone in both,
reappearing on exit; confirm `Esc` still exits fullscreen first, gallery
second; the video seek bar is comfortably easier to click; toggle auto-lock
off and confirm the badge is visible for ~10s then disappears, not
permanently covering the volume control, reappearing only on the next `U`
press.

### Out of scope (deferred)
- Mixed galleries (images/videos/sub-galleries together) — Phase 46.
- Undo for mass tag apply.
- A generalized bulk-rename (only single-item rename per action).
