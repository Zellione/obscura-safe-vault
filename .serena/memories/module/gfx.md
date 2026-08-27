# Module: gfx/ — SDL rendering primitives

Referenced from `mem:core`. Covers `src/gfx/`.

- `window.*`, `renderer.*`, `texture_cache.*`, `text.*` — SDL3 window/renderer, texture
  cache, stb_truetype text atlas.
- `theme.{h,cpp}` — UI colour tokens, runtime-selectable: a `Theme` value + 4 presets
  (Refined Slate default / Light / High Contrast / Midnight). `gfx::set_theme(id)` /
  `active_theme()` swap the active one; the `theme::X` tokens are references into it so every
  call site tracks a switch. `theme_slug`/`theme_from_slug`/`theme_name` are pure helpers.
  `THEME_COUNT` = 4.
  Modal veils and neutral media black/white are theme tokens too; UI code must not
  bypass the palette with literal `gfx::Color` values.
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

## Glyph coverage (Phase 83) — read before writing ANY user-visible string

`FontAtlas` is **UTF-8 aware but not universal**, and the two limits are different things:

1. **What the atlas bakes.** `bake()` packs ASCII 32–126 densely plus every codepoint in
   `text.h`'s `EXTRA_RANGES` (Latin-1, Latin Ext-A/B, Greek, Cyrillic, General Punctuation,
   Currency, U+2212) that the font's own cmap actually maps — filtered with
   `stbtt_FindGlyphIndex`, so naming an absent block is free. Uses
   `stbtt_PackBegin`/`PackFontRanges` (NOT `BakeFontBitmap`) at oversampling 1×1, which is
   what keeps `packedchar` metrics identical to the old `bakedchar` ones. ASCII stays a
   subtraction-indexed array; the rest is a codepoint-sorted vector, binary-searched.
   Atlas is 1024² at 28px.
2. **What the bundled font contains.** `assets/fonts/NotoSans-Regular.ttf` is a **2965-codepoint
   subset**: Latin/Greek/Cyrillic + common punctuation. It HAS `— – … · • − × ± § µ ‖`. It has
   **no arrows (↑↓←→), no ✓ ✗, no ★ ● ▲ ▼, no ≤ ≥ ⇒**. Those cannot be drawn no matter what
   the atlas does — UI strings spell them out in ASCII instead (`[Up/Dn]`, `Name asc`,
   `* favorite`, `^`/`v`, `->`), each with a comment at the call site.
   `tests/gfx/test_text.cpp::font_atlas_renders_ui_typography` is the guard: add a new
   typographic character there and it fails if the font cannot draw it.

Unmapped scalars and malformed UTF-8 render `REPLACEMENT_GLYPH` (`?`) — one per scalar, not
per byte. The decoder treats node names as untrusted: every failure path consumes exactly one
byte (cannot hang), and overlongs/surrogates are rejected so `C0 AF` cannot alias `/`.

`text_top_for_center` measures **ASCII ink only, deliberately** — folding in accented capitals
and cedillas would shift the vertical centring of every screen whenever coverage grows.
- `YuvTexture` (streaming I420/NV12 video texture) + `Renderer::draw_triangle` (play badge /
  play-pause icon).
