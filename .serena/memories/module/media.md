# Module: media/ + image/ — decode (image, video, audio)

Referenced from `mem:core`. Covers `src/image/` (image codecs + thumbnails, always built)
and `src/media/` (FFmpeg video/audio, whole subsystem gated `OSV_VENDORED_AV`).

## image/
- `decode.*`, `thumbnail.*` — stb_image decode, thumbnail generation.
- `format_registry.*` — magic-byte format detection.
- `decoder.*` — `Decoder` interface + `DecoderRegistry` (polymorphic dispatch;
  `default_registry()` wires WebP/HEIF/stb decoders).
- `decode_webp.*`, `decode_heif.*` — libwebp (WebP), libheif (HEIC/AVIF).
  Phase 57: an animated WebP has no top-level VP8/VP8L chunk, so
  `WebPDecodeRGBInto` fails on it (while `WebPGetInfo` still reports the VP8X
  canvas size) — `decode_webp_from_memory` routes those through
  `WebPAnimDecoder`'s frame 0, flattened over black. Before that, animated WebPs
  could not be imported at all.
- `anim_info.*` (was `gif_info.*`) — `is_animated(ImageFormat, span)` dispatches
  to `gif_is_animated()` (Phase 47: pure bounds-checked GIF block walker) or
  `webp_is_animated()` (Phase 57: one `WebPGetFeatures()` call reading the VP8X
  ANIMATION flag — libwebp's own parser, not one of ours, on untrusted input).
  Not gated on `OSV_VENDORED_AV`, so the badge works everywhere.
- `decode_worker.*` — off-thread image decoder: caller reads+decrypts on its thread, worker
  runs `decode_from_memory()` on one bg thread, caller uploads result to GPU. Coalesces by
  key, SDL wake event, `retain()`/`pending()`. Each screen owns its own worker; FullTexCache
  + GalleryGrid use it for async decode. **Phase 58:** added `submit_fetch(key, Fetcher)` —
  a two-stage pipeline where `Fetcher = std::function<bool(crypto::SecureBytes&)>` runs on the
  worker thread BEFORE decode. Hosts (tile_thumb, grid detail) wire the Fetcher to their vault's
  `read_thumbnail()` and cache strategy; a false return yields an empty Result (memoized as
  failed). `image/` stays vault-agnostic: the caller brings the fetch logic.

## media/ (gated OSV_VENDORED_AV except anim_decoder.h + webp_anim_decoder.*)
Files: `video_source.*`, `chunk_avio.*`, `mem_avio.*`, `video_decoder.*`, `audio_decoder.*`,
`av_sync.*`, `audio_frame.h`, `volume_setting.*`, `loop_setting.*`, `video_probe.*`,
`decoded_frame.h`, plus `frame_convert.*`, `video_decode_worker.*`, `hw_accel.*`.

### Demux + software decode
- **Container detection** (`detect_video_container()` in `video_format.cpp`): bounds-checked
  magic-byte detection for H.264/H.265 (raw), MP4 (ftyp), Matroska (EBML), WebM (EBML),
  AVI (RIFF+AVI ), MPEG-PS (0x000001BA), MPEG-TS (0x47 sync at offsets 0, 188, 376),
  ASF (GUID), FLV (FLV magic), Ogg (OggS), RealMedia (.RMF). TS checked last (single
  0x47 is weak signature; require all three sync offsets).
- `VideoSource` = decrypt-on-demand byte stream over a video's ChunkStore (mlock'd 1-chunk
  cache). `ChunkAvio`/`MemAvio` = `AVIOContext` (read+seek, never a temp file).
- `VideoDecoder` = FFmpeg shared demuxer feeding both video + audio via per-stream packet
  queues (`vq_`/`aq_`); H.264/HEVC + ~34 legacy codecs (Phase 52: MPEG-1/2, MPEG-4 ASP,
  MS-MPEG4 v1–v3, WMV1/2/3, VC-1, H.263, FLV1, VP6/a/f, SVQ1/3, DV, MSVideo1, RPZA,
  HuffYUV, FFV1, Theora, RealVideo 10/20/30/40) decode → `DecodedFrame` (yuv420p/nv12,
  swscale fallback) + keyframe seek; `has_audio()`/`audio_info()`/`next_audio_frame()`.
  `open()` delegates to `container_duration_us()` (fmt duration, else stream duration, else 0) and
  `open_audio_stream()` (non-fatal throughout — leaves `audio_index_` at -1 for video-only) to stay
  under SonarQube's complexity cap. SAR is stored as one `AVRational sar_`, exposed via
  `sar_num()`/`sar_den()`.
  `display_dims()` helper computes anamorphic-corrected width (round(coded_width * SAR_num / SAR_den))
  for DVD/DV clips with non-square pixels (SAR read via `av_guess_sample_aspect_ratio()`).
  VideoMeta stores display dims (not coded dims) for detail panel + thumbnail poster.
