## Phase 40 — Video codec/loop/sync polish, gallery position memory & view density 🔜

**Goal:** Three independent UX/quality gaps reported from live use, bundled
under one phase (like Phase 39's Part 1/Part 2) because they were requested
together, not because they share code: (1) `.webm`/`.mov` playback is still
missing some commonly-encountered codecs, and video has no loop option; (2)
navigating the gallery tree — descending into sub-galleries, backing out,
leaving to another screen and returning — never remembers where you were,
landing you back at the top of a gallery every time; (3) the grid/list toggle
is a binary choice when several thumbnail densities would serve different
window sizes better. Each part ships as its own PR against this phase.

---

## Part 1 — Broader video codec support, loop toggle, hardened A/V sync ✅

**Goal:** Extend the vendored FFmpeg decoder set to cover AV1 (`.webm`) and
more of the codecs still commonly found in `.mov` containers beyond the
Phase 28 set, add a user-toggleable loop for video playback, and verify the
existing A/V sync logic holds under the new codecs and the new loop-boundary
reseek.

### Tasks
- **AV1 decoder.** FFmpeg ships a native `av1` decoder — no `libaom`/`dav1d`
  link needed, the same "native decoder, no new vendored dependency" pattern
  Phase 38 used for `vp8`/`vp9` (the vendored `libaom` in `vendor/libaom` is a
  separate, unrelated integration — it decodes AVIF *stills* via libheif, not
  FFmpeg video). Add `av1` to `--enable-decoder` in both
  `scripts/build_codecs.sh`'s `build_ffmpeg()` and
  `scripts/build_ffmpeg_windows.sh` (the two lists are kept in sync per the
  Windows script's own header comment). Confirm during implementation whether
  an explicit `--enable-parser=av1` / `--enable-bsf` is required, or whether
  (as with Phase 38's `vp9_superframe_split`) FFmpeg's configure auto-selects
  the dependency — Phase 38's WebM out-of-scope note explicitly deferred
  "AV1-in-WebM" to a later phase; this is that phase.
- **`VideoCodec` enum** (`src/vault/index.h`) — add `AV1` after the last
  Phase 38 value (raw `u8` index field, no format bump, same pattern as every
  codec addition since Phase 28).
- **`VideoDecoder::open` mapping** (`src/media/video_decoder.cpp`) — map
  `AV_CODEC_ID_AV1` to the new enum value; `video_codec_name` gains the label.
- **Broaden `.mov` codec coverage.** Research FFmpeg's `mov.c` fourCC table
  for native decoders (no new vendored lib) still worth adding beyond the
  Phase 28 set (H.264/H.265/ProRes/DNxHD/MJPEG): candidates to evaluate —
  `qtrle` (QuickTime Animation, common in screen-recording/lossless `.mov`
  exports), `cinepak` (legacy but still found in older `.mov` archives), and
  confirming AV1-in-`.mov` (`av01` box, covered by the decoder added above)
  and VP9-in-`.mov` (decoder already exists since Phase 38 — likely only
  needs confirming the `mov` demuxer already routes the `vp09` box to it, no
  new code) actually work now. Only add a decoder whose fourCC the `mov`
  demuxer already maps by default — don't hand-roll new box-to-codec routing.
- Update `CLAUDE.md` (FFmpeg decoder list, `VideoCodec` table), the README
  stack line, `docs/VENDORED_DEPS.md`, `mem:tech_stack`.
- `tests/` — an AV1 `.webm` fixture (probe + poster + full decode through the
  encrypted-chunk path); one fixture per newly-added `.mov` codec; existing
  MP4/MOV/MKV/WebM fixtures stay byte-for-byte unchanged (no demuxer/container
  regression).

- **Loop toggle.** New `media::loop_setting.h/.cpp` (mirrors
  `media::volume_setting`'s function-local-static pattern exactly):
  `saved_loop_enabled()` / `set_saved_loop_enabled(bool)`, process-lifetime,
  not vault-persisted. `VideoPlayback` seeds its loop flag from it on
  construction. New keybinding: `R` (repeat) — `L` is already taken inside
  `VideoPlayback::handle_key` for the J/L seek-forward binding, and `R` is
  free in both `ImageViewer` and `VideoPlayback`'s key maps. At end-of-stream
  (`video_playback.cpp`'s `advance()`, the
  `if (eof_ && !pending_ && model_.playing())` branch): when loop is enabled,
  call the existing `do_seek(0.0)` and keep `playing()` true instead of
  pausing — this is the *exact* path already used by the "press Space at the
  end" replay (`key()`'s `SDLK_SPACE` + `model_.at_end()` branch), so no new
  seek/resync code is needed, only a branch to reach it from EOF
  automatically. Add a small on-screen loop-state indicator (same visual
  treatment as the existing mute icon) and a `help_groups()` entry.
- `tests/` — `loop_setting` get/set/default test; a headless test on the
  EOF-branch logic asserting the loop path re-seeks-and-continues instead of
  pausing (no SDL needed — this is data-flow, not rendering).

- **A/V sync hardening (verification, not redesign).** The sync mechanism
  already works (Phase 16's `media::av_sync::decide`, audio-clock-driven
  `advance()`, and `do_seek()` already flushes + re-aligns both tracks
  correctly). This task is to confirm it holds under the two new stresses
  this phase introduces, and fix only what's actually found broken:
  (a) the new AV1/`.mov` codecs' frame timestamps behave correctly under
  `av_sync::decide` — a B-frame-heavy fixture (PTS reordering) should still
  Present/Hold/Drop correctly; (b) the loop-boundary reseek doesn't glitch —
  assert the same "seek flushes both tracks and re-aligns" invariant Phase 16
  already tests for manual seeks also holds when triggered from the new
  automatic loop path; (c) if no existing test stresses multi-minute
  playback, add one synthetic long-duration drift regression test. Any real
  gap found gets fixed as a targeted change to `av_sync.cpp`/
  `video_playback.cpp`, not a new subsystem — same clock, same `decide()`.

**Out of scope (YAGNI):** encoding/transcoding; AV1 hardware acceleration;
professional/rare `.mov` codecs beyond the evaluated candidates unless a real
fixture proves the need (e.g. Apple ProRes RAW — proprietary, no open FFmpeg
decoder exists); a per-video loop-count limit (infinite loop only, toggle
on/off); loop behavior for images/slideshow (out of scope — this is the
video transport only).

---

## Part 2 — Session-scoped gallery position memory

**Goal:** Extend the session-scoped memory pattern Phase 39 Part 2 already
established (`GallerySessionState`) so that navigating the gallery tree —
descending into sub-galleries, backing out, leaving to another screen (
Favorites, Tags, Advanced Search, Vault Manager) and returning — always
restores the previously-selected tile at every level, for the rest of the
unlocked session.

### Current gap
`NavModel::enter()`/`up()` (`src/ui/nav_model.cpp`) unconditionally reset
`selected_` to `0`. `App` reconstructs a brand-new `GalleryGrid` (with a
fresh `NavModel`) on most navigations, seeded only with the single
`Nav{path, index}` the transition explicitly carries (e.g. the viewer handing
back its exact position to the gallery it was launched from — this one case
already works today). There is no memory of the index you were on in an
ancestor gallery once you've descended past it, and no memory of where you
were in a gallery you left for an entirely different screen and later
returned to.

Because a gallery is either all sub-galleries or all leaf media, never mixed
(the project's gallery-model invariant), "remember the sub-gallery I had
selected" (request 4) and "remember the image/video I had selected" (request
5) are the same mechanism applied at different tree depths — implemented as
one path-keyed position map, not two features.

### Tasks
- `GallerySessionState` (`src/ui/gallery_session_state.h`) gains
  `std::unordered_map<std::string, int> last_index_by_path;` (key =
  `NavModel::path()`'s `"a/b/c"` string, `""` for the root) plus two small
  pure methods — `void record(std::string_view path, int index)` and
  `int recall(std::string_view path) const` (returns `0` when absent) —
  unit-tested like the rest of the struct.
- `GalleryGrid` reports into `session_.last_index_by_path` (via the same
  App-owned-session capture mechanism Phase 39 Part 2 already uses for
  view/strip-side) at every point the current path's selection is about to
  go stale: before `nav_.enter()` (descending — records the parent's
  pre-descent index), before `nav_.up()` (ascending), and in `on_exit()`
  (covers "left to a different screen entirely" and the ordinary
  grid→viewer transition).
- `GalleryGrid`'s construction path consults `session_.recall(path)` to seed
  the initial selected index for a freshly-built grid at that path — but an
  explicit `Nav.index` supplied by a more specific transition (e.g. the
  viewer returning to its exact launch position) still takes precedence; the
  recalled value is a fallback default, never an override of fresher,
  more-specific state.
- Resets at the same points `GallerySessionState::reset()` already resets at
  (`LockActive`, idle auto-lock, vault switch) — no new reset points, no
  vault-format change (this is in-memory session state only, like the rest
  of `GallerySessionState`).
- Update `CLAUDE.md` (`GallerySessionState`'s new field) + `mem:core`.
- `tests/` — `record`/`recall` pure unit tests (empty-map default,
  overwrite-on-repeat-visit, independent entries per path); an App-level
  capture/restore test in the same style Phase 39 Part 2 used for its own
  App-side wiring (full suite + `--asan` green; note the same manual
  interactive-verification caveat Phase 39 Part 2 already documented, since
  this environment has no GUI-automation tool to drive real keyboard-driven
  round trips through `GalleryGrid`/`ImageViewer`).

**Out of scope (YAGNI):** persisting positions to the vault (session-only,
like the rest of `GallerySessionState`); remembering scroll position
independently of selected index (the existing scroll-to-selection logic
already re-derives scroll from the selected index on render); a position
history/breadcrumb beyond the current path's own remembered index.

---

## Part 3 — Gallery view density: List, Grid S/M/L/XL

**Goal:** Replace the binary `L`-key List/Grid toggle with a five-state
cycle — **List → Grid S → Grid M → Grid L → Grid XL → List** — so the
thumbnail density can be tuned to window size and preference instead of a
single fixed grid size.

### Design
`GalleryView` (`src/ui/gallery_view.h`) currently has two values (`Grid`,
`List`); the actual tile pixel size is a single file-local
`constexpr float CELL = 188` in `gallery_grid.cpp`, already threaded through
`grid_columns(avail_w, cell, gap)` as a parameter (`src/ui/widgets.h`/`.cpp`)
— the shared grid-layout helper needs no changes, only what value
`gallery_grid.cpp` passes it.

### Tasks
- `GalleryView` gains four grid variants replacing the single `Grid`:
  `GridS`, `GridM`, `GridL`, `GridXL` (`List` unchanged). `GridM`'s pixel size
  equals today's `CELL = 188` exactly, so existing sessions render identically
  until a user opts into a different density.
- A small pure helper — `float cell_size_for(GalleryView) noexcept` in
  `gallery_view.h`/`.cpp` — maps each grid variant to a tile size (starting
  values to tune against real window sizes during implementation, e.g.
  S=128, M=188, L=248, XL=320), replacing the `constexpr float CELL` at each
  of its ~10 call sites in `gallery_grid.cpp` with `cell_size_for(view_)`.
- `SDLK_L` handling (`gallery_grid.cpp`'s `handle_key`, currently
  `view_ = (view_ == Grid) ? List : Grid;`) becomes a pure
  `GalleryView next_gallery_view(GalleryView) noexcept` cycling function
  (List→GridS→GridM→GridL→GridXL→List), unit-tested directly instead of
  inlined in the switch statement.
- `help_groups()`'s `{"L", "Toggle list/grid view"}` entry (`gallery_grid.cpp`)
  updates its label for the 5-way cycle (e.g. `"Cycle view: list / grid
  size"`).
- `GallerySessionState::view` (`src/ui/gallery_session_state.h`) already
  stores a `GalleryView` — the type's value-set change is source-compatible,
  no migration needed; it keeps carrying whichever of the 5 values was last
  active across a grid↔viewer round trip, same mechanism as today.
- `tests/` — `cell_size_for` returns the 4 expected distinct grid sizes;
  `next_gallery_view` cycles correctly through all 5 states and wraps back to
  List.

**Out of scope (YAGNI):** applying density levels to Favorites/Tag/
Advanced-search result grids — `GalleryView` is not used outside
`GalleryGrid` today and none of those screens were part of the request; a
persisted (vault-level) density preference — stays session-scoped like the
existing choice; user-configurable/arbitrary pixel sizes beyond the 4 presets.

---

### Acceptance criterion
A VP9-class AV1 `.webm` (with audio) and the newly-covered `.mov` codec
fixtures import, probe, show a poster, and play with correct A/V sync;
pressing `R` in the viewer toggles looping, and a looping video replays from
the start indefinitely with audio/video staying in sync across the loop
boundary; descending through several nested sub-galleries, backing out level
by level, and returning to any of them lands on the tile that was selected
before descending — including after leaving to Favorites/Tags/Vault Manager
and coming back, for the rest of the unlocked session; pressing `L`
repeatedly in the gallery grid cycles List → Grid S → Grid M → Grid L →
Grid XL → List, each grid density rendering visibly larger/smaller
thumbnails with a correspondingly adjusted column count.

**Status:** 🔜 Part 1 ✅ shipped (this PR) — AV1 (`.webm`/`.mov`, via the
already-vendored libaom as FFmpeg's `libaom-av1` decoder — FFmpeg's own native
`av1` decoder turned out to be hwaccel-dispatch only, no software decode path,
contradicting this doc's original "no libaom/dav1d link needed" assumption)
plus QTRLE/Cinepak for `.mov`; loop toggle (`R`, `media::loop_setting`,
on-screen ring indicator); A/V sync hardening (presentation-order pts
invariant, loop-boundary reseek re-alignment, long-duration drift regression
test) — all verified with `scripts/test.sh` and `scripts/test.sh --asan`
green. Parts 2 and 3 not started. Planned as their own PRs against this same
phase, in that order (Part 2's session-memory extension and Part 3's
`GalleryView` value-set change both touch `GallerySessionState` and
`gallery_grid.cpp`, so land in that order to keep merge conflicts small).
