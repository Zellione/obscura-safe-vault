# Hardware-accelerated video decode — design spec

**Date:** 2026-07-17
**Phase:** 43 (roadmap)
**Status:** Implemented (Part 1 + Part 2)

## Problem

Phase 41 moved codec-level video decode off the render thread onto
`media::VideoDecodeWorker` so a slow *software* codec (AV1/HEVC) no longer
stalls playback/input — but the decode itself is still 100% CPU. Where the
platform exposes a dedicated video decode block (Intel/AMD/Nvidia GPUs, most
laptops/desktops built in the last decade), handing codec-level decode to it
instead of the CPU cuts power/CPU usage and helps headroom-starved playback
(a laptop on battery, several video tiles decoding posters at once).

**Hardware decode is strictly opportunistic, never a hard requirement.**
Phase 41's pure-software `VideoDecodeWorker` path is the permanent fallback —
used automatically and silently whenever hardware decode isn't available (no
GPU decode block, missing/outdated driver, codec/profile the hardware can't
handle, or the platform build simply doesn't have this feature compiled in).
No code path may make playback depend on hardware decode succeeding, and no
code path may make the *application* depend on a hardware-decode-only system
library being present (see the VAAPI linking discussion below — this is the
concrete reason Part 2 needs a dlopen shim rather than a direct system link).

**Scope is deliberately narrow: only the continuous-playback decode path.**
`VideoDecoder`'s one-shot `next_frame()`/`seek()` (posters, thumbnails, the
existing synchronous test suite) stays software-only — a single decoded frame
isn't worth a hardware device context's setup/teardown cost, and keeping that
path untouched means Phase 41's proven synchronous behavior for
posters/thumbnails needs no revalidation.

**Frames stay in system memory — no GPU-texture interop with SDL_Renderer.**
After a hardware-decoded frame is produced, it's transferred back into system
memory (`av_hwframe_transfer_data()`) into the *same* `DecodedFrame` /
`media::FrameConverter` / `gfx::YuvTexture` pipeline Phase 41 already built.
This phase only changes *where the codec math runs*, not how a decoded frame
reaches the screen.

## Codec coverage (confirmed against vendored FFmpeg 7.1.1 source, not assumed)

Grepping `vendor/ffmpeg/configure` for `*_vaapi_hwaccel_deps`/`*_d3d11va_hwaccel_deps`
against the decoders this project already enables:

| codec | VAAPI | D3D11VA |
|---|---|---|
| h264 | yes | yes |
| hevc | yes | yes |
| vp8 | yes | no |
| vp9 | yes | yes |
| mjpeg | yes | no |
| av1 (`libaom_av1`) | no | no |
| prores / dnxhd / qtrle / cinepak | no | no |

AV1 gets nothing: the project's AV1 decode uses `libaom_av1` (a *software*
decoder wrapping the vendored libaom, per Phase 40's gotcha), not FFmpeg's
native `av1` decoder, which is the only one with a hwaccel path. This matches
the roadmap doc's suspicion and closes out Task 1 ("spike: confirm hwaccel
wiring per already-shipped decoder") without further investigation needed.

## VAAPI linking: why dlopen, not a system link

FFmpeg's `libavutil/hwcontext_vaapi.c` calls 23 `va*` functions directly
(`vaInitialize`, `vaCreateSurfaces`, `vaGetDisplayDRM`, `vaSyncSurface`, …).
Once `--enable-vaapi` is passed to FFmpeg's configure, this translation unit
is unconditionally compiled into `libavutil.a` (FFmpeg's hwcontext dispatch
table references it at compile time, not behind a runtime flag) — so those 23
symbols must resolve at **our own app's final link step**, regardless of
whether hardware decode is ever exercised at runtime.

Linking directly against a system `libva.so.2` (`-lva`) would give the
produced Linux binary a `DT_NEEDED` entry on `libva.so.2` — the app would
fail to **launch**, not just fail to hw-decode, on any Linux machine without
libva installed. That's a materially bigger blast radius than "opportunistic
hw decode," breaks the project's vendored/offline-build philosophy
(`docs/VENDORED_DEPS.md`), and would add a mandatory install prerequisite for
every Linux user regardless of whether they even have a supported GPU.

