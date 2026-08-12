# Module: ui/viewer — image/video viewer, playback, slideshow

Full-screen image and video playback with zoom, pan, slideshow, and strip navigation.

## Image / video viewer
- `image_viewer.*`, `widgets.*` — viewer has Fit + FillScroll + Slideshow modes, bottom/left
  strip toggle (keys F/T, P starts slideshow). `widgets` has button_state + elide_middle /
  elide_tail (pure, templated on the measure callable) + their font-bound bindings fit_text /
  fit_text_tail — middle by default, tail where the string's start carries the meaning
  (`[key]  description` help lines). Hosts a fit-only VideoPlayback when the current
  item `is_video()`: Space play/pause, J/L ±5s, `,`/`.` frame-step, drag seek bar; M mute,
  volume ∓5% (seek bar seeks video+audio in-sync). Volume via `ui::volume_dir` — `-`/`+` glyph
  keys (HUD `[-/+] Vol`) + `[`/`]` produced char resolved through `SDL_GetModState` (German
  AltGr) + physical bracket scancodes; level seeded from `media::saved_volume()` on open,
  written back on change. `R` toggles loop (process-lifetime `media::saved_loop_enabled()`;
  VideoPlayback re-seeks to 0 and keeps playing at EOF when set); on-screen ring indicator next
  to play/pause. **Phase 56:** `on_vault_changed()` uses the `album_rebind.*` module to re-bind
  by path, preserving zoom, pan, fill-scroll offset, video position and animation frame.
  Chrome: `viewer_chrome(const ImageViewer&)` free friend returns the `ChromeBands` for the
  whole viewer area (window minus strip) — an OPAQUE STRIP_BG header band (name/index/zoom +
  [F1] Help) and an opaque footer band, with the media fit into `.content` only, so a band
  never covers picture/video. `viewport_rect()` IS `viewer_chrome(*this).content`, so zoom,
  pan, the fill-scroll model, mouse hit-testing and drawing all agree. Windowed, both bands are
  always reserved (the image never resizes when a status comes and goes); fullscreen drops the
  header entirely — HUD text included — for an edge-to-edge picture, and forces the footer band
  in only while `viewer_footer_text()` is non-empty. That one free friend is the single source
  for both the reserved height and the drawn line, so they cannot disagree; it prefers the
  export status over the "Video playback unavailable in this build." notice. VideoPlayback's
  own CONTROL_H bar sits inside `.content`, directly above the footer band (same STRIP_BG, so
  the two read as one bar). Ctor gains `initial_strip_side` (default Bottom); three free
  friends —
  `current_strip_side`, `capture_video_resume` (snapshot outgoing viewer's video path+position
  into a GallerySessionState, or clear when the current item isn't a live video),
  `apply_video_resume` (seek a freshly (re)opened matching video to the remembered position,
  called right after `on_enter()` builds video_). "Collection mode" (explicit image set +
  per-image path + exit Nav) lets the viewer serve favorites/tag sets, not just one gallery.
  **Strip fetch windowing (post-Phase-58 fix):** `render_strip` requests thumbnail textures
  ONLY for cells in `strip_visible_range(scroll, extent, thumb, gap, count,
  STRIP_PREFETCH_CELLS)` (pure helper in `strip_layout.*`, ±8-cell margin) and calls
  `DecodeWorker::retain()` with the window's keys each frame so out-of-window queued fetches
  are dropped. Requesting the whole album (the Phase 58 shape) enqueued every thumbnail in
  the album as a background fetch+decode job — on a big cold vault that ground the disk for
  minutes, starved the render thread's video demux reads (~3 fps playback), and an album
  bigger than the 256 MB texture budget re-evicted and re-fetched forever. The badge/hover
  loop is bounded to the same window. Off-window cells draw as placeholders.
- `anim_playback.*` (Phase 47 GIF, Phase 57 WebP; was `gif_playback.*`) — `AnimPlayback`:
  pImpl, decoder libs confined to `.cpp`. Picks its `media::AnimDecoder` backend from
  `node.meta.format` — WebP via libwebp (ALWAYS available), GIF via FFmpeg (`OSV_VENDORED_AV`
  only); no backend -> `valid()==false` and the host shows the static first frame. Auto-loop
  (a file's declared loop count is deliberately IGNORED, so both formats behave alike), Space
  toggles pause, zoom/pan unchanged. Decrypted bytes held in mlock'd `crypto::SecureBytes`
  outliving the decoder. Frames uploaded row-by-row honoring `SDL_LockTexture` pitch.
  **Phase 57 fixed a latent break here:** the file used to open `namespace ui {` INSIDE its
  `#ifdef OSV_VENDORED_AV`, so the `#else` stub landed outside the namespace and the TU did not
  compile at all without vendored FFmpeg. `Impl` is now always compiled; only the GIF backend is
  gated. No CI job builds without `OSV_VENDORED_AV`, so nothing would have caught it.
- `anim_model.*` (Phase 47; was `gif_model.*`) — pure logic: `AnimHoverGate` (200 ms dwell, one
  start-edge per hover), `anim_within_hover_dimension_budget(w,h)`,
  `anim_hover_frame_count_exceeded(frames)`, `anim_frames_to_advance(...)` with 64-frame
  catch-up cap.
- `video_playback.*` — in-viewer player: `VideoDecoder` (demux only, render-thread-side) +
  `VideoDecodeWorker` (codec-level decode, bg thread, see `mem:module/media`) + YUV texture +
  `SDL_AudioStream` (master audio clock) + seek bar (both tracks); mute/volume via
  `SDL_SetAudioStreamGain`; A/V sync via `av_sync::decide`; pause pauses both; pImpl gated on
  `OSV_VENDORED_AV` (non-AV build -> poster). `seek(seconds)` is clamped, does NOT touch
  play/pause (restores a resume bookmark right after ctor; playback opens paused). Impl demuxes
  (`demux_next_video_packet()`) + submits packets by `generation_`, reads back Results. Shared
  helpers `feed_one_packet()`/`prefetch_upto()`/`consume_result()`: `decode_into_pending()`
  blocks (bounded by `wait_result()`'s ~20ms timeout, retried) — used ONLY by the ctor's first
  frame (pts 0 is a keyframe, no decode-forward gap); `try_advance_pending()` is the steady
  path, a single `wait_result()` (no retry) so `render()` never blocks >~20ms under a slow
  codec. Both keep the worker's `outstanding()` backlog to `PREFETCH_DEPTH` packets ahead and,
  on a miss, feed one more up to `MAX_STEADY_IN_FLIGHT`.
  **Seeks are asynchronous (Phase 59):** `do_seek()` is a pure state transition — demux reseek
  (`seek_demux_only`), `++generation_` + `begin_seek()`, audio re-base, `model_.seek_to(target)`
  (transport jumps immediately) — and returns without waiting; the last-shown frame stays up
  until the first at-or-after-target frame lands. While `skip_pending_` is set (a seek's
  decode-forward in progress): `animating()` reports true even when paused (keeps App::run's
  poll gate open so the seek gets ticks), `try_advance_pending()` tops up to `SEEK_FEED_DEPTH`
  (32) instead of `PREFETCH_DEPTH` and feeds uncapped on timeout (one-time GOP-bounded gap),
  and `consume_result()` realigns the transport to the decoded frame's actual pts on resolve.
  Stale-generation Results are discarded (the worker un-counts every finished job incl.
  silently-discarded seek frames, so no phantom backlog wedges feed). Impl's audio + pending-
  frame state each live in nested `AudioState`/`FrameState` structs (SonarQube struct-size).
- `playback_model.*` — pure video transport maths: clock/clamp/seek-bar map/mm:ss/frame-due
  (pure/tested).
- `slideshow_view.*` — full-screen slideshow SDL plumbing (owns SlideshowModel + cross-fade
  render). `slideshow_model.*` — auto-advance/wrap/shuffle/cross-fade maths (pure/tested,
  driven via update(dt)).
- `full_tex_cache.*` — shared decode→GPU full-res texture cache (decrypt into mlock'd
  SecureBytes, wipe after upload); used by viewer + slideshow.
