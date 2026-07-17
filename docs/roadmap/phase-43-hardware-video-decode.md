## Phase 43 — Platform hardware-accelerated video decode 🔜

**Goal:** Phase 41 moved codec-level video decode off the render thread onto
`media::VideoDecodeWorker` so a slow *software* codec (AV1/HEVC) no longer
stalls playback/input — but the decode itself is still 100% CPU (Phase 40's
roadmap explicitly deferred "AV1 hardware acceleration" as YAGNI at the
time). This phase attacks the cost itself: where the platform exposes a
dedicated video decode block (Intel/AMD/Nvidia GPUs, most laptops/desktops
built in the last decade), hand codec-level decode to it instead of the CPU,
cutting power/CPU usage and headroom-starved playback (e.g. a laptop on
battery, or several video tiles decoding posters at once).

**Hardware decode is strictly opportunistic, never a hard requirement.**
Phase 41's existing pure-software `VideoDecodeWorker` path is the permanent
fallback — used automatically and silently whenever hardware decode isn't
available (no GPU decode block, missing/outdated driver, codec/profile the
hardware can't handle, or the platform build simply doesn't have this
feature compiled in). No code path may make playback depend on hardware
decode succeeding.

**Scope is deliberately narrow: only the continuous-playback decode path.**
`VideoDecoder`'s one-shot `next_frame()`/`seek()` (posters, thumbnails, the
existing synchronous test suite) stays software-only — a single decoded
frame isn't worth a hardware device context's setup/teardown cost, and
keeping that path untouched means Phase 41's proven synchronous behavior for
posters/thumbnails needs no revalidation.

**Frames stay in system memory — no GPU-texture interop with SDL_Renderer.**
After a hardware-decoded frame is produced, transfer it back into system
memory (`av_hwframe_transfer_data()`) into the *same* `DecodedFrame` /
`media::FrameConverter` / `gfx::YuvTexture` pipeline Phase 41 already built.
Keeping a decoded frame resident on the GPU and handing SDL_Renderer a
foreign texture handle would be a much larger, per-backend undertaking
(SDL_Renderer's external-texture support is backend-specific and not
uniformly documented) — out of scope here. This phase only changes *where
the codec math runs*, not how a decoded frame reaches the screen.

### Platform mechanism (FFmpeg hwaccel)

FFmpeg exposes hardware decode via an `AVHWDeviceContext` set on
`AVCodecContext::hw_device_ctx`, plus a `get_format` callback on the codec
context that picks a hardware pixel format if the decoder offers one for
that stream (falls back to the software format list otherwise — this is the
mechanism, not a separate flag). Two platform-specific backends apply here:

- **Windows — D3D11VA.** Headers/libs ship with the Windows SDK already used
  to build the app (same toolchain MSVC already needs); no new vendored
  dependency, no system package to install on the CI runner or the user's
  machine.
- **Linux — VAAPI.** The standard non-vendor-specific route (works across
  Intel/AMD/some Nvidia via nouveau), but `libva` is a **system** library
  with per-GPU-vendor driver backends — this conflicts with the project's
  fully-vendored/offline-build philosophy (`docs/VENDORED_DEPS.md`,
  `mem:tech_stack`). **Open design question to resolve at implementation
  time:** `dlopen("libva.so...")` at runtime (build and run unchanged if
  `libva` isn't present — hw decode is just unavailable, matching the
  "linked only when present" pattern `link_av()`/`link_archive()` already
  use for `OSV_VENDORED_AV`/`OSV_VENDORED_ARCHIVE`) vs. hard-linking against
  a system `libva-dev` (adds a build prerequisite, closer to how `nasm` is
  already required) vs. vendoring `libva` itself as a submodule (it's
  Apache-2.0/MIT, but pulls in its own driver-loading machinery that
  expects to find vendor drivers on the host at runtime regardless of how
  it was built — vendoring the loader doesn't remove the runtime system
  dependency). Recommendation to validate first: runtime `dlopen`, since it
  preserves "build and run anywhere, hw accel silently unavailable if the
  system doesn't have it" without adding a hard build-time requirement.
- **macOS.** Not supported (dropped platform, `mem:tech_stack`) — no
  VideoToolbox work.

### Tasks
- [x] **Spike: confirm hwaccel wiring per already-shipped decoder.** Confirmed
  against `vendor/ffmpeg/configure`: h264/hevc/vp9 have both VAAPI and
  D3D11VA; vp8/mjpeg have VAAPI only; av1(`libaom_av1`)/prores/dnxhd/qtrle/
  cinepak have neither. See the design spec's codec-coverage table.
