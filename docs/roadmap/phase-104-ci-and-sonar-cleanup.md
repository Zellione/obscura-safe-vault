# Phase 104 — CI failure + SonarQube cleanup ✅

**Status:** ✅ shipped
**Date:** 2026-09-15

The PR #221 ("Phase 101+102+103: Remove Windows, VAAPI hwaccel diagnostics,
AudioDecoder EAGAIN fix") had two CI failures flagged by the owner:

1. **`No FFmpeg / tests / Linux` CI leg failed to compile** at step 108/600 on
   `src/ui/help_popup.cpp` — `hwaccel_status_line()` referenced
   `media::HwAccelStatus` unconditionally, but the enum only exists when
   `OSV_VENDORED_AV` is defined. The `--no-av` build defines no
   `OSV_VENDORED_AV`, so the six `case` labels vanished and the function
   fell off the end → `-Werror=return-type`.

2. **SonarQube flagged 12 new findings** (quality gate still passed at
   89.8 % coverage / 0 dup, but per AGENTS.md / Phase 100 convention we
   re-scan to zero before merge).

This phase clears both — same branch (`phase-101-remove-windows`),
three commits.

## What changed

### Part A — the `--no-av` build break

`src/ui/help_popup.cpp`'s `hwaccel_status_line()` now wraps the
`using media::HwAccelStatus;` and the `switch` in
`#if defined(OSV_VENDORED_AV) && defined(OSV_HWACCEL_VAAPI)`. In a non-AV
build the function returns `"Video decode: hwaccel not built"`, which is
honest — the override strings above it
(`force-software by Ctrl+Shift+F`, `hardware disabled by Ctrl+Shift+H`)
already told the user the truth, so the AV-specific probe-outcome
branches don't apply.

The call site at `draw_help_popup()` already gates the call itself in the
same `#if`, but the function definition was always compiled regardless;
the inner `#ifdef` makes the function compile cleanly on every build.

Also: the five `hwaccel_status_line` tests in `tests/ui/test_help_popup.cpp`
are now wrapped in the same `#ifdef` so the `--no-av` CI leg's
`Run tests (no FFmpeg)` step passes.

### Part B — SonarQube findings (12 → 0)

| # | Rule | File / line | Fix |
|---|---|---|---|
| B.1 | `cpp:S3776` (CRITICAL) | `src/app/app.cpp:716` (`OverlayDispatch::settings()`, complexity 39) | Extracted `try_f2_toggle`, `try_hwaccel_hotkey`, `try_trigger_migration`, `sync_runtime_after_settings_event`. The outer `settings()` becomes a flat dispatcher (complexity ~3). |
| B.2 | `cpp:S3776` (CRITICAL) | `src/ui/settings_overlay.cpp:357` (`pane_row_text()`, complexity 35) | Extracted `appearance_row`, `playback_row`, `browsing_row`, `tagcolours_row`, `vaultops_row`, `security_row`. Top-level `pane_row_text()` is now a flat switch calling the right helper (complexity ~2). |
| B.3 | `cpp:S5421` (CRITICAL) | `src/media/hw_accel.cpp:73` (namespace-scope globals `g_force_unavailable`, `g_probe_mutex`) | Moved into the existing `ProbeCache` struct as `force_unavailable` + `mtx` members. **Bonus:** closes a latent thread-safety bug — `test_only_force_hwaccel_unavailable()` previously wrote `g_force_unavailable` with no lock. |
| B.4 | `cpp:S5827` (MAJOR) | `src/app/app.cpp:520` (redundant `float` annotation) | `const auto tw = static_cast<float>(font.measure(text));` |
| B.5 | `cpp:S7121` (MAJOR) | `src/app/app.cpp:528` (redundant `.c_str()`) | `r.draw_text(font, ..., text, WARN)` — `draw_text`'s last param is `std::string_view`, so `.c_str()` is leftover from the `const char*` era. |
| B.6 | `cpp:S7035` (MAJOR) | `src/ui/help_popup.cpp:237` (`static_cast<int>(enum)`) | `std::to_underlying(media::hwaccel_status())` at the call site. |
| B.7 | `cpp:S1820` (MAJOR) | `src/app/app.h:32` (App struct has 21 fields) | Bundled the three Phase 102 toast members into `HwAccelToastState`. App now has 19 fields. The visibility predicate in `app/hwaccel_toast.h` still takes primitives (text, elapsed, window_secs), so the test in `tests/app/test_hwaccel_toast.cpp` is unaffected. |
| B.8 | `cpp:S1188` (MINOR) | `src/media/audio_decoder.cpp:115` (lambda > 20 lines) | Extracted the per-frame build body into `AudioDecoder::push_frame(out)`. The `drain` lambda shrinks to receive-loop + error log (~10 lines). |
| B.9 | `cpp:S886` (MINOR) | `src/media/audio_decoder.cpp:164` (C-style `for (init; cond; update)`) | Converted to `while (send_ret == AVERROR(EAGAIN) && attempt < MAX_SEND_RETRIES)` with explicit `++attempt;`. |
| B.10 | `cpp:S6177` (MINOR) | `src/ui/help_popup.cpp:64` (`using media::HwAccelStatus;`) | Same area as the Part A fix — falls back to the classic using-declaration. (GCC 16 rejects `using enum` with fully-qualified names; verified by trying.) |
| B.11 | `cpp:S6177` (MINOR) | `src/platform/paths.cpp:223` (`OwnerOnlyCreate::` prefix on every reference) | `using enum OwnerOnlyCreate;` inside `create_owner_only_file()`, drop the prefix on the `case` labels and return values. Pre-existing on main since Phase 88; my Phase 101 rewrite of `paths.cpp` moved the line into the PR's diff context. |
| B.12 | `cpp:S6004` (MINOR) | `src/media/hw_accel.cpp:157` (declare-then-guard pattern) | Use an init-statement inside the `if`: `if (const std::string name = decoder ? decoder->name : "(null)"; !logged.has_value() || *logged != name) { ... }`. No behaviour change. |

