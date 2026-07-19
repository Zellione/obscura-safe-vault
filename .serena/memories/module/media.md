# Module: media/ + image/ — decode (image, video, audio)

Referenced from `mem:core`. Covers `src/image/` (image codecs + thumbnails, always built)
and `src/media/` (FFmpeg video/audio, whole subsystem gated `OSV_VENDORED_AV`).

## image/
- `decode.*`, `thumbnail.*` — stb_image decode, thumbnail generation.
- `format_registry.*` — magic-byte format detection.
- `decoder.*` — `Decoder` interface + `DecoderRegistry` (polymorphic dispatch;
  `default_registry()` wires WebP/HEIF/stb decoders).
- `decode_webp.*`, `decode_heif.*` — libwebp (WebP), libheif (HEIC/AVIF).
- `decode_worker.*` — off-thread image decoder: caller reads+decrypts on its thread, worker
  runs `decode_from_memory()` on one bg thread, caller uploads result to GPU. Coalesces by
  key, SDL wake event, `retain()`/`pending()`. Each screen owns its own worker; FullTexCache
  + GalleryGrid use it for async decode.

## media/ (all gated OSV_VENDORED_AV)
Files: `video_source.*`, `chunk_avio.*`, `mem_avio.*`, `video_decoder.*`, `audio_decoder.*`,
`av_sync.*`, `audio_frame.h`, `volume_setting.*`, `loop_setting.*`, `video_probe.*`,
`decoded_frame.h`, plus `frame_convert.*`, `video_decode_worker.*`, `hw_accel.*`.

### Demux + software decode
- `VideoSource` = decrypt-on-demand byte stream over a video's ChunkStore (mlock'd 1-chunk
  cache). `ChunkAvio`/`MemAvio` = `AVIOContext` (read+seek, never a temp file).
- `VideoDecoder` = FFmpeg shared demuxer feeding both video + audio via per-stream packet
  queues (`vq_`/`aq_`); H.264/HEVC decode → `DecodedFrame` (yuv420p/nv12, swscale fallback)
  + keyframe seek; `has_audio()`/`audio_info()`/`next_audio_frame()`.
- `AudioDecoder` owns an `AVStream*`, decodes planar PCM → interleaved F32 in
  `AudioFrame{samples,channels,sample_rate,pts_seconds}`.
- `av_sync` = PURE logic (no SDL/FFmpeg) for audio-clock tracking: `decide(audio_clock,
  frame_pts,...)` → `FrameAction{Present,Hold,Drop}`; `audio_clock(base,samples_consumed,
  rate)`; `clamp_volume`/`effective_gain` helpers; unit-tested.
- `probe_video` = container/codec/dims/duration + first-frame poster; best-effort (succeeds
  with placeholder Unknown/0/empty if the container is detected but the codec isn't decodable
  yet — `ui/video_repair.*` + `Vault::repair_video_metadata` heal such nodes later).
- Supported codecs (`VideoCodec`): H.264, HEVC (native); AV1 via the already-vendored libaom
  as FFmpeg's `libaom-av1` decoder (FFmpeg's own native "av1" decoder is a hwaccel-dispatch
  shim only — no software decode); QTRLE, Cinepak (native `.mov`).
- `volume_setting.*` / `loop_setting.*` — process-global in-memory volume + loop-toggle
  state (NOT AV-gated): `saved_volume()`, `saved_loop_enabled()`/`set_saved_loop_enabled()`.
  Volume persists via `platform::VolumePref`; loop is process-lifetime only.

### frame_convert.{h,cpp} — FrameConverter
swscale-based YUV->I420 conversion shared by VideoDecoder + VideoDecodeWorker: `zero_copy()`
for already-I420/NV12 frames, `to_i420()` otherwise, cached `SwsContext` reused per stream.
`copy_owned_frame()` copies a DecodedFrame's planes into a caller-owned
`std::vector<uint8_t>` for safe cross-thread handoff (FFmpeg's internal AVFrame buffers are
unsafe to alias once the next decode call can run concurrently).

### video_decode_worker.{h,cpp} — VideoDecodeWorker
Background `std::jthread` doing ONLY codec-level video decode (`avcodec_send_packet`/
`receive_frame` + FrameConverter), fed demuxed packets by the render thread via
`submit(pkt,generation)`. Owns its own `AVCodecContext` — no state shared with VideoDecoder's
`AVFormatContext`/vault handle; demuxing + the vault file handle stay render-thread-only (this
worker never touches AVFormatContext/ChunkAvio). Publishes generation-tagged
`Result{generation,eof,frame,storage}` via `take_result()` (non-blocking) or `wait_result()`
(short-timeout blocking, the render thread's main path). `outstanding()` reports
submitted-but-not-finished jobs, decremented for EVERY finished job (incl. discard-only ones
that publish no Result) so the render thread gates feeding on the real backlog.
`begin_seek(target_pts)` drops queued undecoded packets + skips decoded frames below target; a
superseded seek's stale results are discarded by the caller comparing `Result::generation`.
`run()`'s wait checks `stop_` regardless of whether `queue_` is empty (draining a full backlog
before honoring stop_ made teardown block for seconds after slow decode); the destructor's own
cleanup loop frees what's left. `run()` is decomposed into `wait_for_job()`/`send_packet()`/
`decode_available_frames()`/`publish_decoded_frame()`/`publish_result()`/`publish_eof()`
(SonarQube complexity limits).

### hw_accel.{h,cpp} — HwAccelContext (opportunistic hardware decode)
- `try_attach_hwaccel(ctx,decoder)` attaches a process-wide cached `AVBufferRef` hw device
  context + `get_format` callback to a codec context being opened, IF a platform hwaccel macro
  (`OSV_HWACCEL_D3D11VA`, `OSV_HWACCEL_VAAPI`) is compiled in and the decoder advertises
  support. Device creation is attempted exactly once per process (cache-the-outcome pattern).
  Compiles to an always-false stub when neither macro is defined, so VideoDecodeWorker needs no
  #ifdef. `test_only_force_hwaccel_unavailable(bool)` is the deterministic-failure injection
  hook (CI has no real GPU decode block).
- `transfer_hw_frame(frame,sw_frame)` wraps `av_hwframe_transfer_data()`: a hw-decoded
  AVFrame's `data[]` planes are an opaque device handle, so
  `VideoDecodeWorker::publish_decoded_frame()` transfers into a lazily-allocated, reused
  `hw_transfer_frame_` BEFORE the zero-copy/swscale FrameConverter pipeline runs (that pipeline
  is otherwise unmodified).
- VideoDecodeWorker calls `try_attach_hwaccel()` when opening its codec context (`hw_active_`
  tracks the outcome); on any hard decode error while hw_active_, `reopen_software_only()`
  drops the hw context and opens a fresh software-only one from a saved copy of the original
  `AVCodecParameters` (`saved_params_`) for the rest of that clip — playback continues, never
  aborts.
- VAAPI (Linux): `vendor/vaapi-shim` (`osv_vaapi_shim.a`) provides the ~36 `va*` symbols
  FFmpeg's `hwcontext_vaapi.c`/`vaapi_*.c` glue references via its own internal
  `dlopen("libva.so.2"/"libva-drm.so.2")` + `dlsym()` forwarding, so the app never gets a
  DT_NEEDED entry on real libva. `vendor/libva` is a headers-only submodule (never built). See
  `docs/superpowers/specs/2026-07-17-hardware-video-decode-design.md`.
