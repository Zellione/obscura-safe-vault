## Phase 56 — UI layout, import status & navigation polish ✅

**Goal:** Fix five owner-reported UI defects plus one HiDPI defect found while
tracing them. Popups stop cutting their bottom line, the Import Status page shows
a readable two-line row that keeps source → target on finished items and keeps
focus on a reordered item, the viewer stops resetting zoom/scroll/playback every
time a background import writes the vault, and the right mouse button becomes a
universal "back / up one level". No `.osv` format change, no `INDEX_VERSION`
bump, no vault- or crypto-layer change.

Full design, including recorded tradeoffs:
[`docs/superpowers/specs/2026-07-28-phase-56-ui-layout-import-status-navigation-design.md`](../superpowers/specs/2026-07-28-phase-56-ui-layout-import-status-navigation-design.md).

### Root causes (traced before planning)

| Defect | Root cause |
|---|---|
| Popup text cut by the popup's bottom edge when scrolled fully down (Windows) | The font is baked at **28 px** (`app.cpp:75`) but surfaces hardcode a smaller text pitch — `help_popup.cpp:65,107,164` use `LINE_H = 24`, `detail_panel.cpp:17` `ROW_H = 24`, `tag_overview.cpp:41` `PROMPT_LINE_H = 20`, `advanced_search_screen.cpp:29` `ROW = LINE * 0.85` = 25.5. The help popup then clips its content band to a whole multiple of that too-small pitch, so the bottom line's glyph box is cut. |
| Import Status: bar overlaps the text | `draw_running_row` centres the 12 px bar and the row text on the *same* y band (`import_status_screen.cpp:51,54`). |
| Import Status: finished rows lose source/target | `format_row_text` returns only an outcome string for `Done`/`Failed`/`Cancelled`, dropping `display_name` and `dest_gallery` (`import_status_screen.cpp:35-40`). |
| Import Status: focus does not follow a reordered item | `sel_` is a positional index and `reorder_selected` never adjusts it (`import_status_screen.cpp:143-147`). |
| Viewer resets on a vault write | `App::run` calls `screen_->on_vault_changed()` on every drain that applied records (`app.cpp:630-632`); `ImageViewer::on_vault_changed` unconditionally ends in `show_image_at(index_)`, which sets `fit_.fitted = false`, re-runs `scroll_to_image`, and does `video_.reset()` + rebuild (`image_viewer.cpp:95-112`, `:244-261`). |
| Right mouse button does nothing | `ImageViewer::handle_mouse_down` returns early for any non-left button (`image_viewer.cpp:515`); `GalleryGrid`'s mouse branch ignores button identity (`gallery_grid.cpp:990`). |
| **(found during tracing)** Mouse hit-tests are off at Windows display scaling | `gfx::Window::width()/height()` return render-output **pixels** and all layout is in that space, but SDL delivers mouse events in window **points**. With `SDL_WINDOW_HIGH_PIXEL_DENSITY` (`window.cpp:14`) and 125%/150% scaling every hit-test — tile clicks, edge-click nav, video seek bar, footer band — is off by the scale factor. |

### Tasks

**1. One font-derived text pitch — `src/ui/text_metrics.{h,cpp}` (new)**
- [x] Pure module, no SDL and no `FontAtlas`: `line_pitch(font_px)` → `ceil(font_px * 1.25)` and `row_height(font_px, pad)` → `line_pitch + 2 * pad`. The 1.25 leading guarantees `pitch > font_px`, so adjacent lines cannot touch and a whole-line clip band cannot cut a descender. At the app's 28 px font the pitch is 35 px.
- [x] Migrate every hardcoded text-line pitch to the helper: `help_popup.cpp` (`LINE_H` ×3), `detail_panel.cpp` (`ROW_H`), `tag_overview.cpp` (`PROMPT_LINE_H`), `advanced_search_screen.cpp` (`LINE` **and** `ROW`), `saved_search_panel.cpp` (`LINE`), `search_result_view.cpp` (`LINE`), `search_overlay.cpp` (`LINE`, `RESULT_ROW_H`), `tag_editor.cpp` (`LINE`, `TAG_ROW_H`).
- [x] **Row heights are not text pitches:** `gallery_grid.cpp`'s `ROW_H = 44` is a thumbnail row and keeps its value; a test asserts it stays above the pitch so it cannot silently become a clipper.
- [x] Help popup re-derives chrome and content band from the pitch so `content_top + (visible_lines - 1) * pitch + font_px <= content_top + band_h` holds by construction. Whole-line scroll (`clamp_help_line`) is already correct and stays.
- [x] Re-check each migrated panel's height/row-cap maths: a grown pitch fits fewer rows in a fixed-height panel (help 24→35, detail panel 24→35, tag overview 20→35, advanced search 30→35, saved-search 30→35, search results 30→35, search overlay 34→35, tag editor 34→35). Where a panel has a fixed height it must scroll, never truncate.
- [x] Tests: `line_pitch(px) > px` across a font-size sweep; every migrated surface's pitch ≥ font height; `gallery_grid`'s `ROW_H` still exceeds the pitch; help-popup band invariant at several window heights, **including scrolled fully to the bottom**.

