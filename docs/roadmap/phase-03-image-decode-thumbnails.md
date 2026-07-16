## Phase 3 — Image decode & thumbnails ✅

**Goal:** Decode images from decrypted memory buffers and generate encrypted thumbnails.

### Tasks
- [x] `src/image/image.{h,cpp}` — `ImageData{pixels, width, height, channels, format}`; owns heap pixel buffer.
- [x] `src/image/decode.{h,cpp}` — `decode_from_memory(std::span<const uint8_t> buf) -> ImageData` via `stb_image`. Detect format from buffer magic bytes.
- [x] `src/image/thumbnail.{h,cpp}` — `make_thumbnail(const ImageData&, int max_side) -> ImageData` — nearest/bilinear downscale using `stb_image_resize2`.
- [x] Wire thumbnail generation into `Vault::add_image`: decode → downscale (e.g., max 256 px) → re-encode to JPEG → encrypt → store as the image's thumb chunk.
- [x] `tests/image/`:
  - [x] Decode JPEG, PNG, BMP, GIF (static frame), TGA from memory buffers (ship small test fixtures).
  - [x] Thumbnail size is ≤ max_side in both dimensions.
  - [x] Decode of a malformed buffer returns an error, not a crash.
  - [x] Round-trip via vault: add image → read thumb chunk → decode thumb → verify dimensions.

### Acceptance criterion
All image tests pass. A vault with 10 images (mixed JPEG/PNG) can be added and all thumbnails decoded without errors.

**Status:** ✅ Merged in #4. Decode forces 3-channel RGB; `make_thumbnail` downscales with
`stb_image_resize2` and re-encodes to JPEG; `Vault::add_image` stores the thumbnail as a
separate encrypted chunk (best-effort: decode/thumb failure stores the image with
`thumb_length=0` rather than failing the add). Image tests pass under `scripts/test.sh` and ASAN.
