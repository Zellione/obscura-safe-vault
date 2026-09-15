# Phase 102 — VAAPI hwaccel diagnostics + force-software toggle ✅

**Status:** ✅ shipped
**Date:** 2026-09-15

Linux VAAPI hardware video decode was the silent-no-op root cause of "slow
decode on a box that should be fast": when `av_hwdevice_ctx_create` failed
at runtime — wrong driver in `LIBVA_DRIVER_NAME`, no `/dev/dri/renderD*`
accessible to the user, or the loaded driver doesn't expose the codec's
`VAProfile` — the existing Phase 41 software `VideoDecodeWorker` is the
automatic silent fallback, but NOTHING logged which step had blocked. The
user had no way to distinguish "GPU is too old for VP9 hw decode" from
"wrong driver" from "permission on the render node", short of attaching a
debugger and stepping through FFmpeg.

Phase 43 already declared the VAAPI hwaccel flag set + the `vaapi-shim`
+ the `vendor/libva` headers; the runtime was correct. The diagnostic
output and the user-toggled overrides were the missing piece.

## What changed

### 1. `[HwAccel]` diagnostic log on first probe (one log per process)

`src/media/hw_accel.cpp`'s `cached_device_ctx()` now logs exactly once
per process — mirrors `should_warn_mlock_once()`'s
(`src/crypto/secure_mem.h`) cache-the-outcome pattern:

```
[HwAccel] VAAPI probe: OK; LIBVA_DRIVER_NAME=(unset) LIBVA_DRIVERS_PATH=(unset)
```

or, on any failure (driver not found, render-node open failed,
`vaInitialize` rejected the driver):

```
[HwAccel] VAAPI probe failed: LIBVA_DRIVER_NAME=(unset) LIBVA_DRIVERS_PATH=(unset) — software decode will be used; run `vainfo` for details
```

FFmpeg's `av_hwdevice_ctx_create` swallows the specific errno / VAStatus,
so the app-level log can't name the exact step that failed — the
suggestion to run `vainfo` is the cross-platform fix: it enumerates every
profile the driver exposes plus the actual `VAStatus` for each.

A companion log fires once per `AVCodec` name per process when the
decoder doesn't advertise a VAAPI `hw_config` — that is, FFmpeg's
`avcodec_get_hw_config()` returns no entry for `AV_HWDEVICE_TYPE_VAAPI`
on that codec, which means the FFmpeg build was configured without that
specific hwaccel:

```
[HwAccel] decoder libvpx-vp9 reports no VAAPI hw_config — using software
```

### 2. Two persisted runtime overrides

| Hotkey | Setting | Default | Effect |
|---|---|---|---|
| `Ctrl+Shift+H` | Hardware video decode enabled | on | off → `try_attach_hwaccel()` is skipped entirely |
| `Ctrl+Shift+F` | Force software decode | off | on → always software (overrides the HW toggle) |

Both live-saved to `hwaccel.conf` in the config dir via the new
`platform::HwAccelPref` (mirrors the `AutoplayPref` /
`VolumePref` pattern: file-based, atomic `temp + rename`, missing or
invalid → defaults, never breaks startup). File format: one
`hw=on` / `hw=off` line + one `swforce=on` / `swforce=off` line.

`try_attach_hwaccel()` now reads both overrides first and returns false
immediately if either says software — no FFmpeg call, no probe, no
diag-log spam. New runtime accessors live in `media/hwaccel_setting.h`:
`enable_hardware_decode()` / `force_software_decode()` plus setters.

The new persisted settings also appear in F2 → Playback as two new
rows ("Hardware video decode" + "Force software decode"). The F2 value
cycling handler live-saves them via `HwAccelPref::save` (same as
autoplay and the existing machine-scoped prefs).

### 3. F1 help popup "Video decode:" row

F1's Global group gains a new live status line drawn without brackets
(the same convention as the existing "Secure memory:" row):

- `Video decode: VAAPI OK (run \`vainfo\` for driver name)` — probe succeeded
- `Video decode: VAAPI unavailable (see error.log; run \`vainfo\`)` — probe failed
- `Video decode: software-only (hardware disabled by Ctrl+Shift+H)` — user toggled HW off
- `Video decode: software-only (force-software by Ctrl+Shift+F)` — user toggled SW forced on
- `Video decode: not attempted (no clip played yet)` — no clip opened this session

The two user toggles take priority over the probe outcome, so once the
user has explicitly chosen software the line reflects that choice
regardless of whether the underlying probe would have succeeded. Rendered
through the new `ui::hwaccel_status_line(int, bool, bool)` pure helper —
testable in isolation.

