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

JPEG/PNG/GIF/BMP/TGA fixtures are generated at runtime in `fixtures.cpp` — they are
not stored here.
