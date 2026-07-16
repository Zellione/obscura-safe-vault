## Phase 9 — Extra image formats ✅

**Goal:** Add WebP and HEIC/AVIF support.

### Tasks
- [x] **WebP** — add `vendor/libwebp` submodule; cmake-build it into the codec prefix; add `decode_webp_from_memory`.
- [x] **HEIC/AVIF** — add `vendor/libde265`, `vendor/libaom`, `vendor/libheif` submodules; update build; add `decode_heif_from_memory` (one libheif entry point decodes both HEIC and AVIF).
- [x] Add `src/image/format_registry.{h,cpp}` to detect and dispatch by container magic (WebP RIFF, ISO-BMFF `ftyp` brands).
- [x] `tests/image/` — WebP/HEIC/AVIF decode tests with committed fixtures, malformed-buffer rejection, and a vault round-trip import/read-back.

### Acceptance criterion
WebP and HEIC/AVIF images can be imported and displayed. All existing tests still pass.

**Status:** ✅ Decode-only support via vendored-from-source static libs. `decode_from_memory`
dispatches on `format_registry::detect_format`: stb_image (JPEG/PNG/GIF/BMP/TGA/HDR),
libwebp (WebP), libheif (HEIC via libde265, AVIF via libaom). 154/154 tests pass under
`scripts/test.sh` and `--asan`; the decoders run under ASAN/UBSan on untrusted input in CI.

> **Notes / decisions made during implementation**
> - **Vendoring:** all four codecs (libwebp, libde265, libaom, libheif) are git submodules,
>   cmake-built static and `cmake --install`ed into one staging prefix
>   `vendor/codecs-prefix/` (gitignored). `scripts/build_codecs.{sh,bat}` does the build and
>   is shared by `setup.{sh,bat}` and CI; `premake5.lua`'s `link_image_codecs()` links the
>   set in dependency order (`heif → de265 → aom → webp → sharpyuv`).
> - **AVIF backend = libaom** (decoder-only, `-DCONFIG_AV1_ENCODER=0`), cmake-built like the
>   rest — no meson/dav1d toolchain added. **libaom bumped 3.9.1 → 3.14.1** because 3.9.1's
>   `test_nasm` probe rejects nasm 3.x. libaom needs **nasm** on PATH (added to CI + the
>   README prerequisites for every OS).
> - **CMake 4.x:** libde265 1.0.15 declares a pre-3.5 `cmake_minimum_required`, so the codec
>   build passes `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`.
> - **No new on-disk plaintext:** decoded pixels stay in the same transient `ImageData`
>   buffers as the stb path; the security invariants are unchanged.
> - **ASAN job:** now builds the codecs too (running the C decoders under the sanitisers is
>   the highest-value place to have them). The old "no SDL3 in the ASAN job" note was stale —
>   the ASAN leg has provisioned SDL3 since Phase 4 — and was removed from CLAUDE.md.

---

## UI Overhaul — modern restyle, strip toggle & fill-scroll ✅

**Goal:** Modernise the look of all screens and extend the image viewer. Shipped on
the `feat/ui-overhaul` branch (a feature, not a numbered phase).

### Tasks
- [x] `src/gfx/theme.h` — centralised "Refined Slate" colour tokens; replaced the
  scattered inline colour literals across every screen and widget.
- [x] `gfx::Renderer::draw_round_rect` + `draw_selection_glow` (via `SDL_RenderGeometry`)
  and a pure, unit-tested `round_rect_outline` helper. Tiles, fields, buttons and
  panels are now rounded; selection uses an accent glow.
- [x] `draw_button` finally renders its hover/active states; `ui::button_state`
  (pure, tested) wires live mouse hover/press on the unlock screen.
- [x] Image-viewer thumbnails halved (`src/ui/strip_layout.{h,cpp}`,
  `strip_thumb_size`).
- [x] **Strip-position toggle** (`T`): bottom (horizontal) ↔ left (vertical),
  orientation-aware layout/scroll/hit-testing; persists for the session.
- [x] **Fill-width continuous scroll** view mode (`F`): images scaled to viewport
  width, wheel scrolls vertically across the whole leaf gallery, active thumbnail
  tracks the viewport centre. Pure `src/ui/scroll_model.{h,cpp}` drives the maths;
  only the on-screen images + immediate neighbours are decoded (bounded texture
  set, decrypted into locked memory, wiped after GPU upload — never to disk).
- [x] **Gallery responsiveness & detailed list view**: `Window::width()/height()`
  now report the live renderer output size, so the grid reflows on resize and is
  centred; thumbnails are aspect-fit on black (no stretch); filenames are
  middle-elided to fit (`ui::elide_middle`). A `L` key toggles a detailed list
  view with very small thumbnails + columns (name, dimensions, size, type, date)
  formatted by pure `src/ui/meta_format.{h,cpp}`.
- [x] **Text centring**: `FontAtlas::text_top_for_center` centres text by real
  glyph extents (the font bakes at 28px with baseline = y + px), used by buttons,
  fields, list rows and headers.

### Acceptance criterion
All screens use the shared theme; the viewer supports half-size thumbnails, the
bottom/left strip toggle, and the fill-width scroll mode. All tests pass.

**Status:** ✅ Pure logic (strip layout, scroll model, button state, rounded-rect
tessellation) is TDD-covered; 174/174 tests pass under `scripts/test.sh` and `--asan`.
Security invariants unchanged — the fill-scroll texture window reuses the existing
`SecureBytes` decrypt-then-wipe path.

> **Notes / decisions made during implementation**
> - **Strip side & view mode** reset to defaults (`Bottom`, `Fit`) when the viewer
>   is re-entered from the gallery; they persist only while the viewer is open.
> - **Bottom strip** keeps its full bar height; the smaller thumbnails just sit
>   centred in it.
> - **Scroll heights** come from `ImageMeta.width/height`, so the scroll model is
>   built without decrypting any image up front.
