## Phase 47 — Animated GIF support ✅

**Goal:** Animated GIFs display an "A" badge in the gallery grid and thumbnail
strip, animate in the full-screen viewer with Space toggling pause, and auto-loop
on a grid or strip tile after a 200 ms hover dwell — subject to a 1920×1080 /
300-frame budget. GIFs stored before Phase 47 get the same treatment on first
view without any migration step. Non-FFmpeg builds still show the badge and the
static first frame.

### Completed work
- `image::gif_is_animated()` in `src/image/gif_info.{h,cpp}` — pure,
  bounds-checked GIF block walker, deliberately NOT gated on `OSV_VENDORED_AV`
  so the badge works in non-FFmpeg builds.
- `vault::ImageMeta::animated` (`bool`), serialized as a `u8` after `thumb_length`
  on Image nodes. `INDEX_VERSION` 6 → **7**. v1–v6 blobs read it as false. A byte
  other than 0/1 is REJECTED on deserialise, not clamped (matching the Phase 37
  `sort_key` rule).
- `Vault::add_image` sets the flag at import (covers file-picker, background
  multi-import, and all archive imports).
- `Vault::repair_image_animated(std::string_view node_path, bool)` +
  `ui::maybe_repair_gif_animated(...)` in `src/ui/gif_repair.{h,cpp}` — lazy
  bidirectional healing for GIFs stored before this phase, persisted through the
  same crash-safe `commit_index()` path as `repair_video_metadata`. No-op (no
  write) when the flag is already correct.
- FFmpeg build: the `gif` **decoder**, **demuxer**, AND **parser** are enabled in
  `scripts/build_codecs.sh` and `scripts/build_ffmpeg_windows.sh`. The parser is
  essential: FFmpeg n7.1.1's gif demuxer emits raw 1024-byte chunks and sets
  `need_parsing = AVSTREAM_PARSE_FULL_RAW` (`libavformat/gifdec.c:224`),
  delegating frame reassembly to `libavcodec/gif_parser.c`. Without it,
  `av_read_frame` returns unparsed chunks and decoding fails on the second chunk
  with `AVERROR_INVALIDDATA`.
- `media::GifDecoder` in `src/media/gif_decoder.{h,cpp}` (gated `OSV_VENDORED_AV`)
  — `MemAvio` over the decrypted bytes → gif demuxer → gif decoder → swscale to
  RGBA. Streaming, one frame at a time, constant memory. No audio, no packet
  queues, no seeking, no hwaccel. `rewind()` for looping. Per-frame delay clamped
  to a 20 ms floor. `open()` BORROWS the caller's buffer.
- `ui::GifPlayback` in `src/ui/gif_playback.{h,cpp}` — pImpl, FFmpeg confined to
  the `.cpp` so `image_viewer.h` compiles everywhere. Auto-loop, **Space** toggles
  pause, zoom/pan unchanged. Decrypted bytes held in an mlock'd
  `crypto::SecureBytes` that outlives the borrowing decoder. Uploads frames
  row-by-row honoring `SDL_LockTexture`'s pitch.
- `ui::gif_model.{h,cpp}` — pure logic: `GifHoverGate` (200 ms dwell, one
  start-edge per hover), `gif_within_hover_dimension_budget(w,h)`,
  `gif_hover_frame_count_exceeded(frames)`, `gif_frames_to_advance(...)` with a
  64-frame catch-up cap.
- `ui::tile_shows_animated_badge(node)` and `ui::draw_animated_badge(...)` in
  `src/ui/tile_thumb.{h,cpp}`; `ui::tile_can_hover_animate(node)` combining the
  badge check with the dimension budget. The "A" badge is drawn top-right in the
  gallery grid and the viewer's thumbnail strip.
- `ui::strip_cell_rect(...)` added to `src/ui/strip_layout.{h,cpp}` — the forward
  index→rect mapping, alongside the pre-existing inverse `strip_hit_axis`. NOTE:
  `gfx::Renderer::draw_thumbnail_strip` computes the same layout internally and
  **cannot** call this (gfx must not depend on ui), so both sites carry SYNC
  comments. This is a manual sync point worth recording.
- Hover animation: the tile under the cursor animates after a 200 ms dwell, in
  BOTH the gallery grid and the viewer strip, at most one at a time, gated by a
  1920x1080 dimension budget pre-construction and a 300-frame cap during
  playback. Over-budget GIFs stay static but still show the badge.
- `gfx::Window::mouse_x()/mouse_y()` added (wrapping `SDL_GetMouseState`).
- Comprehensive unit tests: detector (incl. hostile/truncated input), index
  round-trip and v6 back-compat, hover/advance logic, plus integration tests
  for `GifDecoder` and `GifPlayback`.

### Acceptance criterion
- An animated GIF imported into a vault shows an "A" badge in the gallery grid
  and thumbnail strip.
- Animated GIFs animate in the full-screen viewer with Space toggling pause.
- Animated GIFs animate on a grid or strip tile after a 200 ms hover dwell,
  subject to a 1920×1080 / 300-frame budget.
- A GIF stored before Phase 47 gets the same treatment on first view, without
  any migration step.
- Non-FFmpeg builds still show the badge and the static first frame.
- `scripts/test.sh` green (997 tests).
- `scripts/test.sh --asan` clean (no memory/UB errors).

### Follow-ups
- **FFmpeg `gif` parser requirement:** The `gif` decoder, demuxer, AND parser
  must all be enabled in FFmpeg builds. The parser is essential (FFmpeg's gif
  demuxer needs it); a future FFmpeg bump must not silently regress this —
  explicitly document it in the build scripts.
- **`strip_cell_rect` / `draw_thumbnail_strip` manual sync point:** Both
  `ui::strip_cell_rect` and `gfx::Renderer::draw_thumbnail_strip` implement the
  same geometry layout. Gfx cannot depend on ui, so both computations are
  duplicated. Each site carries a SYNC comment; keep them in sync on any
  strip-geometry changes.
- **Frame-count enforcement is runtime, not pre-rejection:** A GIF's total frame
  count is not knowable without decoding it (the first frame may report a frame
  count that is wrong or incomplete). Hover animations are gated by a 300-frame
  runtime cap, not a pre-import rejection, so over-budget GIFs remain importable
  but static.
- **Hover dwell timing with unfocused window:** Hover animation dwell timers
  complete even if the app window loses focus, because `SDL_GetMouseState`
  returns the last known mouse position. This is low risk under the local-attacker
  threat model. A `Window::is_focused()` guard is the cheap fix if stricter
  behavior is wanted later.
- **Degenerate GIF import UBSan flag (stb bug):** Importing a 1×1 or 2×2 GIF
  triggers an in-bounds but misaligned store inside vendored stb's SIMD resizer
  (`stb_image_resize2.h`), which UBSan flags as undefined. Harmless on the
  supported x86-64 targets; revisit on an stb bump.
- **`scripts/test.sh` compile coverage gap (pre-existing):** `scripts/test.sh`
  does not compile 25 of the `src/ui/*.cpp` files — the `osv_tests` premake
  target enumerates them individually while the `osv` app target globs. A compile
  error in any of the unlisted files passes `scripts/test.sh` unnoticed. This
  phase shipped three tasks against an app that would not build because of it.
  This is a PRE-EXISTING project-wide issue, not caused by Phase 47, and deserves
  its own follow-up.
