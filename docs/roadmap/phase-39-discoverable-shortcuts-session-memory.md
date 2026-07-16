## Phase 39 — Discoverable shortcuts & session-scoped gallery memory

**Goal:** Fix four related UX gaps reported from live use: (1) every screen's
inline keyboard-shortcut legend is a single text line that visibly runs off
the window edge at normal window sizes, hiding shortcuts past that point;
(2) fullscreen (`F11`/double-click) and keep-unlocked (`U`) already work but
are effectively undiscoverable, and `U` isn't reachable from the image
viewer; (3) `App` constructs a brand-new screen instance on every navigation,
so the gallery grid's List/Grid choice, the viewer's thumbnail-strip side,
and any video playback position are lost on a routine "open an image, go
back" round trip; (4) video tiles show a blank box instead of their stored
poster frame in both the gallery grid and the image viewer's thumbnail strip,
because the poster-existence check reads the wrong index field. See
`docs/superpowers/specs/2026-07-15-phase39-ui-discoverability-session-memory-design.md`
for the full design.

**Part 1 — `F1` help popup, shortcut discoverability, video poster
thumbnails.** Replaces every screen's overflow-prone inline legend with an
`F1`-triggered scrollable, grouped popup (`Esc`/`Q` to close); documents the
already-working fullscreen and keep-unlocked shortcuts and wires keep-unlocked
into the image viewer too; fixes the video-poster thumbnail gate.

### Tasks (Part 1)
- [x] `src/ui/tile_thumb.h`/`.cpp` gain `ui::thumb_key_for(node)` (pure): a
  video's thumbnail lives in `vmeta.poster_offset`/`poster_length`, not
  `meta.thumb_length` (always 0 for a video node) — the bug this fixes.
  `tile_thumb_texture` (grid tiles) and `ImageViewer::thumb_texture` (strip)
  both switch to it.
- [x] `src/ui/help_popup.{h,cpp}` (new): `HelpEntry`/`HelpGroup` data types,
  `HelpPopupState`, pure scroll/open/close/key logic, and
  `draw_help_popup` — a centred, scrollable, veiled panel matching the
  existing `consent_dialog`/`progress_modal` visual style.
- [x] `Screen` (`src/ui/screen.h`) gains a `help_groups()` virtual (default
  empty). `App` owns one `HelpPopupState`, intercepts `F1` globally, and
  renders/routes input to the popup — mirrors the Phase 33 keep-unlocked
  corner-badge overlay pattern.
- [x] Every concrete screen (`GalleryGrid`, `ImageViewer`, `FavoritesImages`,
  `FavoritesGalleries`, `TagGalleries`, `TagImages`, `TagOverviewScreen`,
  `AdvancedSearchScreen`, `VaultManager`, `UnlockScreen`) overrides
  `help_groups()` with its own grouped shortcut content; every inline legend
  string is deleted and replaced with a fixed `[F1] Help` footer fragment.
- [x] `ImageViewer::handle_key` gains an `SDLK_U` case
  (`request(NavKind::ToggleKeepUnlocked)`), matching the grid.
- [x] Update `CLAUDE.md` (module table: `help_popup.*`, `tile_thumb.h`'s
  `thumb_key_for`; UI/UX spec section: `F1` help convention) + `mem:core`.
- [x] `tests/` — pure tests for `thumb_key_for` (image vs. video gating,
  including a regression case for the always-zero `meta.thumb_length` on a
  video node) and for `help_popup`'s scroll/open/close logic; a headless
  render smoke test for `draw_help_popup` (mirrors `test_progress_modal.cpp`).

**Part 2 — Session-scoped gallery/viewer memory.** A `GallerySessionState`
(modeled on the existing `AdvancedSearchState`) carries the last-used
List/Grid view and thumbnail-strip side, plus a single "last video watched"
resume bookmark (paused, not autoplaying), through `App`'s screen
reconstruction on every grid↔viewer round trip; reset on lock/vault-switch
like `AdvancedSearchState` already is. Does **not** duplicate gallery
path/focus-index tracking, since `Nav.path`/`Nav.index` already carries that.

### Tasks (Part 2)
- [x] `src/ui/gallery_session_state.h` (new, pure): `GallerySessionState`
  struct + `reset()`; unit-tested.
- [x] `App` gains a `GallerySessionState session_` member, populated from the
  outgoing screen's view/strip-side/video-position just before
  `on_exit()`/destruction on every `ToGallery`/`ToViewer` transition, and fed
  into the new screen's constructor instead of always defaulting.
  `GalleryGrid`/`ImageViewer` constructors gain an initial
  view/strip-side parameter.
- [x] `session_` resets at the same points `adv_session_` already does:
  `LockActive`, idle auto-lock, vault switch (`promote_pending`).
- [x] Leaving the viewer on a video captures its playback position into
  `session_`; reopening the same video seeks there and stays paused; a
  different video (or an image) clears/ignores the bookmark.
- [x] Update `CLAUDE.md` (`App`'s new `GallerySessionState` member,
  `src/ui/gallery_session_state.h` module entry) + `mem:core`.
- [x] `tests/` — pure `GallerySessionState::reset()` test; a real-fixture
  `VideoPlayback::seek()` test (moves position, stays paused). No
  `GalleryGrid`/`ImageViewer` integration test was added: neither class has
  ever been unit-tested in this codebase (both require a real `gfx::Window`,
  which no existing test constructs — `test_window_visibility.cpp` only
  exercises the pure `window_flags_visible` helper), so there is no harness to
  extend. The App-side capture/restore wiring (`capture_session_state`,
  `enter_viewer`, the `current_gallery_view`/`current_strip_side`/
  `capture_video_resume`/`apply_video_resume` free friends) is covered
  indirectly by the full test suite + `--asan` staying green and a clean
  launch/shutdown smoke run; interactive keyboard-driven verification of the
  round trip itself was not possible in this environment (no GUI-automation
  tool available) and is worth a manual pass before merge.

**Out of scope (YAGNI):** a keybinding-customization system; wiring the new
small self-contained overlays (`quick_switch`, `theme_picker`, `tag_editor`,
`transfer_dialog`, `consent_dialog`, `search_overlay`) into the `F1` help
system — they don't have the overflow problem this phase fixes; per-gallery
view-mode/strip-side memory (one global "last used" value only); per-video
resume bookmarks (a single "last video watched" bookmark only); any change to
the vault format, index schema, or persisted data.

### Acceptance criterion
Pressing `F1` on any screen shows a scrollable, grouped shortcut popup that
never runs off the window edge, closable with `Esc` or `Q`; `F11`/
double-click fullscreen and `U` keep-unlocked (now also from the viewer) both
appear in the relevant popups; video tiles in the grid and viewer strip show
their stored poster frame instead of a blank box; going grid → viewer → back
preserves the List/Grid view and thumbnail-strip side within the session, and
leaving a video mid-playback and reopening it resumes paused at the same
position.

**Status:** ✅ Part 1 shipped. Part 2 (session-scoped memory) implemented,
pending owner review/merge — see the acceptance criterion's caveat about
manual interactive verification.