- `AudioDecoder` owns an `AVStream*`, decodes planar PCM → interleaved F32 in
  `AudioFrame{samples,channels,sample_rate,pts_seconds}`. Phase 52 added decoders for
  legacy formats (MP2, WMA v1/v2, Cook, RealAudio 144/288, PCM s16le/u8, ADPCM ms/ima_wav).
- `anim_decoder.h` (Phase 57, NOT gated) — `AnimFrame{rgba,width,height,delay_s}`
  (was `GifFrame`) + the abstract `AnimDecoder` (`open`/`next_frame`/`rewind`/
  `width`/`height`/`frames_decoded`) and `kMinFrameDelay` (20 ms). Pulls in
  neither FFmpeg nor libwebp, so it is includable in any build. `open()` BORROWS
  the caller's buffer; every emitted frame is complete and opaque RGBA.
- `gif_decoder.*` (Phase 47, gated `OSV_VENDORED_AV`) — `GifDecoder`, an
  `AnimDecoder` backend: `MemAvio` over decrypted bytes → gif demuxer → gif
  decoder → swscale to RGBA. Streaming, one frame at a time, constant memory. No
  audio, no packet queues, no seeking, no hwaccel.
- `webp_anim_decoder.*` (Phase 57, NOT gated — libwebp is a hard dependency, so
  animated WebP plays even without vendored FFmpeg) — `WebpAnimDecoder`, the
  other `AnimDecoder` backend, over libwebp's `WebPAnimDecoder` (libwebpdemux).
  Copies each frame out of libwebp's internal canvas (invalidated by the next
  `GetNext`/`Reset`), flattens alpha over black, and converts libwebp's
  CUMULATIVE ms timestamps to per-frame deltas via the pure
  `webp_frame_delay_s(prev_ms, ms)`, clamped to `kMinFrameDelay`. `open()`
  rejects a single-frame file. `rewind()` = `WebPAnimDecoderReset`.
- `av_sync` = PURE logic (no SDL/FFmpeg) for audio-clock tracking: `decide(audio_clock,
  frame_pts,...)` → `FrameAction{Present,Hold,Drop}`; `audio_clock(base,samples_consumed,
  rate)`; `clamp_volume`/`effective_gain` helpers; unit-tested.
- `probe_video` = container/codec/dims/duration + first-frame poster; best-effort (succeeds
  with placeholder Unknown/0/empty if the container is detected but the codec isn't decodable
  yet — `ui/video_repair.*` + `Vault::repair_video_metadata` heal such nodes later).
  **Known limitation (Phase 52):** raw MPEG-PS (`.mpg`/`.mpeg`) files import (container
  detected) but store as Unknown-codec video since the decode-only build cannot identify
  the elementary-stream codec inside the PS wrapper (full system FFmpeg can — it is a
  stripped build limitation). MPEG-1/2 are fully supported via MKV, MPEG-TS, MP4, MOV.
- `media::map_codec_id(int)` (Phase 52) — testable mapping from FFmpeg's `AVCodecID` to the
  app's `VideoCodec` enum. Extracts all 27 Phase 52 + pre-existing codec mappings into one place.
  Registers modern codecs (H.264, HEVC, AV1) and Phase 52 legacy codecs (MPEG-1/2, MPEG-4 ASP,
  MS-MPEG4 v1–v3, WMV1/2/3, VC-1, H.263, FLV1, VP6/a/f, SVQ1/3, DV, MSVideo1, RPZA, HuffYUV,
  FFV1, Theora, RV10/20/30/40, QTRLE, Cinepak). VideoCodec enum now spans values 0–36.
  Implemented as a `static constexpr std::array` of `{AVCodecID, VideoCodec}` rows searched with
  `std::ranges::find_if`, NOT a switch (SonarQube caps switch cases at 30). A
  `static_assert(size == std::to_underlying(RV40) + 1)` restores the exhaustiveness a
  `switch`-without-`default` gave: adding a `VideoCodec` enumerator without a mapping is a build
  error, not a silent `nullopt`. `ui::video_codec_name` mirrors this pattern for display names.
