# Animated GIF/WebP render: wrong colours & squashed aspect (Phase 81)

**Status:** ✅ shipped
**Date:** 2026-08-12

## Problem

Owner report: GIFs "look like their colors and resolutions look wrong compared
to the generated thumbnail."

The comparison is a real side-by-side. Hovering an animated tile swaps the
static thumbnail for live playback in the same rectangle — gallery grid tiles
(`gallery_grid.cpp`), gallery list rows, and the viewer's thumbnail strip
(`image_viewer.cpp`) — so the two renderings sit in exactly the same pixels a
moment apart. The static thumbnail was right; the animation was wrong on both
counts.

Two independent bugs, both in the *draw* half of `ui::AnimPlayback`. Neither is
in a decoder: the frames arrive correct and are then corrupted on the way to the
screen.

## Root cause 1 — packed vs. byte-order pixel format

`AnimPlayback::render` created its streaming texture as
`SDL_PIXELFORMAT_RGBA8888`. Both backends produce frames whose bytes are
**R,G,B,A in memory order**:

- `media::GifDecoder` `sws_scale`s to `AV_PIX_FMT_RGBA` (`gif_decoder.cpp`),
- `media::WebpAnimDecoder` decodes with `MODE_RGBA` and flattens alpha to
  `0xFF` (`webp_anim_decoder.cpp`).

`SDL_PIXELFORMAT_RGBA8888` is a **packed 32-bit** format: the word is
`R<<24 | G<<16 | B<<8 | A`, which on a little-endian CPU is `A,B,G,R` in memory.
SDL's own byte-order alias for R,G,B,A is `SDL_PIXELFORMAT_RGBA32`, documented
in `SDL_pixels.h` as "an alias for ABGR8888 on little-endian CPUs like x86". The
codebase already had this right one file over — `gfx/text.cpp` uploads its
identically-laid-out glyph atlas as `SDL_PIXELFORMAT_RGBA32`, with a comment
saying exactly why.

The resulting mapping on x86: **displayed R = source A** (255 for every opaque
frame, so red was pinned wide open), **G = source B**, **B = source G**, and
**A = source R**. Since SDL's default blend mode for that texture is
`SDL_BLENDMODE_BLEND` (verified by probe), the misread alpha also took effect:
low-red regions faded into whatever was behind them.

Measured against the vendored SDL3 with a software renderer:

```
SDL_PIXELFORMAT_RGBA32 == ABGR8888 (little-endian)
RGBA8888 (old)  src bytes R,G,B,A = 10,60,200,255  ->  displayed R=255 G=200 B= 60 A=255
RGBA32   (new)  src bytes R,G,B,A = 10,60,200,255  ->  displayed R= 10 G= 60 B=200 A=255
```

A deep blue rendered as bright orange — "the colors look wrong".

## Root cause 2 — fill instead of aspect-fit

Every static thumbnail path fits its image into the cell and letterboxes the
remainder over a black backing:

- `ui::draw_tile_thumb` → `fit_rect(tw, th, box)` (`tile_thumb.cpp`),
- `gfx::Renderer::draw_thumbnail_strip` → `min(dst.w/tw, dst.h/th)`, centred
  (`renderer.cpp`).

`AnimPlayback::render` passed its destination rect straight to
`Renderer::draw_image` → `SDL_RenderTexture`, which **stretches to fill**. The
three hover call sites hand it the whole *square* cell (`strip_cell_rect` returns
`thumb × thumb`; the grid passes the tile's thumb box), so every non-square
animation was squashed to the cell's shape the instant the pointer touched it —
"the resolution looks wrong", right next to a correctly-proportioned thumbnail.

The two viewer paths (`render_fit`, `render_scroll`) were never affected: they
build `dest` from the image's own dimensions, so it already carries the right
aspect ratio.

## Why 2000+ tests never caught it

No test had ever called `AnimPlayback::render`. `test_anim_playback.cpp` covered
construction, pause, frame advance and looping; `test_gif_decoder.cpp` and
`test_webp_anim_decoder.cpp` covered dimensions, frame counts, delays and (for
WebP) the decoded channel order. The texture format and the destination rect —
everything between a correct frame and the screen — were untested, and both bugs
have been shipped since the features landed (Phase 47 for GIF, Phase 57 for
WebP).

## Fix

`src/ui/anim_playback.cpp`, in `render()`:

1. `SDL_PIXELFORMAT_RGBA8888` → `SDL_PIXELFORMAT_RGBA32`.
2. `SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_NONE)` — frames are opaque by
   construction, so the draw no longer depends on what alpha a decoder emits.
3. Aspect-fit through the existing `ui::fit_rect` before drawing, with a black
   backing painted **only when a band actually exists** (`img.w < dest.w - 0.5f
   || img.h < dest.h - 0.5f`). That reproduces the static thumbnail's black
   letterbox on the hover paths while staying a strict no-op in the viewer, where
   the frame fills `dest` — so no black seam can appear around a zoomed image.

No call site changed: fitting inside `render()` fixes all three hover surfaces at
once and leaves the two viewer surfaces byte-identical.

## Tests

- `gif_decoder_frames_are_byte_order_rgba` — pins the GIF backend's R,G,B,A
  memory order (the contract the texture format has to match), so a future
  pixel-format change in the decoder cannot silently desynchronise from the
  upload. The WebP equivalent already existed.
- `anim_playback_renders_frames_in_true_colour` and
  `anim_playback_gif_renders_frames_in_true_colour` — render a frame 1:1 into a
  headless software renderer and read the pixel back. Both fixtures are lossless
  with known palette colours, so the assertions are exact. These fail on the old
  code with the exact channel transposition described above.
- `anim_playback_render_letterboxes_inside_a_wider_box` — an 8×8 animation into a
  2:1 box must occupy the centred 16×16 square with black bands, not stretch.
- `anim_playback_render_fills_an_aspect_correct_box` — the same animation into an
  aspect-correct box must reach all four corners (guards the viewer no-op, and
  would catch a fit that shrank the image or left a seam).

A reusable `SoftRenderer` helper with pixel readback now lives in
`tests/ui/test_anim_playback.cpp`; the existing `tests/gfx/test_renderer.cpp`
software-renderer tests are draw smoke tests only and never inspected output.

No `.osv` format change; `INDEX_VERSION` stays 12.

## Follow-ups

None. Vaults need no repair — nothing wrong was ever stored, only drawn.
