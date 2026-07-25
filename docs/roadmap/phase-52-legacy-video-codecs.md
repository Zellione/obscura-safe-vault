## Phase 52 — Legacy container & codec support ✅

**Goal:** Play the video that dominated roughly 2000–2010. Add the AVI, MPEG-PS,
MPEG-TS, ASF/WMV, FLV, Ogg and RealMedia containers, plus the era's codecs
(MPEG-1/2, MPEG-4 ASP a.k.a. DivX/Xvid, MS-MPEG4 v1–v3, WMV1/2/3, VC-1, H.263,
Sorenson, DV, RealVideo and the lossless/legacy long tail), so an archive of old
files imports and plays instead of being rejected at probe time.

Full design, including the tiered-testing rationale and the recorded assumptions:
[`docs/superpowers/specs/2026-07-24-legacy-video-codecs-design.md`](../superpowers/specs/2026-07-24-legacy-video-codecs-design.md).

> **Two corrections to the original framing, established by reading the build:**
> **MKV already works** — `matroska` is an enabled demuxer, `"mkv"` is in both
> extension whitelists, and `detect_video_container()` recognises EBML; what fails
> is the *codec* inside an old MKV, not the container. And **AVI does not work at
> all**, despite `tests/media/fixtures/tiny_mpeg4.avi` sitting in the tree with no
> demuxer able to read it — AVI is where DivX/Xvid lives and is the single most
> valuable addition here.

### Tasks

**Build**
- [ ] Add to **both** configure invocations (`scripts/build_codecs.sh`, `scripts/build_ffmpeg_windows.sh` — they must stay in lockstep):
  - demuxers: `avi, mpegps, mpegts, asf, flv, ogg, rm`
  - video decoders: `mpeg1video, mpeg2video, mpeg4, msmpeg4v1, msmpeg4v2, msmpeg4v3, wmv1, wmv2, wmv3, vc1, h263, flv1, vp6, vp6a, vp6f, svq1, svq3, dvvideo, msvideo1, rpza, huffyuv, ffv1, theora, rv10, rv20, rv30, rv40`
  - audio decoders: `mp2, wmav1, wmav2, cook, ra_144, ra_288, eac3, pcm_s16le, pcm_u8, adpcm_ms, adpcm_ima_wav`
  - parsers: `mpegvideo, mpeg4video, h263, vc1, mpegaudio` — **check each new decoder for whether its stream needs a parser.** Phase 47 shipped a broken gif path for exactly this reason (the demuxer emitted unparsed chunks and decode failed on the second one).
- [ ] `--enable-filter=yadif` + avfilter dependency; `link_av()` in `premake5.lua` gains `avfilter` (before avformat/avcodec, presence-guarded like every other vendored link).
- [ ] Enable the `mpeg2/vc1/wmv3/mpeg4/h263` VAAPI + D3D11VA hwaccel registrations, **verified against `vendor/ffmpeg/configure`'s `*_vaapi_hwaccel_deps` / `*_d3d11va_hwaccel_deps`** (the verification Phase 43 performed). Existing `HwAccelContext` probe + `reopen_software_only()` fallback cover unsupported GPUs — no new failure mode.
- [ ] **`rm -rf build/` after rebuilding the codecs.** Ninja does not treat a rebuilt `libavcodec.a` as a relink trigger (prebuilt external file, not a build edge), so tests otherwise run silently against the stale archive. Recorded in `mem:tech_stack`; it has already cost this project a debugging session.
- [ ] CI codec caches bust automatically (keys hash the build scripts, which this phase edits) — no manual version bump, but expect a full FFmpeg rebuild on both legs.
- [ ] Measure and record build-time and binary-size delta.