**2. Import Status two-line rows — `src/ui/import_status_row.{h,cpp}` (new)**
- [x] Pure formatting/geometry, testable without SDL: `format_task_route(task)` → `"name → dest"` (`root` when the destination is empty), `format_task_status(task)` → the state line, `import_row_height(font_px, pad)` → `2 * pitch + 2 * pad`.
- [x] Every row, in every state, renders two lines. **Line 1 — route:** `display_name → dest_gallery`, elided with the existing `fit_text`, present for `Queued`, `Running`, `Done`, `Failed` and `Cancelled` (this is the "source and target still visible on completed items" fix). **Line 2 — status:** the progress bar plus its `done/total` text for `Running`, otherwise the outcome string in its state colour (`✓ N imported, M skipped` / `✗ <error>` / `− Cancelled` / `Queued #id`).
- [x] The bar occupies line 2's band only, so bar and text never share a y range — the overlap is structurally gone, not merely nudged apart.
- [x] Tests: all five states formatted; empty destination → `root`; elision; `import_row_height` = `2*pitch + 2*pad`; bar band and text band do not intersect.

**3. Import Status focus rides the item — `src/ui/import_status_screen.cpp`**
- [x] Selection becomes **id-based**: store the selected task's `id`, resolve to a row index each frame via a pure `index_of_task(rows, id)`.
- [x] `reorder_selected` keeps the id, so focus follows the moved row and `Ctrl+Up`/`Ctrl+Down` can be repeated on the same item. `move_selection` moves by index, then records the new row's id.
- [x] When the selected id vanishes (cleared by `C`, or cancelled away), fall back to the clamped nearest index — today's behaviour.
- [x] Also fixes an unreported symptom of the same cause: the selection jumping when a running import completes and the snapshot reorders.
- [x] Tests: reorder keeps the focused id; repeated reorder walks the item; completion-driven snapshot reorder keeps focus; vanished id falls back to a clamped index.

**4. Viewer survives a vault write — `src/ui/image_viewer.cpp`**
- [x] `on_vault_changed()`: remember the current item's **path** (`album_.paths[index_]`), re-list `album_.images`/`album_.paths` as today (the tree reallocated; old pointers are stale), then look the remembered path up in the new list.
- [x] **Found** → assign `index_` directly and change *nothing else*: no `fit_.fitted = false`, no `scroll_to_image`, no `video_.reset()`, no GIF rebuild. Zoom, pan, fill-scroll offset, video position and GIF frame all survive. **Not found** (deleted or moved out) → today's `show_image_at(clamped index)` path.
- [x] Collection-backed albums (`album_.from_collection`) keep their current behaviour.
- [x] Keeping the decoder alive across a tree reallocation is safe: `media::VideoSource::open` copies the node's chunk table into its own `std::vector<VideoChunk>` and never retains the `IndexNode&` (`video_source.h:35-52`), the constructor is the only place the node is touched (`video_playback.cpp:101-107`), and `FullTexCache` is keyed by a `uint64_t` offset, not a node pointer.
- [x] Tests: zoom/pan preserved across `on_vault_changed` when the item survives; fill-scroll offset preserved; video position preserved and the decoder not rebuilt; index follows the item when a node is inserted before it; deleted item still falls back to `show_image_at`.

**5. Right-click = Esc — `src/app/app.cpp`**
- [x] `App::dispatch_event` translates a right button-down into a synthetic Escape key-down and dispatches that in its place; the matching button-up is swallowed so no screen sees a dangling release.
- [x] Pure helpers keep it testable without a window: `is_back_click(const SDL_Event&)` and `make_back_key_event()` (yields `SDLK_ESCAPE` + `SDL_SCANCODE_ESCAPE`, `mod == 0`, `repeat == false`).
- [x] One funnel means every modal, dialog and overlay inherits the behaviour with no per-surface code, and the viewer mirrors Esc exactly: in borderless fullscreen the first right-click leaves fullscreen and the second returns to the gallery.
- [x] **Explicit consequences of "exactly Esc"**, accepted by the owner: in the grid, right-click clears an active multi-selection first and only ascends on the next click (`gallery_grid.cpp:835-837`); at the root gallery it exits to the vault manager (`gallery_grid.cpp:300`); while a background file op owns the vault it hits that job's Esc→cancel gate; in a text field (rename / new gallery / password prompt) it cancels the field.
- [x] Help popup's global group gains `Right-click — Back / up one level`.
- [x] Tests: `is_back_click` accepts only right button-down; `make_back_key_event` field values; right button-up produces no dispatch.

