## Phase 57 — Animated WebP support ✅

**Goal:** Animated WebP files import, badge, animate in the viewer and auto-play
on hover exactly like animated GIFs (Phase 47) — with no `.osv` format change,
and working in builds without vendored FFmpeg.

**Why it was also a bug fix:** animated WebPs could not be imported *at all*.
`decode_webp_from_memory` passed its size check (`WebPGetInfo` returns the `VP8X`
canvas size for an animated file) and then `WebPDecodeRGBInto` returned `NULL`,
because the payload after `VP8X` is `ANIM`/`ANMF` rather than a bare `VP8`/`VP8L`
frame. `decode_and_thumbnail` got nothing, and the import was rejected. A useful
consequence: **no vault written before this phase can hold an animated WebP**, so
there was no migration burden.

Full design: [docs/superpowers/specs/2026-07-28-phase57-animated-webp-design.md](../superpowers/specs/2026-07-28-phase57-animated-webp-design.md)

### Completed work
- **Detection** — `src/image/gif_info.{h,cpp}` → `anim_info.{h,cpp}`, exposing
  `image::is_animated(ImageFormat, span)` which dispatches to the existing pure
  `gif_is_animated` walker or the new `webp_is_animated`. The latter is one
  `WebPGetFeatures()` call reading `has_animation` — no hand-rolled RIFF walker
  over untrusted input (invariant 6). Not gated on `OSV_VENDORED_AV`.
- **Still decode** — `decode_webp_from_memory` gained an animated branch:
  `WebPAnimDecoder` in `MODE_RGBA`, frame 0, flattened RGBA→RGB over black. This
  is what fixes import, and it supplies the baked thumbnail plus every static
  display. The static-WebP path is untouched.
- **`media::AnimDecoder`** — new `src/media/anim_decoder.h` holding `AnimFrame`
  (formerly `GifFrame`) and `kMinFrameDelay`. `GifDecoder` implements it,
  internals unchanged, still `OSV_VENDORED_AV`-gated.
- **`media::WebpAnimDecoder`** — new `src/media/webp_anim_decoder.{h,cpp}`, **not**
  gated. Copies each frame out of libwebp's internal canvas (that pointer is
  invalidated by the next `GetNext`/`Reset`), converts libwebp's *cumulative* ms
  timestamps to per-frame deltas clamped to the 20 ms floor via the pure
  `webp_frame_delay_s`, flattens alpha over black, `rewind()` =
  `WebPAnimDecoderReset`. Borrows the caller's mlock'd buffer, same contract as
  `GifDecoder`. Rejects a single-frame file: one frame is not an animation.
- **UI generalization** — `gif_playback`→`anim_playback` (`ui::AnimPlayback`),
  `gif_model`→`anim_model` (`AnimHoverGate`, `anim_frames_to_advance`, `kAnim*`),
  `gif_repair`→`anim_repair` (`maybe_repair_animated`, `AnimSniffGate`), plus
  `ui::gif_viewer_consumes_key`→`anim_viewer_consumes_key` and the viewer/grid
  members. `AnimPlayback` picks its backend from `node.meta.format`. Budget
  constants keep their Phase 47 values (200 ms dwell, 1920×1080, 300 frames).
  The viewer's help group `GIF playback` became `Animation playback` — the only
  user-visible string change.
- **One format predicate** — `vault::format_can_animate(ImageFormat)` in
  `src/vault/index.h` is now the single source of truth, used by import
  (`staging.cpp` and the background `import_queue`), `repair_image_animated`,
  `AnimSniffGate`, `tile_shows_animated_badge` and `tile_can_hover_animate`. A
  stale or hostile `animated` flag on a format that cannot animate no longer
  produces a badge the viewer could never honour.
