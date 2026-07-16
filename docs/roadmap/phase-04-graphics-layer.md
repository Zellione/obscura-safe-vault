## Phase 4 — Graphics layer ✅

**Goal:** Implement the GPU texture cache and text atlas needed by the UI.

### Tasks
- [x] Download and commit an OFL-licensed TrueType font to `assets/fonts/`.
- [x] `src/gfx/texture_cache.{h,cpp}` — upload `ImageData` to `SDL_Texture`; LRU eviction by GPU memory budget.
- [x] `src/gfx/text.{h,cpp}` — bake a glyph atlas from the bundled font using `stb_truetype`; `draw_text(renderer, x, y, text, colour)`.
- [x] `src/gfx/renderer.{h,cpp}` — expand stub: `draw_image`, `draw_rect`, `draw_text`, `draw_thumbnail_strip`.
- [x] `tests/gfx/` — headless smoke tests:
  - [x] Font atlas bakes without crash for all printable ASCII.
  - [x] Texture upload for a 1×1 pixel RGBA image succeeds.

### Acceptance criterion
App opens, clears, and can draw a text label and a coloured rectangle. Font atlas is visible.

**Status:** ✅ 10 new gfx tests (font bake/measure/coverage/garbage-reject + draw, texture-cache
upload/LRU-eviction/MRU-touch/clear, thumbnail-strip layout) — 88/88 total pass under
`scripts/test.sh` and ASAN+UBSan+LSan. The gfx units are tested headlessly against an SDL
software renderer (`SDL_CreateSoftwareRenderer`), so they need no display in CI. `App` now bakes
the font atlas on init and draws a coloured rectangle + text label each frame.

> **Notes / decisions made during implementation**
> - **Bundled font:** the environment had no network access, so the bundled OFL/permissive font is
>   **Noto Sans Regular** (`assets/fonts/NotoSans-Regular.ttf`, license in `NotoSans-LICENSE.txt`)
>   rather than Inter. Swappable via the `OSV_DEFAULT_FONT` compile define.
> - `FontAtlas::bake()` is pure CPU (8-bit alpha coverage bitmap + per-glyph metrics) and grows a
>   square atlas 256→2048 until all printable ASCII (32–126) fits; the `SDL_Texture` is created
>   lazily on the first `draw_text()`. `measure()` rounds each glyph advance independently so it is
>   exactly additive.
> - `TextureCache` keys on a caller-supplied `uint64_t`, accounts each entry as `w*h*4` GPU bytes,
>   and evicts least-recently-used entries past the budget (default 256 MiB).
> - premake: SDL3 linkage was factored into a shared `link_sdl3()` helper; `osv_tests` now compiles
>   the headless-testable gfx units (`texture_cache`, `text`, `renderer`) and links SDL3.