**Decision: a small dlopen shim.** A thin static library (exact location is a
Part 2 implementation-plan decision, see below) provides the same 23
symbols FFmpeg's `hwcontext_vaapi.c` needs, implemented as `dlopen("libva.so.2")`
+ `dlsym()` forwarding, returning a "not available" error path if the dlopen
fails. This satisfies the static link unconditionally (so the build always
succeeds) while keeping the real `libva.so.2` dependency 100% optional at
runtime — no `DT_NEEDED` entry, silently unavailable if absent, exactly
matching the pattern `link_av()`/`link_archive()` already use for "linked
only when present." Building `hwcontext_vaapi.c` itself (and FFmpeg's own
`configure` link-probe for it) still needs libva's real headers
(`va/va.h`, `va/va_drm.h`) present at **build** time — these are vendored as
headers only (permissively licensed), not libva's implementation or its
driver-loading machinery.

This is Part 2's core deliverable and is scoped separately from Part 1 below.

## Architecture (shared by both parts)

### `media::HwAccelContext`

A new component beside `VideoDecodeWorker`, gated by
`#if defined(OSV_HWACCEL_D3D11VA) || defined(OSV_HWACCEL_VAAPI)` — compiles to
an always-unavailable stub when neither is defined, so `VideoDecodeWorker`
never needs per-platform `#ifdef`s at its call sites (the same
"always compiles, does nothing without the capability" shape `link_av()`
already establishes one layer up, for `OSV_VENDORED_AV` itself).

- **Process-wide singleton**, lazily created on first `VideoDecodeWorker`
  construction. Attempts `av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_D3D11VA` or
  `AV_HWDEVICE_TYPE_VAAPI, ...)` **exactly once per process**, caching the
  resulting `AVBufferRef*` (or the failure) — mirrors
  `should_warn_mlock_once()`'s cache-the-outcome pattern (`CLAUDE.md`
  hardening notes) for a best-effort platform capability. No retry per clip.
- Each `VideoDecodeWorker` wanting hw decode takes its own `av_buffer_ref()`
  of the cached device context when opening its `AVCodecContext`, and installs
  a `get_format` callback selecting the hw pixel format
  (`AV_PIX_FMT_D3D11`/`AV_PIX_FMT_VAAPI`) if the decoder offers one for that
  stream, falling back to the software format list otherwise — this is
  FFmpeg's existing hwaccel dispatch mechanism, not a separate branch this
  project writes.
- **Per-clip fallback, not just per-process.** If `avcodec_send_packet`/
  `avcodec_receive_frame` hard-fails with a hw context active,
  `VideoDecodeWorker` drops it and reopens a fresh software-only codec context
  for the rest of that clip — playback continues, never aborts.
- A successfully hw-decoded frame is transferred to system memory via
  `av_hwframe_transfer_data()` immediately, before it reaches
  `FrameConverter`/`copy_owned_frame()` — everything downstream
  (`decode_available_frames()`, `publish_decoded_frame()`, the
  `VideoPlayback::Impl` consumer side) is byte-for-byte unmodified from Phase
  41.

### Test-only failure injection

A free function `media::test_only_force_hwaccel_unavailable(bool)`, matching
the existing `vault::test_only_force_video_codec_unknown` shape —
`HwAccelContext` checks it before attempting real device creation. Lets tests
deterministically exercise the fallback path even though CI runners have no
real GPU decode block. Built in Part 1, reused unchanged by Part 2.

## Part 1 — shared infrastructure + Windows D3D11VA

**Build:**
- `scripts/build_ffmpeg_windows.sh`: add `--enable-d3d11va` to the existing
  decode-only configure invocation — a hwaccel dispatch-registration flag,
  additive to the existing `--enable-decoder=...` list. No new dependency:
  ships with the Windows SDK the MSVC toolchain already needs.
- `premake5.lua`: new `OSV_HWACCEL_D3D11VA` define under
  `filter "system:windows"`, gated on `OSV_VENDORED_AV` already being on —
  same "define only when the underlying capability is present" pattern
  `link_av()` uses for `OSV_VENDORED_AV` itself.
- Linux stays exactly Phase 41 behavior in Part 1 — no `--enable-vaapi`, no
  `OSV_HWACCEL_VAAPI`, no shim yet.

**Code:** `media::HwAccelContext` (new files) + `VideoDecodeWorker`
constructor/decode-loop changes to use it, following the Architecture section
above. D3D11VA is the only concrete backend wired up.