- **Build** — `link_image_codecs()` gained `webpdemux` / `libwebpdemux` ahead of
  `webp`. No new submodule, no cmake flag, no FFmpeg change:
  `scripts/build_codecs.sh` already emitted `libwebpdemux.a`. The three
  `src/ui/gif_*.cpp` entries in the hand-enumerated `osv_tests` file list were
  renamed (`src/image` and `src/media` are globbed, so the new files there needed
  no project edit).
- **Fixtures** — two committed lossless binaries in `tests/image/fixtures/`,
  following the `sample.webp` convention: `sample_anim.webp` (194 B; 8×8, 3
  opaque frames 100 ms apart) and `sample_anim_alpha.webp` (140 B; 8×8, frame 0
  fully transparent), the latter pinning the flatten-over-black rule with an
  exact `00 00 00 ff` assertion. Regeneration commands are in that directory's
  README.
- **Tests** — 41 added across detector (including truncated and hostile input),
  still decode, `WebpAnimDecoder` (frame count, delta timestamps, rewind,
  opacity, reopen, truncation), `AnimPlayback` (WebP plays, pauses, loops; static
  WebP and JPEG decline to build), vault round-trip (`animated` set at import,
  thumbnail generated), lazy repair in both directions, the sniff gate, and the
  badge/hover gate. Plus a fuzz case over the animated fixture: the animation
  parser is not reached by the existing static-WebP fuzzer.

**No `INDEX_VERSION` bump:** `vault::ImageMeta::animated` was already a
format-neutral `u8`.

**Out of scope (YAGNI):** WebP encoding; honouring the file's loop count;
compositing alpha over the theme background or a checkerboard; per-frame export;
animated AVIF/HEIC image sequences; any `.osv` format change.

### Acceptance criterion
An animated WebP imports (it could not before), shows the "A" badge in the grid
and strip, animates in the viewer with `Space` toggling pause, and hover-animates
on a tile after 200 ms under the 1920×1080 / 300-frame budget — with animated GIF
behaviour unchanged and WebP animation working in a build without vendored
FFmpeg. **1677 tests / 0 failed; `scripts/test.sh --asan` clean.**

### Follow-ups
- **A pre-existing build break fixed here.** `src/ui/gif_playback.cpp` opened
  `namespace ui {` *inside* its `#ifdef OSV_VENDORED_AV`, so the `#else` stub was
  declared outside the namespace and the translation unit did not compile at all
  without vendored FFmpeg (the stub was also missing the `frame_count()` its
  public wrapper called). Phase 47's "non-FFmpeg builds still show the badge and
  the static first frame" was therefore not buildable. The `AnimPlayback`
  restructure fixes it by construction, but nothing in CI would have caught it.
  **Closed by the follow-up PR that added the `tests-no-av` CI job** (`--no-av`
  premake option, Linux, builds app + tests and runs the suite); adding that job
  immediately exposed two more files broken the same way —
  `tests/media/test_sar_display.cpp` (used gated `media::display_dims` ungated)
  and `tests/ui/test_anim_playback.cpp` (a helper left unused once the WebP cases
  moved outside the gate, tripping `-Werror=unused-function`).
- **Canvas-sized allocation from untrusted input.** `WebPAnimDecoder` allocates
  w×h×4 and the WebP container permits 16384², so a hostile vault can provoke a
  large allocation. Not a new exposure — the static WebP path (`WebPDecodeRGBInto`,
  w×h×3) and GIF playback share it — so this phase deliberately added no gate GIF
  lacks. A decode-dimension cap across all formats belongs in a hardening pass.
- **The WebP arm of the lazy repair is unreachable today.** No vault written
  before this phase can hold an animated WebP, so `maybe_repair_animated` will
  never correct one. It is format-generic anyway: it costs nothing and covers a
  vault whose writer classified a file differently.
- **`strip_cell_rect` / `draw_thumbnail_strip` manual sync point** (Phase 47)
  still applies; this phase touched neither, but the SYNC comments remain
  load-bearing.

**Status:** ✅ Shipped.
