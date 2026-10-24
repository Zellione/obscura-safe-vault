# Phase 122G — VP8/WebM investigation and regression coverage

## Scope and diagnosis

Reported configuration: another Arch Linux PC, NVIDIA GPU, `nvidia-open`.
Software playback worked historically; the current affected file has not been
retested in software. No original failing media or GPU model/driver versions
were supplied. The sandbox has no `/dev/dri`. An unrestricted sanitizer run later reported a
successful VAAPI device probe and passed the automatic-path color checks; it
did not identify the GPU or prove hardware was selected for every frame. The
reported NVIDIA configuration and accelerated SDL presentation are unverified.
The generated fixture is a regression fixture, **not a reproduction of the
reported driver failure**. No blanket hardware disable or speculative driver
blacklist has been introduced. Target-machine remediation remains unverified.

The application requests FFmpeg VAAPI, not direct NVDEC. On NVIDIA the optional
VAAPI-to-NVDEC translation driver is a separate layer from `nvidia-open`.
Upstream advertises VP8 but warns that its Firefox-focused implementation may
not work correctly in other applications. Its v0.0.15 release includes PR #394,
“VP8: null slice headers”. This is a reason to inspect installed versions, not
evidence that this report has the same cause.

## Pipeline inspection

- `VideoDecoder` demuxes through custom in-memory AVIO and uses the native VP8
  software decoder for synchronous decoding/posters. Worker playback opens its
  own codec context and tries VAAPI when the runtime hardware preference permits.
- `transfer_hw_frame` unreferences its previous CPU frame before downloading;
  opaque hardware handles are never passed to software conversion on a failed
  download. Failed downloads currently skip frames. Successful but corrupt
  driver output is not detectable from the return code; automatic fallback on
  hard codec errors cannot promise to detect green pixels.
- `FrameConverter::zero_copy` preserves separate Y/U/V strides for I420 and
  interleaved UV for NV12. The worker immediately copies planes into owned
  `SecureBytes` before publishing; a later decoder unref cannot invalidate the
  published storage. `ffmpeg_secure` retains original buffer references and
  wipes at final release, protecting codec reference frames.
- `to_i420` uses padded `av_frame_get_buffer` storage; RGB posters use aligned
  rows plus a tail before copying to tight RGB. `YuvTexture::update` forwards
  each plane and its actual stride to SDL's I420/NV12 upload functions. GPU/SDL
  visual correctness on the reported PC is not established by source inspection.

## Regression assets and checks

`tests/media/fixtures/generate_vp8.py` generates original synthetic color bars
without outside imagery. The script and generated `vp8_colors_odd.webm` may be
redistributed under CC0-1.0. No user/vault content is used. Requires a development
FFmpeg executable with the libvpx encoder; the shipped application stays
**decode-only**. Normal tests use the committed fixture and need no encoder.

Generation: `python3 tests/media/fixtures/generate_vp8.py`.
Two successive runs with system FFmpeg n9.0.2 produced SHA-256
`97f27b9260c4bb9def411ef1b9ea6f3b63b5549473713f6abb9d6893546a6b2a`.
Encoder upgrades can change bytes; do not pin a cross-version encoding hash.
Fixture: VP8, 65×49 YUV420P, limited range BT.601 (`smpte170m`), 12 frames
at 6 fps, keyframes every 3 frames. Red/green/blue bars reverse after one second.
The decoder under test is the vendored FFmpeg 7.1.1 native VP8 implementation.

`test_vp8_colors.cpp` checks analytic YUV reference samples on top, middle and
bottom rows (including odd chroma height), full frame count, temporal changes,
forward/backward seek, fresh reopen, and RGB poster primaries. Worker tests use
the actual Software preference gate and hardware-requested/device-unavailable
fallback separately, then real probing (software fallback in the sandbox),
retain outputs through EOF, and repeat seeking on the same
worker. Tolerances allow lossy VP8 rounding, while catching green output or
swapped U/V. These are software/fallback tests, not claimed hardware coverage.
`test_frame_convert.cpp` additionally exercises odd-sized padded NV12 with
asymmetric chroma and retains the secure copy after releasing its source.

## README-ready user guidance

### Green or corrupt VP8/WebM playback (especially NVIDIA on Linux)

First select **F2 → Playback → Video decoding → Software**. Close the video and
open it again: the choice is saved and applies to the **next clip**, not to an
already-open decoder. `Ctrl+Shift+H` toggles the same choice; check its toast says
**Software (next clip)**. F1 shows the configured decode status, but a successful
VAAPI device probe alone does not prove that a particular clip decoded on the GPU.
No export or conversion of decrypted vault media is necessary.