**Settings toggle:** deferred (both parts) — the roadmap doc marks the
force-software-decode toggle "nice-to-have, not blocking"; YAGNI applies
until there's a real driver-troubleshooting need.

**Tests:** `VideoDecodeWorker` gains (1) a case asserting byte-identical
`DecodedFrame` output whether hw device creation succeeds or is forced to
fail via the injection hook, (2) the forced-failure fallback case itself.
Existing Phase 41 async-decode tests must pass unmodified. `scripts/test.sh`
and `--asan` are the acceptance gate — CI has no real GPU, so the
forced-injection path is what's actually exercised there.

**Docs:** update `CLAUDE.md` tech table, `mem:tech_stack`, `mem:core` with the
new build define, the codec-coverage table above, and the fallback behavior.

## Part 2 — VAAPI dlopen shim + Linux enablement (follow-up PR)

- The dlopen shim library (see "VAAPI linking" above): 23 forwarded `va*`
  symbols, vendored libva headers (headers only), built ahead of FFmpeg in the
  `scripts/build_codecs.sh` pipeline so FFmpeg's own `configure` link-probe
  for `--enable-vaapi` succeeds against it.
- `scripts/build_codecs.sh`: add `--enable-vaapi` to FFmpeg's configure
  invocation, pointed at the shim.
- `premake5.lua`: new `OSV_HWACCEL_VAAPI` define under
  `filter "system:linux"`, same presence-gating pattern as Part 1's
  `OSV_HWACCEL_D3D11VA`.
- `media::HwAccelContext` gains the VAAPI backend (device creation via
  `AV_HWDEVICE_TYPE_VAAPI`, DRM render-node path — no X11 dependency).
- Tests/docs mirror Part 1's additions for the VAAPI path.

**Implementation note:** the shim forwards **36** `va*` symbols, not the
document's original "~23" estimate — the real count, once
`libavcodec/vaapi_{decode,h264,hevc,vp8,vp9,mjpeg}.c`'s own direct calls are
included (not just `libavutil/hwcontext_vaapi.c`), confirmed by grepping the
pinned `vendor/ffmpeg` source directly. `va_str.h`/`va_drmcommon.h` turned
out unnecessary to vendor: neither is `#include`d given this project's
configure flags (no `CONFIG_LIBDRM`, a separate/unrelated feature this
project never enables). `vaGetDisplay` (the X11 variant) and
`vaGetDisplayWin32` are excluded — only the DRM winsys backend
(`vaGetDisplayDRM`) is wired up, matching "no X11 dependency" above.

Exact shim file layout and the precise libva header subset to vendor are
implementation-plan decisions for Part 2, made once Part 1's
`HwAccelContext`/`VideoDecodeWorker` shape is settled and reviewed.

## Acceptance criterion (both parts' shared gate)

`scripts/test.sh` and `scripts/test.sh --asan` both green on every platform,
exercising the forced-fallback path (real hardware isn't available in CI).
**Manual verification on real hardware** (documented separately, not
CI-gated): on a Windows box with a D3D11VA-capable GPU (Part 1) and later a
Linux box with a VAAPI-capable GPU (Part 2), play a clip using a codec from
the coverage table above, and confirm via a platform GPU-usage tool
(Task Manager's GPU decode graph / `intel_gpu_top`/`radeontop`) that decode is
actually running on the hardware block, with no playback regression
(pause/seek/volume/A-V sync) versus Phase 41's software path.

## Out of scope (YAGNI)

- GPU-resident texture interop with SDL_Renderer (frames still land in system
  memory, unchanged from Phase 41).
- NVDEC/NVENC or other vendor-specific decode APIs beyond VAAPI/D3D11VA —
  VAAPI already covers Nvidia via nouveau on Linux; a proprietary-driver fast
  path can be revisited if a real fixture proves the need.
- Hardware-accelerated *encoding* (this app has no encode path at all —
  decode-only, `mem:tech_stack`).
- macOS/VideoToolbox (platform not supported).
- Forcing hardware decode without a software fallback, or surfacing hw decode
  failures as playback errors — a GPU without driver/hardware support for a
  given codec must be indistinguishable from Phase 41's existing behavior to
  the user.
- The force-software-decode settings toggle (deferred, both parts — see
  above).
- A hard system link against libva on Linux (rejected above — breaks the
  "opportunistic, never a hard requirement" invariant for every Linux user,
  not just ones without a supported GPU).
