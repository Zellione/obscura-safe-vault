# Image test fixtures

Tiny (8×8 solid `#3366cc`) committed binaries for formats that the test harness
cannot synthesise in-memory (stb_image_write has no WebP/HEIC/AVIF encoder, and we
build those codecs decode-only). Regenerate with:

```bash
magick -size 8x8 xc:'#3366cc' sample.webp                       # libwebp / VP8
magick -size 8x8 xc:'#3366cc' src.png
heif-enc -o sample.heic src.png                                 # HEIC (HEVC)
avifenc  src.png sample.avif                                    # AVIF (AV1)
rm src.png
```

Animated WebP fixtures (Phase 57), encoded **lossless** so per-pixel assertions
are exact:

```bash
magick -delay 10 -loop 0 \( -size 8x8 xc:'#3366cc' \) \( -size 8x8 xc:'#cc6633' \) \
       \( -size 8x8 xc:'#33cc66' \) -define webp:lossless=true sample_anim.webp
magick -delay 10 -loop 0 \( -size 8x8 xc:none \) \( -size 8x8 xc:'#cc3333' \) \
       -define webp:lossless=true sample_anim_alpha.webp
```

`sample_anim.webp` is 3 opaque frames 100 ms apart. `sample_anim_alpha.webp` is 2
frames whose first is fully transparent — it pins the "flatten over black" rule
(a transparent pixel must decode to opaque black, never to stale bytes).

JPEG/PNG/GIF/BMP/TGA fixtures are generated at runtime in `fixtures.cpp` — they are
not stored here.

## Phase 95 grid + overlay regression fixtures (libheif 1.23.2, OSV-AUD-002)

`sample_grid.avif` and `sample_overlay.heic` are committed binaries generated with
**system** tooling (ImageMagick + `avifenc` + libheif's own overlay encoder API) —
our vendored libheif is decode-only and cannot re-create them. They exercise the
two decode paths that carried advisories fixed by the 1.23.2 upgrade: GHSA-2vh6
(uninitialized grid-image pixels) and GHSA-hg7q (out-of-bounds reads during
overlay compositing). Regenerate with:

```bash
# 2x2 grid AVIF, 128x128 (4x 64x64 cells): TL red, TR green, BL blue, BR white.
magick -size 64x64 xc:red  -size 64x64 xc:green +append - /tmp/grid_top.png
magick -size 64x64 xc:blue -size 64x64 xc:white +append - /tmp/grid_bottom.png
magick /tmp/grid_top.png /tmp/grid_bottom.png -append /tmp/grid_128.png
avifenc -g 2x2 --lossless --chroma 444 /tmp/grid_128.png sample_grid.avif

# Overlay HEIC, 64x48 canvas: bottom 40x24 #3366cc at (0,0), top 16x16 #cc6633 at
# (8,8), opaque black background where neither child covers. Built via the system
# libheif encoder API (heif_context_add_overlay_image; the throwaway generator
# used during Phase 95 is described in the phase doc).
```

Colour assertions are intentionally tolerant: lossless HEVC/AV1 still round-trips
through YUV, so channel bits can shift by a couple of LSBs (the AV1 grid's green
chroma sub-samples to 0x80).
