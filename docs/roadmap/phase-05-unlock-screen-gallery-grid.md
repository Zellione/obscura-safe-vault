## Phase 5 — Unlock screen & gallery grid ✅

**Goal:** Connect the vault layer to the UI; the app can create/open/unlock a vault and browse galleries.

### Tasks
- [x] `src/platform/paths.{h,cpp}` — `config_dir()`, `default_vault_path()`, `read_file()`. The open-file dialog became its own async wrapper, `src/platform/file_dialog.{h,cpp}`, over `SDL_ShowOpenFileDialog`.
- [x] `src/ui/input.{h,cpp}` — `InputAction` enum + `map_key()` mapping SDL keycodes → actions.
- [x] `src/ui/widgets.{h,cpp}` — pure layout/hit-testing (`grid_columns`, `grid_cell_rect`, `grid_hit`, `fit_rect`, `point_in_rect`) + thin `draw_button`/`draw_text_field` helpers. Masked password entry lives in `src/ui/secure_text_field.{h,cpp}` (an mlock'd buffer). (`ProgressBar`/`ScrollView` deferred — not needed by the Phase 5 screens.)
- [x] `src/ui/unlock_screen.{h,cpp}`:
  - [x] Password field + keyfile picker button.
  - [x] "Create New Vault" flow. (Passphrase-strength meter + random generation deferred to Phase 7, per `CLAUDE.md`.)
  - [x] "Open Existing Vault" flow (with "Open other…" file picker).
  - [x] Error display for wrong password / bad keyfile.
  - [x] Submit validation extracted to a pure `src/ui/unlock_logic.{h,cpp}` (`decide_submit`).
- [x] `src/ui/gallery_grid.{h,cpp}`:
  - [x] Tile grid (sub-gallery tiles or thumbnail tiles, never mixed — enforces the leaf invariant on import/create).
  - [x] Breadcrumb navigation bar (path state in `src/ui/nav_model.{h,cpp}`).
  - [x] Keyboard: `Enter`/`Space` = open, `Backspace`/`Esc` = up; `I` = import, `N` = new gallery.
  - [x] Import button → file dialog → `Vault::add_image` → grid refresh.
- [x] App state machine: `src/ui/screen.h` `Screen` interface + `Nav` transitions; `App` owns one active screen and swaps unlock ↔ gallery on `take_nav()`.
- [x] `tests/ui/`:
  - [x] Submit-logic scoring (`decide_submit`): empty / mismatch / unlock / create cases.
  - [x] `NavModel` path split/join, enter/up, selection clamp (headless).
  - [x] Widget layout/hit-test (`grid_columns`/`grid_cell_rect`/`grid_hit`/`fit_rect`), input mapping, and `SecureTextField` wipe verification.

### Acceptance criterion
App starts in the Locked (unlock) state. Creating a vault, adding images, and navigating the gallery tree works end-to-end with keyboard and mouse.

**Status:** ✅ Merged in #6. 110/110 tests pass under `scripts/test.sh` and ASAN+UBSan+LSan;
the SonarCloud quality gate is green with zero open issues. The UI is built around a small
`Screen` state machine (`UnlockScreen` ↔ `GalleryGrid`), with all decision logic factored into
pure, headlessly-tested units (`unlock_logic`, `nav_model`, `widgets`, `input`,
`secure_text_field`).

> **Notes / decisions made during implementation**
> - **Async file dialogs:** `SDL_ShowOpenFileDialog` is callback-based and may fire on another
>   thread, so `platform::FileDialog` parks results in a mutex-guarded slot delivered to the main
>   thread via `take_result()` once per frame. It is owned by `App` for the whole run because the
>   callback captures `this`.
> - **Password buffer:** `SecureTextField` holds the typed password in an mlock'd buffer and
>   `crypto_wipe`s it on `clear()` (invariant #2), so plaintext passwords never land in a plain
>   `std::string`.
> - **Passphrase-strength meter** and **random passphrase generation** are intentionally deferred
>   to Phase 7 (Hardening & polish), matching the deferral table in `CLAUDE.md`.
