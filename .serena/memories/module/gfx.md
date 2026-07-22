# Module: gfx/ — SDL rendering primitives

Referenced from `mem:core`. Covers `src/gfx/`.

- `window.*`, `renderer.*`, `texture_cache.*`, `text.*` — SDL3 window/renderer, texture
  cache, stb_truetype text atlas.
- `theme.{h,cpp}` — UI colour tokens, runtime-selectable: a `Theme` value + 4 presets
  (Refined Slate default / Light / High Contrast / Midnight). `gfx::set_theme(id)` /
  `active_theme()` swap the active one; the `theme::X` tokens are references into it so every
  call site tracks a switch. `theme_slug`/`theme_from_slug`/`theme_name` are pure helpers.
  `THEME_COUNT` = 4.
  Phase 49 tag palette: `TAG_SWATCH_COUNT` = 16, `gfx::tag_swatch(i)` and
  `gfx::tag_swatch_name(i)` (out-of-range indices are handled, not UB). Because a chip paints
  the tag TEXT, each swatch carries an **on-dark and an on-light RGB** and `tag_swatch`
  returns a `Color` **BY VALUE**, picking between them from the active theme's background
  luminance (`bg_luma() > 0.5`) — so one table stays legible on all four themes with no
  per-theme entries. Binding the result to a reference is a defect. A legibility test asserts
  a 0.20 contrast floor across all THEME_COUNT × TAG_SWATCH_COUNT = 64 combinations.
  **`gfx` deliberately does NOT depend on `vault/`** — `vault::TAG_SWATCH_COUNT` is a separate
  constant, and the `static_assert` tying the two lives in `ui/tag_chip.cpp`, the first TU
  where both headers are legitimately visible.

## Rendering details
- `RADIUS` consts are compile-time; renderer has `draw_round_rect` / `draw_selection_glow`
  (`round_rect_outline` is pure/tested).
- `Window::width()`/`height()` are LIVE (`SDL_GetCurrentRenderOutputSize`, px) so layout
  reflows on resize.
- Font baked at 28px; `draw_text` y = top, baseline = y+px; use
  `FontAtlas::text_top_for_center` to vertically centre text in a box. `draw_text` batches a
  run into ONE `SDL_RenderGeometry` call (per-vertex colour) via `build_text_geometry`
  (pure/tested); `draw_round_rect` reuses scratch buffers + a cached arc table.
- `YuvTexture` (streaming I420/NV12 video texture) + `Renderer::draw_triangle` (play badge /
  play-pause icon).