**6. HiDPI mouse coordinates — `src/app/app.cpp`, `src/gfx/window.cpp`**
- [x] `App::dispatch_event` runs `SDL_ConvertEventToRenderCoordinates(renderer, &e)` on its event copy before anything else, converting button, motion and wheel positions from window points into the render-pixel space the layout uses.
- [x] `gfx::Window::mouse_x()/mouse_y()` route their `SDL_GetMouseState` result through `SDL_RenderCoordinatesFromWindow` (used by grid tile hover and the viewer's strip hover).
- [x] Both APIs are present in the vendored SDL3 (`vendor/SDL3/include/SDL3/SDL_render.h:1650,1716`). At pixel density 1.0 — the Linux dev box — the conversion is the identity, so nothing changes there.
- [x] Tests: conversion is the identity at density 1.0 and scales at 1.5 (pure helper around the SDL call, so the maths is asserted without a window).

**Cross-cutting**
- [x] Update `ROADMAP.md` index row, adding Phase 56 in numeric sequence.
- [x] `scripts/gen.sh` after adding `text_metrics.cpp` and `import_status_row.cpp` so `compile_commands.json` stays accurate.
- [x] Update Serena memories: `mem:module/ui` (new modules `text_metrics`, `import_status_row`); `mem:ui_spec` (right-click = Esc, two-line import-status row, font-derived pitch rule); `mem:conventions` ("never hardcode a text-line pitch; derive it from `font.pixel_height()` via `ui::line_pitch`"). `mem:vault_format` **explicitly NOT changed** — no `INDEX_VERSION` bump.

### Acceptance criterion

The F1 help popup scrolled fully to the bottom shows its last line uncut, and no
migrated panel overlaps or clips its text. The Import Status page shows every task
as two lines — `source → target` above, progress bar or outcome below — with the
bar never overlapping text, and `Ctrl+Up`/`Ctrl+Down` keeps focus on the moved
item so the chord can be repeated. Scrolling a full-screen image or playing a
video while a background import commits leaves zoom, pan, scroll offset and
playback position untouched. Right-click goes up one gallery from the grid,
returns to the gallery from the viewer (leaving fullscreen first, exactly like
Esc), and cancels any open modal. All tests pass under `scripts/test.sh` and
`--asan`.

**Owner-verified gate.** The popup clipping and the HiDPI hit-testing reproduce
only on Windows at a display scale above 100%; CI cannot close them. The phase is
not done until the owner confirms on Windows that (a) the F1 popup scrolled fully
to the bottom shows its last line uncut, and (b) tile clicks, edge-click
navigation and the video seek bar land where the cursor is.

### Deviations from the plan (decided during delivery)

**Layout extractions instead of constant swaps (Tasks 3–5).** The initial plan for Tasks 3–5 swapped hardcoded constants and added a rule-enforcement test that re-asserted Task 1's invariant. The owner chose instead to EXTRACT each surface's layout maths into a pure module, so the migration itself becomes unit-tested. This resulted in three new modules:

- `src/ui/detail_layout.{h,cpp}` — detail-panel line layout (Task 3)
- `src/ui/prompt_layout.{h,cpp}` — centred prompt/summary box layout (Task 4)
- `src/ui/list_layout.{h,cpp}` — shared vertical-list geometry for five surfaces (Task 5)

Each extraction is pure (SDL-free), fully tested, and immobile — all screens that use the layout route through the extracted module. This makes the migration verifiable beyond a simple constant swap.

**Tag editor's footer found drawn outside the modal.** During implementation, the tag editor's footer hint and error line were found being drawn partly *outside* the modal's bounds. These were moved onto pitch-based offsets measured from the modal bottom, fixing the issue.

### Out of scope

- The other `on_vault_changed()` implementations (gallery grid scroll, favorites,
  advanced search) are **not** audited here — the owner chose the viewer-only
  fix. If the grid turns out to lose scroll position on import, it is a follow-up.
- No font-size change: the pitch adapts to the font, not the reverse.
- No `.osv` format, `INDEX_VERSION`, or vault-layer change.