- [x] **FFmpeg configure flags** (`scripts/build_codecs.sh`,
  `scripts/build_ffmpeg_windows.sh`) — add `--enable-d3d11va` (Windows) /
  `--enable-vaapi` (Linux) to the existing decode-only configure invocation.
  These are hwaccel *dispatch* registration flags, additive to the existing
  `--enable-decoder=...` list (mirrors the `libaom_av1` naming gotcha
  `mem:tech_stack` already documents — verify the exact component names
  FFmpeg expects, don't assume). Windows (D3D11VA) done in Part 1 — Linux
  (VAAPI) done in Part 2 ✅.
- [x] **Build gating** (`premake5.lua`) — new defines
  (`OSV_HWACCEL_D3D11VA` / `OSV_HWACCEL_VAAPI`), each independently probed
  per-platform, following the existing "define only when the underlying
  capability is actually present" pattern `link_av()` uses for
  `OSV_VENDORED_AV`. A build without either must stay exactly Phase 41's
  behavior. `OSV_HWACCEL_D3D11VA` done in Part 1 — `OSV_HWACCEL_VAAPI` done
  in Part 2 ✅.
- [x] **`media::VideoDecodeWorker`** (`src/media/video_decode_worker.h/.cpp`)
  — in the constructor, attempt hw device context creation **once per
  process** (cache the outcome; don't retry per clip if the first attempt
  failed — mirrors `should_warn_mlock_once()`'s log-once pattern for a
  best-effort platform capability). If available, open the codec context
  with `get_format` selecting the hw pixel format; on any hard failure
  during `avcodec_send_packet`/`avcodec_receive_frame` with a hw context
  active, drop it and reopen a fresh software-only codec context for the
  rest of that clip — playback must continue, not abort. A successfully
  hw-decoded frame is transferred to system memory
  (`av_hwframe_transfer_data()`) before it reaches
  `FrameConverter`/`copy_owned_frame()`, so everything downstream
  (`decode_available_frames()`, `publish_decoded_frame()`, the
  `VideoPlayback::Impl` consumer side) is unmodified. Fully implemented,
  platform-agnostic — Part 2 only adds a second concrete backend, no
  further `VideoDecodeWorker` changes expected.
- [x] **Test-only failure injection** — a way to force hw device creation to
  fail deterministically in tests (CI runners generally have no real GPU
  decode hardware, or only a software rasterizer with no VAAPI/D3D11VA
  device at all), so the fallback path is exercised on every CI run even
  though real hardware decode can't be.
- [ ] **Optional settings toggle** — force-software-decode switch for
  troubleshooting a bad driver, mirroring the existing persisted
  loop/volume settings (`media::saved_loop_enabled()`/`saved_volume()`
  pattern). Nice-to-have, not blocking.
- [x] Update `CLAUDE.md` tech table + `mem:tech_stack` + `mem:core` (new
  build defines, the per-codec hwaccel-availability table, the fallback
  behavior).
- [x] `tests/` — `VideoDecodeWorker` gains a case asserting identical
  decoded output (same `DecodedFrame` bytes) whether hw device creation
  succeeds or is forced to fail, plus the forced-failure fallback case
  itself; existing Phase 41 async-decode tests must pass unmodified.

### Acceptance criterion
`scripts/test.sh` and `scripts/test.sh --asan` both green on every platform,
exercising the forced-fallback path (real hardware isn't available in CI).
**Manual verification on real hardware** (documented separately, not
CI-gated): on a Linux box with a VAAPI-capable GPU and on a Windows box with
a D3D11VA-capable GPU, play a clip using one of the codecs the Task-1 spike
confirms has a hwaccel path, and confirm via a platform GPU-usage tool
(`intel_gpu_top`/`radeontop`/Task Manager's GPU decode graph) that decode is
actually running on the hardware block, with no playback regression
(pause/seek/volume/A-V sync) versus Phase 41's software path.

### Out of scope (YAGNI)
- GPU-resident texture interop with SDL_Renderer (frames still land in
  system memory, unchanged from Phase 41).
- NVDEC/NVENC or other vendor-specific decode APIs beyond VAAPI/D3D11VA —
  VAAPI already covers Nvidia via nouveau on Linux; a proprietary-driver
  fast path can be revisited if a real fixture proves the need.
- Hardware-accelerated *encoding* (this app has no encode path at all —
  decode-only, `mem:tech_stack`).
- macOS/VideoToolbox (platform not supported).
- Forcing hardware decode without a software fallback, or surfacing hw
  decode failures as playback errors — safety-critical: a GPU without
  driver/hardware support for a given codec must be indistinguishable from
  Phase 41's existing behavior to the user.

**Status:** ✅ Done (Part 1 + Part 2).
