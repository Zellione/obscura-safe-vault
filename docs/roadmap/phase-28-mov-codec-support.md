## Phase 28 — Broaden `.mov` / video codec support 🔜

**Goal:** Decode the codecs commonly found in `.mov` containers beyond H.264/H.265.
The `.mov` container, `mov` demuxer, and `ftyp` detection already work — the gap is
files whose video stream uses a codec the vendored FFmpeg doesn't currently decode.

### Tasks
- [x] **Add FFmpeg decoders** — extend the `--enable-decoder` set in `scripts/build_codecs.sh` with the codecs common in `.mov`: **ProRes** (`prores`), **DNxHD/DNxHR** (`dnxhd`), **MJPEG** (`mjpeg`) + the `dnxhd`/`mjpeg` parsers (FFmpeg has no prores parser; the mov demuxer frames its packets). The build stays **decode-only** (no encoders/muxers). `build_codecs.bat` is untouched — Windows never builds FFmpeg (video is `OSV_VENDORED_AV`-gated to Linux/macOS).
- [x] Confirmed no format-detection change is needed — `vault::detect_video_container` maps any `ftyp` box to the ISO-BMFF/MP4 path (the probe tests assert `container == MP4` for the `.mov` fixtures), and `.mov` was already in the import dialog filter.
- [x] Size impact measured: `libavcodec.a` 19.1 → 20.2 MiB (+1.0 MiB); linked Release `osv` 16.44 → 16.62 MiB (+172 KiB, ~1%).
- [x] Update `CLAUDE.md` (FFmpeg decoder list) + the README stack line (+ `docs/VENDORED_DEPS.md`, `mem:tech_stack`).
- [x] `tests/` — small `.mov` fixtures (`tiny_prores.mov`, `tiny_dnxhr.mov`, `tiny_mjpeg.mov` — 160/256×120, 10 frames, generated with ffmpeg `testsrc`) probe with poster + full decode through the encrypted-chunk path; gated behind `OSV_VENDORED_AV`.

**Out of scope (YAGNI):** encoding/transcoding; professional codecs beyond the above (CineForm, DNxHR HQX variants) unless a real fixture proves the need; new audio codecs.

### Acceptance criterion
A ProRes (and an MJPEG) `.mov` imports, probes, shows a poster, and plays; existing
H.264/H.265 playback and A/V sync are unchanged.

**Status:** ✅ 685/685 tests pass; `scripts/test.sh --asan` clean. `VideoCodec` gains `ProRes`/`DNxHD`/`MJPEG` (raw-u8 index field — no format bump; older builds show new values as plain "Video"), `VideoDecoder::open` maps the three FFmpeg codec ids instead of rejecting them, and `video_codec_name` labels them in the viewer metadata. Their native pixel formats (yuv422p10le / yuv422p / yuvj422p) flow through the existing swscale → I420 conversion, so playback and A/V sync are untouched.
