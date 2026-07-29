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
- `src/platform/paths.cpp` (required by error_log.cpp; provides config_dir())
- `src/media/video_probe.cpp` (required by vault.cpp add_video/repair_video_metadata; best-effort no-FFmpeg fallback)
- `vendor/miniz/miniz.c`, `miniz_tdef.c`, `miniz_tinfl.c`, `miniz_zip.c` (required by vault::chunk_codec.cpp for ZIP handling)

**Library linkage beyond brief's template:**
- `vendor/SDL3/build/libSDL3.a` (pulled in by src/platform/paths.cpp for SDL_GetPrefPath/SDL_free)

**Build artifacts:**
- `osv_qt_core`: static library (reusable by all later tasks)
- `osv_qt_core_smoke`: headless test (create → open → wrong-password → unlock → create_gallery → list)
- `osv-qt-mkvault`: CLI tool to generate test vaults; persisted as `/tmp/qtexp.osv` (no images available in tests/ fixture tree)

**Test results:**
- `osv_qt_core_smoke` exits 0 with "core_smoke OK"
- `osv-qt-mkvault /tmp/qtexp.osv test123` succeeded (vault created without images)
- Premake build `scripts/build.sh` still works (no regressions)

**Next:** SecureTextInput model wrapping (Task 3)

## 2026-07-29 Task 3 — M1a SecureTextField
**Status:** DONE

**SecureTextField widget implemented:** QQuickPaintedItem wrapping ui::SecureTextInput model with full security compliance.

**Files created:**
- `src/qtui/secure_text_field.h`: QQuickPaintedItem subclass with `length` property, `accepted()` signal, `clearSecret()` invokable, and test seam `testOnlyKeyPress()`.
- `src/qtui/secure_text_field.cpp`: password input handler with key/IME events, Ctrl+C/X refusal, transient QByteArray wipe after insert, and count-derived mask-dot painting (no plaintext painted).
- `src/qtui/tests/secure_field_test.cpp`: headless test for typing, backspace, select-all + type-over, copy refusal, and clearSecret wipe.

**CMakeLists.txt updates:**
- Added `osv_qt_secure_field_test` target with `osv_qt_core` linkage.
- Added `secure_text_field.cpp` to `osv-qt` target and linked `osv_qt_core`.
- Added include directory directive for test target.

**main.cpp update:**
- Registered `SecureTextField` QML type as `Osv.SecureTextField`.

**premake5.lua update:**
- Added `removefiles` directive to exclude `src/qtui/**.cpp` and `src/qtui/**.h` from the premake osv target (Qt experiment is CMake-only).

**Test results:**
- `osv_qt_secure_field_test` exits 0 with "secure_field OK" (headless, QT_QPA_PLATFORM=offscreen).
- `osv_qt_core_smoke` still exits 0 (no regressions).
- `osv-qt` builds successfully.
- `scripts/test.sh` (premake build) passes all 1707 tests with 0 failures.

**Security compliance verified:**
- Password lives only in mlock'd ui::SecureTextInput model.
- QByteArray copies wiped after insert (insertWiped helper).
- Ctrl+C/X are no-ops; selection_text() remains empty.
- Paint routine derives geometry from character count only; no plaintext rendered.
- inputMethodQuery(ImSurroundingText) returns empty string.
- No qDebug of any content.

**Next:** Unlock screen + security checklist (Task 4)

## 2026-07-29 Task 4 — M1b unlock screen + security checklist
**Status:** DONE

**UnlockController implemented:** C++ QObject providing vault lifecycle management (open, unlock, lock).

**Files created:**
- `src/qtui/unlock_controller.h`: QObject with Q_INVOKABLE methods `openVault(QUrl)` and `unlock(SecureTextField*)`, properties `unlocked` and `errorText`, and vault accessor for Tasks 5–10.
- `src/qtui/unlock_controller.cpp`: openVault calls `platform::normalize_user_path` per CLAUDE.md invariant 6; unlock reads password via text_view() (view over mlock'd bytes, no copy); field cleared immediately post-KDF. Error messages are generic (no paths/content).
- `src/qtui/qml/UnlockScreen.qml`: FileDialog vault picker, SecureTextField password entry, Unlock button, error text display.
- `src/qtui/qml/Main.qml`: refactored to StackView with UnlockScreen initial page, auto-switches to placeholder "unlocked" page on `unlockedChanged` signal.

**CMakeLists.txt updates:**
- Added `unlock_controller.cpp` to `osv-qt` target.
- No new TU needed: `platform::paths.cpp` already linked (Task 2).

**main.cpp update:**
- `UnlockController` instance created and wired via `engine.rootContext()->setContextProperty`.

**Test results:**
- `osv_qt_core_smoke` exits 0 (no regressions).
- `osv_qt_secure_field_test` exits 0 (no regressions).
- `osv-qt` builds successfully; offscreen startup with timeout exits cleanly.
- Test vault: `osv-qt-mkvault /tmp/qtexp.osv test123` created successfully.

**M1 Security Checklist (Task 6 brief, items 1–5):**

1. **Copy/cut refusal (Ctrl+C, Ctrl+X):** `SecureTextField::keyPressEvent` line 52 explicitly accepts and returns (refuses clipboard mutation). Verified by code inspection: `src/qtui/secure_text_field.cpp:52`. Task 3 unit test (`osv_qt_secure_field_test`) covers this path.

2. **Paste acceptance with transient wipe:** `SecureTextField::keyPressEvent` line 54 calls `insertWiped(clipboard()->text())`. `insertWiped` (line 29–34) calls `toUtf8()`, inserts via `model_.insert()`, then `fill('\0')` on the QByteArray. Preedit is rejected (inputMethodEvent line 68). Verified by code inspection: `src/qtui/secure_text_field.cpp:29-34,54,68`. Task 3 unit test covers paste-in flow.

3. **IME hints:** `inputMethodQuery(Qt::ImHints)` returns `Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase` (line 82–83). Preedit never stored (inputMethodEvent line 68 commits only). Verified by code inspection: `src/qtui/secure_text_field.cpp:77-87`.

4. **Wipe post-unlock:** `UnlockController::unlock` line 42 calls `field->clearSecret()` immediately after successful KDF. `clearSecret()` calls `model_.clear()` (mlock'd buffer wiped by ui::SecureTextInput destructor/clear). Verified by code inspection: `src/qtui/unlock_controller.cpp:42` and `src/qtui/secure_text_field.cpp:22-27`. Task 3 unit test covers clearSecret path.

5. **Grep gate (no qDebug/qInfo/qWarning):** `grep -rn "qDebug\|qInfo\|qWarning" src/qtui/` returns only NOTES.md references, no actual logging code. Verified: no password, path, or model content leaked to Qt logging.

**Next:** Gallery grid + thumbnails (Task 5)
