## Phase 57 — Animated WebP support 🔜

**Goal:** Animated WebP files import, badge, animate in the viewer and auto-play
on hover exactly like animated GIFs (Phase 47) — with no `.osv` format change,
and working in builds without vendored FFmpeg.

**Why it is also a bug fix:** animated WebPs cannot be imported *at all* today.
`decode_webp_from_memory` passes its size check (`WebPGetInfo` returns the `VP8X`
canvas size for an animated file) and then `WebPDecodeRGBInto` returns `NULL`,
because the payload after `VP8X` is `ANIM`/`ANMF` rather than a bare `VP8`/`VP8L`
frame. `decode_and_thumbnail` gets nothing, and the import is rejected. A useful
consequence: **no existing vault can hold an animated WebP**, so there is no
migration burden.

Full design: [docs/superpowers/specs/2026-07-28-phase57-animated-webp-design.md](../superpowers/specs/2026-07-28-phase57-animated-webp-design.md)

### Tasks
- [ ] **Detection** — `src/image/gif_info.{h,cpp}` → `anim_info.{h,cpp}` exposing
  `image::is_animated(ImageFormat, span)`, dispatching to the existing pure
  `gif_is_animated` walker or a new `webp_is_animated` (one `WebPGetFeatures()`
  call reading `has_animation` — no hand-rolled RIFF parser over untrusted input).
  Not gated on `OSV_VENDORED_AV`.
- [ ] **Still decode** — `decode_webp_from_memory` gains an animated branch:
  `WebPAnimDecoder` in `MODE_RGBA`, frame 0, flattened RGBA→RGB over black. Fixes
  import and supplies the thumbnail plus every static display. The static-WebP
  path is untouched.
- [ ] **`media::AnimDecoder` interface** — new `src/media/anim_decoder.h`
  (`AnimFrame` = today's `GifFrame`; `kMinFrameDelay` moves here). `GifDecoder`
  implements it, internals unchanged, still `OSV_VENDORED_AV`-gated.
- [ ] **`media::WebpAnimDecoder`** — new `src/media/webp_anim_decoder.{h,cpp}`,
  **not** gated. Copies each frame out of libwebp's internal canvas (that pointer
  dies on the next call), converts libwebp's *cumulative* ms timestamps to
  per-frame deltas clamped to the 20 ms floor, flattens alpha over black,
  `rewind()` = `WebPAnimDecoderReset`. Borrows the caller's mlock'd buffer, same
  contract as `GifDecoder`.
- [ ] **UI generalization** — `gif_playback`→`anim_playback` (`ui::AnimPlayback`,
  backend chosen from `node.meta.format`), `gif_model`→`anim_model`
  (`AnimHoverGate`, `anim_frames_to_advance`, `kAnim*`),
  `gif_repair`→`anim_repair` (`maybe_repair_animated`, `AnimSniffGate`). Budget
  constants keep their Phase 47 values (200 ms dwell, 1920×1080, 300 frames).
  Widen the six `ImageFormat::GIF` checks (`staging.cpp:35`, `vault.cpp:915`,
  `tile_thumb.cpp:111`, `import_queue.cpp:147`, two in `anim_repair.cpp`) to
  GIF|WebP, and rename the matching members in `image_viewer.h` / `gallery_grid.h`.
  Keep the rename in its own commit, separate from the feature work.
- [ ] **Build** — `link_image_codecs()` gains `webpdemux` / `libwebpdemux` ahead
  of `webp`; `osv_tests` additionally links `webpmux` for `WebPAnimEncoder`
  fixtures. No new submodule, no cmake flag, no FFmpeg change —
  `scripts/build_codecs.sh` already emits both archives.
- [ ] **`tests/`** — `test_anim_info` (GIF cases retained + static/animated/
  truncated/hostile WebP), new `test_webp_anim_decoder` (frame count, delta
  timestamps, 0 ms clamped to 20 ms, `rewind()`, opaque output, undecodable
  input), renamed `test_anim_playback` / `test_anim_model` / `test_anim_repair`
  with WebP cases, and a vault round-trip asserting `animated == true` with a
  non-empty thumbnail. Fixtures synthesized in-process — no binary blobs in the
  repo.
- [ ] Update `mem:module/media` (it covers `src/image` too) and `mem:module/ui`
  for the renamed modules and the new decoder, plus `mem:tech_stack` for the
  newly linked libwebpdemux/libwebpmux.

**No `INDEX_VERSION` bump:** `vault::ImageMeta::animated` is already a
format-neutral `u8`.

**Out of scope (YAGNI):** WebP encoding; honouring the file's loop count;
compositing alpha over the theme background or a checkerboard; per-frame export;
animated AVIF/HEIC image sequences; any `.osv` format change.

### Acceptance criterion
An animated WebP imports (it cannot today), shows the "A" badge in the grid and
strip, animates in the viewer with `Space` toggling pause, and hover-animates on
a tile after 200 ms under the 1920×1080 / 300-frame budget — with animated GIF
behaviour unchanged and WebP animation working in a build without vendored
FFmpeg. `scripts/test.sh` green; `scripts/test.sh --asan` clean.

### Known risks
- **Canvas-sized allocation from untrusted input:** `WebPAnimDecoder` allocates
  w×h×4 and the container permits 16384². Not a new exposure (the static WebP
  path and GIF playback share it), so this phase deliberately adds no gate GIF
  lacks; a decode-dimension cap across all formats belongs in a later hardening
  pass.

**Status:** 🔜 Planned.