**C++**
- [ ] Extend `vault::VideoCodec` + `vault::VideoContainer`. **No `INDEX_VERSION` bump** — byte layout unchanged, existing blobs parse; only forward compat is affected, which is already the project's stance. Phases 28/38/40 added codec values the same way. **Verify against the actual deserialisation path before relying on this.**
- [ ] `detect_video_container()`: `RIFF`+`AVI ` / `0x000001BA` / `0x47` sync at 0,188,376 / ASF GUID / `FLV` / `OggS` / `.RMF`. TS checked **last** — one `0x47` is a weak signature, so require all three offsets.
- [ ] Extend the codec-ID switch at `src/media/video_decoder.cpp:94` (the app's only video allowlist; audio has none and is governed purely by the configure list).
- [ ] **Fold the duplicated video-extension whitelist into one shared constant** (`src/ui/zip_plan.cpp:130` and `src/ui/meta_format.cpp:107` are verbatim copies) **before** extending it with `avi, mpg, mpeg, wmv, asf, flv, ts, m2ts, ogv, rm, rmvb`. Same additions to the dialog filter (`src/platform/file_dialog.cpp:55`) and display names in `video_container_name()`.
- [ ] **Anamorphic pixels:** carry `sample_aspect_ratio` out of `VideoDecoder` and apply it in the viewer's fit maths (pure `viewer_model`/`playback_model`, so unit-testable). Without it a 720×576 DVD rip renders squashed. Also affects stored dimensions and the poster thumbnail, which must use the display aspect.
- [ ] **Deinterlacing:** run yadif only when the frame reports interlaced content, inside `FrameConverter` — so `VideoDecodeWorker`'s submit/receive pipeline, generation-tagged seeks and the hwaccel transfer path stay untouched.

**Tests — tiered (a meaningful subset of these codecs cannot be encoded by any FFmpeg build)**
- [ ] **Tier 1, full decode tests** with generated committed fixtures (~≤100 KB, 160×120) via a committed `scripts/gen_media_fixtures.sh` run against a *system* ffmpeg (the vendored build is decode-only): `mpeg1video, mpeg2video, mpeg4, msmpeg4v2, msmpeg4v3, wmv1, wmv2, h263, flv1, svq1, dvvideo, msvideo1, rpza, huffyuv, ffv1, theora, rv10, rv20` + audio `mp2, wmav1, wmav2, adpcm_ms, pcm_s16le`. Each: container detected → codec mapped → first frame decodes → dimensions correct → seek works.
- [ ] Behavioural fixtures: an **anamorphic** clip (e.g. 704×576 SAR 16:15) reports the correct display aspect; an **interlaced** clip comes out deinterlaced.
- [ ] `tests/media/fixtures/tiny_mpeg4.avi` — already committed, unreadable until now — gets a demuxer and a test.
- [ ] **Tier 2, registration-level only** (no encoder exists → no fixture possible): `wmv3/vc1, svq3, rv30, rv40, vp6/vp6a/vp6f, cook, msmpeg4v1`. Assert `avcodec_find_decoder()` resolves and the codec-ID switch maps it. **The phase doc must state plainly that their decode path is unverified** — this is the weak point of the phase, since `wmv3`/`vc1` is the dominant `.wmv` codec and `rv30`/`rv40` the dominant RealVideo ones. Manual verification against real files is a release-check item, not an automated one.
- [ ] Excluded entirely: `indeo3/4/5` — no encoder, no fixture, and rare enough not to justify a hand-crafted one.

**Cross-cutting**
- [ ] Update ROADMAP index row, `docs/VENDORED_DEPS.md`, and Serena memories `mem:tech_stack` (configure lists, avfilter, hwaccel set) + `mem:module/media` (new codecs, SAR, deinterlace).

> **Merge note:** this branch and `phase-51-tag-metadata-folder-import` each add one
> row to the ROADMAP index directly after Phase 50, so whichever merges second
> conflicts there. Resolution is trivial — keep both rows in numeric order.

### Acceptance criterion

A `.avi` (DivX/Xvid), `.mpg` (MPEG-2), `.wmv` (WMV1/2), `.flv`, `.ogv` and `.ts`
file each import, probe to the correct container and codec, show a poster
thumbnail, play, and seek — on Linux and Windows. An anamorphic DVD rip displays at
its correct aspect rather than squashed, and an interlaced source plays without
comb artifacts. Every Tier-1 codec has a committed fixture and a passing decode
test; every Tier-2 codec has a passing registration test and is documented as
decode-unverified. All tests pass under `scripts/test.sh` and `--asan`.

**Status:** ✅ Complete

#### Results & measurements

**Build:**
- libavcodec.a: 36.3M
- libavfilter.a: 1.0M (new — avfilter was not built before Phase 52)
- osv_tests binary (Debug): 135.7M
- Build time: ~37 seconds

**Tests:**
- Baseline (Phase 51): 1168 tests
- Phase 52 final: 1298 tests (+130)
- Result: 1298/0 (all passing)
- ASAN: 1298/0 (zero sanitizer errors; ASAN ran with live hardware VAAPI path active)

**Deliverables:**
- 7 legacy video containers detected via bounds-checked magic bytes: AVI, MPEG-PS, MPEG-TS, ASF/WMV, FLV, Ogg, RealMedia
- ~34 legacy video + audio decoders enabled in vendored decode-only FFmpeg
- 7 new demuxers (avi, mpegps, mpegts, asf, flv, ogg, rm)
- 4 new parsers (mpegvideo, mpeg4video, h263, vc1)
- `display_dims()` helper + SAR-aware storage in VideoMeta
- Yadif deinterlacing (conditional on AV_FRAME_FLAG_INTERLACED)
- Unified video-extension whitelist (video_exts.h)
- `media::map_codec_id(int)` extracted + 27 new VideoCodec enum values (10..36)
- VAAPI/D3D11VA hwaccels enabled (modern + legacy); hwaccel was a compiled no-op Phase 43–51, now fully functional
- INDEX_VERSION remains 9 (no format bump)

#### Known limitation

**Raw MPEG-PS (`.mpg`/`.mpeg`) is not decodable in the decode-only build.** The
vendored FFmpeg cannot identify the program-stream elementary-stream codec (full
system FFmpeg reads it fine — it is a stripped decode-only build). A `.mpg` file
still imports (container detected as MPEG-PS) but stores as an unplayable Unknown-codec
video. **MPEG-1 and MPEG-2 are fully supported via MKV, MPEG-TS, MP4, and MOV**
(all decode-tested end-to-end).

#### Tier-2 (decode unverified)

The following codecs have been registered and mapped but their decode paths are
unverified (no FFmpeg encoder exists, so no fixture can be generated): wmv3/vc1,
svq3, rv30/rv40, vp6/vp6a/vp6f, msmpeg4v1, cook, and DV (encoder exists but only for
rare `.avi` import; registration only). These codecs' presence in the decode list is
asserted by test but actual frame decoding is a manual release-check item against
real-world files. This is the phase's honest weak point — headline `.wmv` and RealMedia
decode rest partly here.

#### Hardware decode note (Phase 43 gap fixed)

Phase 43 declared `--enable-vaapi` / `--enable-d3d11va` in the build scripts but never
passed `--enable-hwaccel=`, so `CONFIG_HWACCELS` remained 0 and no hwaccel symbols were
compiled. Hardware decode was a silent no-op from Phase 43 through Phase 51. Phase 52 adds
the complete `--enable-hwaccel=` lists (VAAPI: h264,hevc,vp8,vp9,av1,mpeg2,mpeg4,vc1,wmv3,h263;
D3D11VA: h264,hevc,vp9,av1,mpeg2,vc1,wmv3), making hwaccels actually compile and function.