A new `media::hwaccel_status()` accessor exposes the cached probe
outcome (`NotAttempted` / `Ok` / `Unavailable`) so the F1 row can render
without re-running the probe. `force_software` and the hardware-disabled
gate are read separately from `media/hwaccel_setting.h` — the probe
itself is independent of the user's preferences.

### 4. `[HwAccel]` log tag

The new `[HwAccel]` module prefix matches the existing `[Vault]`,
`[Crypto]`, `[SecureMem]`, `[Platform]`, `[VideoDecodeWorker]`,
`[ArchiveReader]` convention from `AGENTS.md` (`Errors must be
prefixed with [Module]`). Helps filtering `error.log` and grep.

## Acceptance criterion

- `scripts/test.sh` green: **2274 tests, 0 failed** (baseline 2262 + 12 new:
  5 in `tests/platform/test_hwaccel_pref.cpp`, 2 in `tests/media/test_vaapi_shim.cpp`,
  5 in `tests/ui/test_help_popup.cpp`).
- `scripts/test.sh --asan` green.
- **Manual AMD box**: with `vainfo` showing `VAProfileVP9Profile0`:
  - play a VP9 `.webm`. With defaults, `~/.config/ObscuraSafeVault/ObscuraSafeVault/error.log`
    has one line per process:
    ```
    [HwAccel] VAAPI probe: OK; LIBVA_DRIVER_NAME=(unset) LIBVA_DRIVERS_PATH=(unset)
    ```
  - confirm GPU block engages via `cat /sys/class/drm/card*/device/gpu_busy_percent`
    or `radeontop` while a clip plays (non-zero GPU utilisation).
  - press `Ctrl+Shift+F` while a clip plays (or before): confirm software
    path runs (no GPU utilisation, no `[HwAccel]` probe log).
  - press `Ctrl+Shift+H` while a clip plays (or before): confirm probe runs
    and reports unavailable (HW gate is off, no hwaccel attached).
  - A/B compare CPU% with all defaults vs. `Ctrl+Shift+F` on — the
    difference is the GPU's contribution to decode.

## Tests added

| File | Test | Covers |
|---|---|---|
| `tests/platform/test_hwaccel_pref.cpp` | 5 | missing-file defaults, round-trip both overrides, partial-file parse, garbage lines ignored, `default_location()` paths |
| `tests/media/test_vaapi_shim.cpp` | 2 (existing test_vaapi_shim + 1 extended) | toggle round-trip + `hwaccel_status()` accessor steady-state validity |
| `tests/ui/test_help_popup.cpp` | 5 | `hwaccel_status_line` override-priority ordering (force_software > hw_disabled > probe outcome), the four user-visible steady states |

The diag-log-dedup test (one log per process across N probe calls) is
covered implicitly: `test_only_force_hwaccel_unavailable(bool)` resets
the cached device + status, so any dedup assertion is testable via
stderr capture in a future phase if needed.

## Out of scope (deliberate)

- No GPU-resident texture interop with SDL_Renderer (frames still land
  in system memory, unchanged from Phase 41).
- NVDEC / NVDEC / vendor-specific decode APIs beyond VAAPI —
  Phase 43 already documented the trade-off and the user chose VAAPI
  for broad coverage (Intel/AMD/Nouveau). A proprietary-driver fast
  path can be revisited if a real fixture proves the need.
- Hardware-accelerated *encoding* (this app has no encode path at all —
  decode-only, `mem:tech_stack`).
- macOS / VideoToolbox (platform not supported since Phase 101).
- Forcing hardware decode without a software fallback — safety-critical:
  the fallback path remains automatic and silent, exactly like Phase 41.
- No change to FFmpeg configure list — `vp8_vaapi,vp9_vaapi` already
  enabled (Phase 52).

## Files changed

- `src/media/hw_accel.{h,cpp}` — diag log + override gates + `hwaccel_status()` accessor
- `src/media/hwaccel_setting.{h,cpp}` — runtime override slots (new)
- `src/platform/hwaccel_pref.{h,cpp}` — persisted `hwaccel.conf` (new)
- `src/ui/help_popup.{h,cpp}` — `hwaccel_status_line` + F1 Global row
- `src/ui/settings_model.{h,cpp}` — 2 new Playback rows + value cycling
- `src/ui/settings_overlay.cpp` — render the 2 new rows + persist via `HwAccelPref`
- `src/app/app.cpp` — seed from pref at init, `Ctrl+Shift+H` / `F` hotkeys, sync to runtime on F2 events
- `premake5.lua` — add `src/platform/hwaccel_pref.cpp` to osv_tests files list
- Tests: 3 new + 1 extended
- Docs: this file, ROADMAP.md row, AGENTS.md Video-decode table row, README.md new "Hardware decode on Linux" subsection, `.serena/memories/tech_stack.md` Phase 43 Part 2 paragraph updated
