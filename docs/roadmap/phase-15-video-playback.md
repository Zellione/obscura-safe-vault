## Phase 15 — Video playback (frames + seek) ✅

**Goal:** Store video files in the vault and play their **video** track,
streaming decode directly from encrypted chunks with **no temp file**. Audio is
added in Phase 16.

### Tasks
- [x] **Vendor FFmpeg/libav** — decode-only static build (H.264/H.265 video; mov/mp4/m4v + matroska/webm demuxers; libswscale; encoders/muxers/protocols/network disabled). configure-built into `vendor/codecs-prefix` by `scripts/build_codecs.{sh,bat}`; `premake5.lua` `link_av()` links avformat/avcodec/swscale/avutil under `OSV_VENDORED_AV`. Needs **nasm** (like libaom).
- [x] **Encrypted-chunk streaming** — `src/media/chunk_avio.{h,cpp}` wraps `media::VideoSource` (decrypt-on-demand over `ChunkStore`, mlock'd one-chunk cache) in a read+seek `AVIOContext`. Seeks map a byte offset to the spanning chunk(s). **No bytes are ever written to a temp file** (fs-write assertion test).
- [x] **Index/format extension** — `IndexNode::Type::Video` + `VideoMeta` (container/codec/w/h/duration/orig_size/chunk list/poster), `INDEX_VERSION = 4` (v1–v3 read back-compat); `add_video` chunks the container + stores a first-frame JPEG poster; `read_thumbnail` returns the poster.
- [x] `src/media/video_decoder.{h,cpp}` — demux + H.264/H.265 decode → `DecodedFrame` (yuv420p/nv12 direct, swscale fallback); keyframe-anchored seek; `gfx::YuvTexture` streaming upload (`SDL_UpdateYUVTexture`/`UpdateNVTexture`).
- [x] **Viewer integration** — `src/ui/playback_model.{h,cpp}` (pure transport maths) + `src/ui/video_playback.{h,cpp}` (decoder + YUV texture + seek bar) hosted by `ImageViewer` when the current item `is_video()`: opens paused, `Space` play/pause, `J`/`L` ∓5 s, `,`/`.` frame-step, click/drag seek bar. Poster + play-badge on grid/list video tiles (`draw_tile_thumb`); list view shows duration + codec.
- [x] Update `CLAUDE.md` tech table (FFmpeg/libav, nasm) + `mem:tech_stack`/`mem:core`.
- [x] `tests/` — AVIO byte-exact read across chunk boundaries + SET/CUR/END/SIZE seek + **no-fs-write** assertion + auth-failure surfaced as `AVERROR(EIO)`; decoder frame-count/seek/swscale/malformed-reject; index v4 round-trip + v1–v3 back-compat; `add_video` reopen checksum + poster; transfer/search/favorites over video; `playback_model` transport maths; a headless `VideoPlayback` open→play→seek cycle that asserts **zero disk writes**.

### Acceptance criterion
✅ A short H.264 clip imported into the vault plays its video track, seeks
correctly, shows a poster thumbnail in the grid, and a test asserts that **no
decrypted bytes are ever written to disk** during playback.

### Delivered as 5 stacked PRs
1. Vendor FFmpeg (decode-only static build) (#28)
2. Video storage model — `Type::Video` + index v4 (#29)
3. Encrypted-chunk streaming — `VideoSource` + `chunk_avio` (#30)
4. Video decoder + YUV upload + poster/metadata (#31)
5. Viewer integration — `playback_model` + `video_playback` + grid/list wiring

**Status:** ✅ 379 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean). Video streams frame-by-frame from its encrypted chunks (custom AVIO over
`ChunkStore`, mlock'd decrypt cache) — no temp file, ever. `ImageViewer` hosts a
fit-only `VideoPlayback` (opens paused on frame 0; `Space`/`J`/`L`/`,`/`.` +
draggable seek bar); the grid/list show the poster with a play badge. Pure
transport maths live in the SDL-/FFmpeg-free `ui::playback_model`; the live
playback glue is verified headlessly with a no-disk-write assertion.