- Supported codecs (`VideoCodec`): H.264, HEVC (native); AV1 via the already-vendored libaom
  as FFmpeg's `libaom-av1` decoder (FFmpeg's own native "av1" decoder is a hwaccel-dispatch
  shim only — no software decode); QTRLE, Cinepak (native `.mov`); Phase 52 additions
  (see map_codec_id above). Tier-2 (decode unverified — no system encoder, real-file test deferred
  to release): wmv3/vc1, svq3, rv30/rv40, vp6/vp6a/vp6f, msmpeg4v1, cook, DV.
- `volume_setting.*` / `loop_setting.*` — process-global in-memory volume + loop-toggle
  state (NOT AV-gated): `saved_volume()`, `saved_loop_enabled()`/`set_saved_loop_enabled()`.
  Volume persists via `platform::VolumePref`; loop is process-lifetime only.

### frame_convert.{h,cpp} — FrameConverter
swscale-based YUV->I420 conversion shared by VideoDecoder + VideoDecodeWorker: `zero_copy()`
for already-I420/NV12 frames, `to_i420()` otherwise, cached `SwsContext` reused per stream.
`copy_owned_frame()` copies a DecodedFrame's planes into a caller-owned
`std::vector<uint8_t>` for safe cross-thread handoff (FFmpeg's internal AVFrame buffers are
unsafe to alias once the next decode call can run concurrently).

**Phase 52 addition:** deinterlacing via yadif (Yet Another Deinterlacing Filter). `should_deinterlace(int flags)`
predicate (conditional on `AV_FRAME_FLAG_INTERLACED` in frame flags). Lazily-built cached avfilter
graph (buffer → yadif mode=0 → buffersink) hooked in `publish_decoded_frame()` after hardware-transfer
if present. Graph is per-instance, rebuilt when source geometry (width/height/pix_fmt) changes, freed on
`reset()`/destruction. Built by `build_deint_graph()`; every failure path routes through
`fail_deint_graph()` (warn + `free_deint_graph()` + return false), and all warnings go through
`deint_warn_once()`, so a persistently broken graph logs once rather than once per frame. Cached
geometry is committed only AFTER the graph fully builds, so a half-built graph never looks valid.
On error or EAGAIN returns nullptr and the caller shows the original frame once.

**Two FFmpeg-API invariants — both were violated in the original Phase 52 implementation:**
- Feed frames with `av_buffersrc_write_frame()` (takes `const AVFrame*`, keeps the caller's
  reference). NEVER `av_buffersrc_add_frame()`: it takes ownership of the frame's references and
  blanks the caller's frame, which silently breaks the EAGAIN fallback — the caller then displays
  an emptied frame instead of the original. Its non-const signature is also what tempts a
  `const_cast` here; `write_frame` needs none.
- `av_frame_unref(deint_out_)` before EVERY `av_buffersink_get_frame()`: it move-refs into the
  target without unreferencing first, so omitting the unref leaks one full frame buffer per
  deinterlaced frame. Identical failure mode to `to_i420()`'s `conv_` unref.
Both have dedicated regression tests in `tests/media/test_frame_convert.cpp` (the leak one is a
12-frame loop that shows up under ASAN/valgrind, not as an assertion).

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

### Video extension whitelist
`src/ui/video_exts.h` (Phase 52) — unified single-source `constexpr std::array<std::string_view, 16> kVideoExts`.
Modern (5): mp4, m4v, mov, webm, mkv. Legacy (11, Phase 52): avi, mpg, mpeg, wmv, asf, flv, ts, m2ts, ogv, rm, rmvb.
Consumed by `is_supported_media_name()` in `zip_plan.cpp` (archive import) and `meta_format.cpp` (vault import),
and fed into the file-dialog filter in `file_dialog.cpp`. Lookup semantics: lowercase, no dot prefix.
Before Phase 52, these lists were duplicated verbatim in two places (zip_plan.cpp:130 + meta_format.cpp:107);
Phase 52 consolidated them to prevent divergence.

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