Software decoding passes the project's synthetic VP8 color/seek regression
checks. If it fixes the affected clip, leave Software selected as the bypass;
it trades hardware acceleration for CPU decoding. If it does not, report that
result too: green output alone does not identify a driver fault.

For a native Arch installation using NVIDIA, inspect the translation layer:

```sh
pacman -Q nvidia-open nvidia-utils libva-nvidia-driver libva-utils
vainfo
```

Use the installed kernel-package variant (for example `nvidia-open-dkms`) if
appropriate. Missing optional packages are diagnostic information.
Arch packages the upstream VAAPI/NVDEC bridge as `libva-nvidia-driver`, and
`libva-utils` supplies `vainfo`. If hardware playback is wanted, install/update
those packages as part of a full system upgrade:

```sh
sudo pacman -Syu libva-nvidia-driver libva-utils
```

Restart/reboot as required by the NVIDIA/kernel update, then restart Obscura
(the VAAPI device probe is cached for the process). Compare the same clip after
selecting Hardware (with fallback) and reopening it. Check `vainfo` for the
chosen driver and VP8 VLD support. On a multi-GPU system, default `vainfo` may
select a different device; record which GPU/render node was tested. Advertised
support is not proof of correct pixels. Container/Flatpak runtime drivers may
differ from native host packages, so these host directions do not establish a
sandboxed application's driver version.

Upstream v0.0.15 includes a VP8 slice-header fix, but an update is **not a
verified fix for this report**. The package names and upstream fix are verified
from the sources below; these commands have not been run on the affected PC.
Do not replace `nvidia-open` or apply Firefox-specific configuration merely
because a WebM looks green. If hardware still corrupts frames, keep Software
selected and report GPU model, kernel/NVIDIA/bridge versions, app build, F1
status, `[HwAccel]` startup diagnostic, and whether the committed synthetic
fixture also fails. Share synthetic media rather than private vault content.

## Sources checked 2026-10-23

- [Upstream compatibility warning, supported codecs and testing](https://github.com/elFarto/nvidia-vaapi-driver)
- [v0.0.15 release notes](https://github.com/elFarto/nvidia-vaapi-driver/releases/tag/v0.0.15)
- [VP8 slice-header change #394](https://github.com/elFarto/nvidia-vaapi-driver/pull/394)
- [Arch libva-nvidia-driver package](https://archlinux.org/packages/extra/x86_64/libva-nvidia-driver/)
- [Arch libva-utils package](https://archlinux.org/packages/extra/x86_64/libva-utils/)

## Local validation

- Focused native runner: **16 tests, zero failures**. Includes the two new VP8
  tests and all frame-conversion tests, including the new padded NV12 case.
- Focused ASAN/UBSan runner: **16 tests, zero failures**, exit 0, leak checking
  enabled in an unrestricted run. Instrumented application media sources used
  shared prebuilt vendor libraries (vendor assembly/codecs are not instrumented).
  The initial sandbox run passed assertions but LeakSanitizer could not finish
  under ptrace; it is not counted as a clean sanitizer gate.
- New/modified test translation units also pass no-AV syntax compilation.
- Fixture generation repeated with identical SHA-256; system FFmpeg's RGB
  center samples were `(254,0,0)`, `(0,255,1)`, `(0,0,255)`.
- `git diff --check` passes. No production media code changed: the generated
  fixture did not reproduce an application decoding failure.

The original full native build stopped on disk quota before tests ran. Its
copied vendor/build artifacts were removed only from this worktree, reducing it
from about 3 GiB to 13 MiB before focused binaries. Dependencies are now read-only
shared symlinks. No shared source, build or vendor artifact was edited. Full
Debug/Release/no-AV gates are intentionally deferred to the parent's integrated
build, as requested. The focused runner links `test_vp8_colors.cpp` and
`test_frame_convert.cpp` against the actual local media implementation, native
VP8/FFmpeg 7.1.1, SDL3, and the standard test harness. The normal integrated test
runner discovers the new test file through the existing premake glob.

## Delivery boundary

README/ROADMAP integration and stacked PR/CI/SonarCloud gates belong to the
parent delivery. This isolated change does not claim the hardware report fixed
or Phase 122G's hardware acceptance completed. Reproduction and an A/B test on
the affected NVIDIA PC remain necessary.
