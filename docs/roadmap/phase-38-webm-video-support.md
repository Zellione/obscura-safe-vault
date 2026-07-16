## Phase 38 — WebM video support (VP8/VP9) ✅

**Goal:** Import and play `.webm` video. The Matroska/WebM **container** side
already works today — `vault::detect_video_container` recognises the EBML
magic bytes as `MKV`, the FFmpeg build already enables the `matroska`/`webm`
demuxer, `.webm` is already in the import file-dialog filter, and Opus/Vorbis
(WebM's common audio codecs) are already enabled decoders. The actual gap is
the **video codec**: VP8/VP9 are not in the `--enable-decoder` set or the
`VideoCodec` enum, so a `.webm` today demuxes but its video stream fails to
decode. This phase mirrors the Phase 28 pattern (broaden `.mov` codec
support) for WebM's own pair of codecs.

### Tasks
- [x] **Add FFmpeg decoders** — extend `--enable-decoder` in
  `scripts/build_codecs.sh` with `vp8`, `vp9` (FFmpeg's native decoders —
  no `libvpx` dependency needed, same approach as the existing native
  `h264`/`hevc` decoders). Confirm no additional parser is required (the
  matroska demuxer packetizes VP8/VP9 frames itself — FFmpeg's own configure
  auto-selects a `vp9` parser and `vp9_superframe_split` bsf as internal
  decoder dependencies, no explicit `--enable-parser` addition needed).
  Decode-only, as with every other codec in this build. `build_codecs.bat`
  stays untouched (video remains `OSV_VENDORED_AV`-gated to Linux/macOS via
  `build_codecs.sh`, plus the existing Windows FFmpeg carve-out —
  `scripts/build_ffmpeg_windows.sh`, which mirrors the same decoder list and
  got the same `vp8`,`vp9` addition for consistency).
- [x] **`VideoCodec` enum** (`src/vault/index.h`) — add `VP8`/`VP9` values
  after `MJPEG` (raw `u8` index field, so older builds read new values as
  plain "Video" — no index format bump, same as Phase 28).
- [x] **`VideoDecoder::open` mapping** (`src/media/video_decoder.cpp`) — map
  `AV_CODEC_ID_VP8`/`AV_CODEC_ID_VP9` to the new enum values instead of
  rejecting them; `video_codec_name` labels them in the viewer metadata.
- [x] **Confirm audio path unchanged** — Opus/Vorbis decode already works
  (Phase 16); a `.webm` with either audio codec should already A/V-sync
  correctly once video decode is wired up. No new audio work expected, but
  verify with a fixture that has an audio track.
- [x] Update `CLAUDE.md` (FFmpeg decoder list, `VideoCodec` table) + the
  README stack line + `docs/VENDORED_DEPS.md` (if the decoder list is
  tracked there) + `mem:tech_stack`.
- [x] `tests/` — small `.webm` fixtures (VP8 and VP9, each with and without
  an Opus/Vorbis audio track — generated with ffmpeg `testsrc`) probe with
  poster + full decode through the encrypted-chunk path, gated behind
  `OSV_VENDORED_AV`; existing MP4/MOV/MKV(H.264/HEVC) fixtures stay
  byte-for-byte unchanged (no regression to the demuxer/container layer).

**Out of scope (YAGNI):** AV1-in-WebM (a separate codec already tracked for
still images via libaom in Phase 9 — video AV1 is a distinct decision);
encoding/transcoding; VP8/VP9 alpha-channel streams; `.mkv` containers
carrying other unsupported codecs (this phase is scoped to WebM's own
VP8/VP9 + Opus/Vorbis combination).

### Acceptance criterion
A VP8 `.webm` and a VP9 `.webm` (each with an Opus or Vorbis audio track)
import, probe, show a poster, and play with correct A/V sync; existing
MP4/MOV/MKV(H.264/HEVC/ProRes/DNxHD/MJPEG) playback is unchanged.

**Status:** ✅ Shipped.
