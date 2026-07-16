## Phase 16 — Audio & A/V sync ✅

**Goal:** Add the audio track to the video pipeline with proper
audio/video synchronisation.

### Tasks
- [x] **Audio decode** — extend `src/media/video_decoder` to decode the audio track (AAC/Opus/MP3/Vorbis/FLAC/AC3; FFmpeg build enables those decoders) into PCM frames.
- [x] **Audio output** — route decoded PCM to an `SDL_AudioStream`; SDL converts to device format as needed.
- [x] `src/media/av_sync.{h,cpp}` — pure, unit-tested sync logic: presentation timestamps tracked against the **audio clock**, with video frame drop/hold decisions returned as data (no SDL in the unit).
- [x] **Viewer controls** — volume + mute (`M`, `[`/`]`); the seek bar now seeks both tracks; pause stops audio cleanly.
- [x] **Memory hygiene** — decoded audio PCM is treated like decoded pixels (transient buffers, no disk); the streaming AVIO path is unchanged.
- [x] `tests/` — the audio decoder produces the expected sample count for a known clip; `av_sync` holds/drops frames correctly for fabricated PTS streams (drift ahead/behind); seek re-aligns both tracks; volume/mute math; headless no-disk-write assertion.

### Acceptance criterion
A short H.264+AAC clip plays with synchronised audio and video, seeks both
tracks correctly, and volume/mute/pause behave correctly — still with no
decrypted bytes written to disk.

**Status:** ✅ 390 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean). FFmpeg build enables six audio decoders (aac, opus, mp3, vorbis, flac,
ac3). `VideoDecoder` owns a shared demuxer feeding both video and audio via
per-stream packet queues; `AudioDecoder` decodes planar→interleaved F32; audio
samples flow into an `SDL_AudioStream` (which handles rate/format/channel
conversion — we do NOT use swresample for our resampling). Audio-clock
synchronisation via pure `av_sync::decide(audio_clock, frame_pts, ...)` drives
frame Present/Hold/Drop decisions. `VideoPlayback` hosts a paused-capable
`SDL_AudioStream` (master clock), mute/volume via `SDL_SetAudioStreamGain`,
controls `M`/`[`/`]`. Seek flushes both tracks and re-aligns. A headless
assertion verifies zero disk writes across open→play→seek→play.
