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