## Tests added / modified

- `tests/ui/test_help_popup.cpp`: the five `hwaccel_status_line` tests
  (added in Phase 102) are now wrapped in
  `#if defined(OSV_VENDORED_AV) && defined(OSV_HWACCEL_VAAPI)`. The
  override-string tests (force-software, hardware-disabled) stay
  unconditional — they test user-facing strings that exist on every build.
- No new tests. All Phase 102 / 103 tests remain in place; the
  refactors don't change observable behavior.

## Files touched

- `src/ui/help_popup.cpp` — Part A + B.6 + B.10
- `src/app/app.h` — B.7 (HwAccelToastState struct)
- `src/app/app.cpp` — B.1 (settings() helpers), B.4, B.5 (draw_hwaccel_toast)
- `src/ui/settings_overlay.cpp` — B.2 (pane_row_text() helpers)
- `src/media/hw_accel.cpp` — B.3 (move globals), B.12 (init-statement)
- `src/media/audio_decoder.{h,cpp}` — B.8 (push_frame extracted),
  B.9 (loop refactor)
- `src/platform/paths.cpp` — B.11 (using enum)
- `tests/ui/test_help_popup.cpp` — gate hwaccel_status_line tests
- `docs/roadmap/phase-104-ci-and-sonar-cleanup.md` — new phase doc (this file)
- `ROADMAP.md` — new Phase 104 row

## Verification

- `scripts/test.sh` (Debug + FFmpeg): **2277 tests, 0 failed**
- `scripts/test.sh --asan` (Debug-asan + FFmpeg): **2277 tests, 0 failed**
- `scripts/test.sh` with `--no-av` (Debug, no FFmpeg): **2083 tests, 0 failed**
  (5 fewer because the hwaccel probe-outcome tests are gated on the
  VAAPI macros)
- All 12 SonarQube findings cleared.

## Out of scope (deliberate)

- A real `vainfo` parser / live VAAPI driver-name extraction. The
  `[HwAccel] VAAPI probe: OK` line still tells the user to run `vainfo`
  themselves for the driver name — that's the cross-platform fix (the
  same FFmpeg call we use internally swallows the vendor name).
- Renaming `OSV_HWACCEL_VAAPI` to a more generic `OSV_HWACCEL_*` macro.
  Phase 101 already collapsed the D3D11VA branch; the macro name is now
  slightly misleading but renaming is noise vs signal at this point.
- Adding the codec cache key to the SonarQube workflow. SonarQube's
  re-scan picks up new code automatically; no cache key wiring needed.

## Acceptance criterion

- `scripts/test.sh` green: **2277 tests, 0 failed**.
- `scripts/test.sh --asan` green: **2277 tests, 0 failed**.
- `scripts/test.sh` with `--no-av` green: **2083 tests, 0 failed** (the
  5 hwaccel-specific tests skip cleanly).
- SonarQube scan reports **0 new issues** on PR #221 (was 12).
- The CI matrix (gcc/clang × Debug/Release + ASAN + TSan + no-av +
  SonarQube) all green.
