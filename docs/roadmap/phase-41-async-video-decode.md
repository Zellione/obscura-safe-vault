## Phase 41 — Async video decode 🔜

**Goal:** Video is the one place expensive decode work still runs
synchronously on the render thread (`VideoPlayback::Impl::advance()` calls
`decoder_.next_frame()` once per rendered frame during playback; `do_seek()`
decodes forward to the target PTS on every scrub). A slow codec — notably
software AV1/HEVC, added in Phase 40 — can stall that thread for tens of
milliseconds per frame, which blocks input handling and skews frame pacing
since `App::run()`'s loop only pumps events once per iteration before
rendering. Image decode already solved this shape of problem
(`image::DecodeWorker`, off the render thread since Phase 3/6); this phase
applies the same idea to video, with one key difference: video's demuxer
(`ChunkAvio`) reads/decrypts vault chunks synchronously inside FFmpeg's pull
callback, so — unlike image decode, where the whole compressed buffer is
read up front — only the *codec-level decode* step (no I/O) can safely move
off-thread. Demuxing and audio decode stay on the render thread, since the
vault's file handle must never be touched from more than one thread.

Full design rationale: `docs/superpowers/specs/2026-07-16-async-video-decode-design.md`
(kept local/untracked per this repo's `docs/`-is-gitignored convention — this
file is the durable summary).

### Tasks
- **`media::FrameConverter`** (`src/media/frame_convert.h/.cpp`) — extracted
  the swscale-based YUV-conversion logic out of `VideoDecoder` so both it and
  the new worker share one implementation. Pure refactor, no behavior change.
- **`VideoDecoder::demux_next_video_packet()` / `seek_demux_only()`**
  (`src/media/video_decoder.h/.cpp`) — additive, I/O-only methods for the
  render thread's new async path. `next_frame()`/`seek()` are untouched and
  keep serving every existing synchronous caller (poster/thumbnail decode,
  the full `tests/media/test_video_decoder.cpp` suite).
- **`media::copy_owned_frame`** (`src/media/frame_convert.h/.cpp`) — copies a
  `DecodedFrame`'s planes into a caller-owned buffer, since aliasing
  FFmpeg's internal `AVFrame` across threads is unsafe once decode of frame
  N+1 can run while the render thread still reads frame N's planes.
- **`media::VideoDecodeWorker`** (`src/media/video_decode_worker.h/.cpp`) —
  new background thread, one per open clip. Owns an independent
  `AVCodecContext` opened from the stream's `AVCodecParameters` (no state
  shared with the render thread's `VideoDecoder`/`AVFormatContext`). Consumes
  packets via `submit()`, publishes owned `DecodedFrame`/EOF results consumed
  via the blocking `wait_result()` (the render thread's main path — it
  prefetches a bounded couple of packets ahead, then blocks for the next
  result so playback never presents a stale frame) or the non-blocking
  `take_result()` (used by tests). `begin_seek(target_pts)` drops queued
  not-yet-decoded packets and sets the decode-forward-to-target skip, mirroring
  `VideoDecoder::seek()`'s existing behavior moved to the worker's own codec
  state; a generation counter (owned by the caller, `VideoPlayback::Impl`)
  lets stale results from a superseded seek be discarded on arrival — same
  discard-if-stale idea `image::DecodeWorker::retain()` already uses.
  Reuses `image::decode_wake_event()`'s SDL user-event registration rather
  than adding a second one.
- **`VideoPlayback::Impl`** (`src/ui/video_playback.cpp`) — `decode_into_pending()`
  now demuxes on the render thread and submits to `video_worker_`, draining
  its result queue instead of calling `decoder_.next_frame()` directly.
  `do_seek()` calls `seek_demux_only()` + `video_worker_->begin_seek()`
  instead of the old combined `decoder_.seek()`. Audio (`pump_audio()`,
  `decoder_.next_audio_frame()`) is unchanged — still render-thread,
  synchronous, as it always was (cheap, not the bottleneck).
- `tests/` — `tests/media/test_frame_convert.cpp` (new), `tests/media/test_video_decode_worker.cpp`
  (new, includes a clean-mid-flight-stop case run under `scripts/test.sh --asan`),
  additions to `tests/media/test_video_decoder.cpp` (demux/seek split) and
  `tests/ui/test_video_playback.cpp` (end-to-end async playback + seek,
  alongside the existing `..._writes_no_disk` A/V-sync regression test).

### Acceptance criterion
`scripts/test.sh` and `scripts/test.sh --asan` both green, including every
pre-existing video/playback test unmodified in behavior. Manual check: play
an AV1 `.webm` clip (Phase 40 fixture) and confirm the transport controls
(pause, seek, volume) respond immediately during playback rather than
lagging behind decode.

### Out of scope (deferred)
- A dedicated ThreadSanitizer CI leg — Phase 42.
- Any change to audio decode threading, image decode, or draw-call/present
  threading (draw calls stay render-thread-only; `SDL_Renderer` is not
  documented as safe for concurrent use, and it isn't the bottleneck).
