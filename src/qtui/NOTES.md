# osv-qt experiment — running notes

Resume protocol: `git log --oneline`, read this file top-down, then open
docs/superpowers/plans/2026-07-29-qt-quick-ui-experiment.md and find the
first unchecked checkbox.

## 2026-07-29 Task 1 — scaffold builds and runs
- CMake + Qt 6.11 window works via scripts/build_qt_experiment.sh
- Next: core library under CMake (Task 2)

## 2026-07-29 Task 2 — osv_qt_core library + smoke test + mkvault
**Status:** DONE

**osv_qt_core target built:** reuses untouched src/crypto, src/vault, non-SDL src/image, plus key models and platform shims.

**TUs added beyond brief's base list (due to linker demands):**
- `src/platform/paths.cpp` (required by error_log.cpp; depends on SDL3 for config_dir())
- `src/media/video_probe.cpp` (required by vault.cpp add_video/repair_video_metadata; best-effort no-FFmpeg fallback)

**Build artifacts:**
- `osv_qt_core`: static library (reusable by all later tasks)
- `osv_qt_core_smoke`: headless test (create → open → wrong-password → unlock → create_gallery → list)
- `osv-qt-mkvault`: CLI tool to generate test vaults; persisted as `/tmp/qtexp.osv` (no images available in tests/ fixture tree)

**Test results:**
- `osv_qt_core_smoke` exits 0 with "core_smoke OK"
- `osv-qt-mkvault /tmp/qtexp.osv test123` succeeded (vault created without images)
- Premake build `scripts/build.sh` still works (no regressions)

**Next:** SecureTextInput model wrapping (Task 3)
