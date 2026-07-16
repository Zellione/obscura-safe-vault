## Phase 25 — Bugfixes & housekeeping ✅

**Goal:** Fix layout-dependent keybindings, give every file operation the same
background-progress UX the Phase 24 import got, and tidy the repo (drop committed
planning docs, flag the project as AI-driven).

### Tasks
- [x] **Layout-independent keybindings** — the in-viewer video **volume** keys `[` / `]` don't fire on non-US layouts (e.g. German QWERTZ, where those glyphs live behind AltGr), so volume can't be changed at all. Rebind volume to **layout-independent** keys — SDL **scancodes** (physical key position) or non-punctuation keys — and audit the other punctuation shortcuts (`/` search, `?` advanced search, `` ` `` quick-switch, `[` / `]`) for the same defect. (Letter/digit and named keys like `M` mute, arrows, Enter, Esc are unaffected.)
- [x] **Background file operations with progress** — move/copy (transfer within/between vaults), delete (a gallery subtree or a single item), and export currently block the UI and only surface a final one-line message. Run each on a worker thread with a live **“N / M items” progress modal + cancel**, reusing the Phase 24 `ZipImportJob` / `ImportProgress` pattern — preserving the single-thread vault-handle invariant and suppressing the idle auto-lock during the op (`Screen::blocks_idle_lock()`). Export keeps its consent modal; its background write is still the one gated plaintext-to-disk deviation.
- [x] **Remove committed docs dir** — delete the only committed doc (`docs/superpowers/plans/2026-06-12-phase8-cross-platform.md`) and add `docs/` to `.gitignore` so AI planning artifacts stay out of the tree.
- [x] **README note** — add a note at the very top of `README.md` stating this is an **AI-driven project, vibe-coded for educational purposes**.
- [x] Update `CLAUDE.md` / `mem:*` if the keybindings or the transfer/delete/export flow change.
- [x] `tests/` — unit-test the layout-independent key mapping (scancode → action, independent of layout); test the background-op progress/cancel reporting the way `ZipImportJob` is tested.

**Out of scope (YAGNI):** fully user-remappable keybindings; UI text localisation; reworking the export consent/scope model (threading only).

### Acceptance criterion
Volume can be changed on a non-US (German) keyboard layout; a large gallery
move/copy/delete/export shows live progress without freezing the UI and can be
cancelled; `docs/` is gone from the tree and gitignored; the README carries the
AI-driven note.

**Status:** ✅ 581/581 tests pass (`scripts/test.sh`); `scripts/test.sh --asan` clean.
Slideshow dwell binds `[`/`]` by **scancode** (`ui::bracket_key_for_scancode`, the
keys right of `P`) plus `-`/`+`. Video **volume** uses `ui::volume_dir`, which
accepts the `[`/`]` produced character (resolved via `SDL_GetModState` so German
QWERTZ **AltGr+8/9 works** — the initial scancode-only fix missed this, as AltGr+8
reports the physical `8` scancode), the `-`/`+`/`=` glyph keys (the intuitive pair,
now advertised in the HUD as `[-/+] Vol`), and the physical bracket scancodes — all
unit-tested. The
`/`, `?`, `` ` `` character shortcuts were already layout-robust via
`SDL_GetKeyFromScancode`; that logic is now centralised as `is_search_key` /
`is_advanced_search_key` / `is_quick_switch_key` in the same header. Export, delete
and move/copy now run on a background worker via a reusable `ui::FileOpJob`
(mirrors `ZipImportJob`): a shared `vault::OpProgress` (which `ui::ImportProgress`
now aliases) drives an "N / M" progress modal (`ui::draw_op_progress`) + Esc-cancel,
gated so the UI never touches the vault while a job runs, with the idle auto-lock
held off (`blocks_idle_lock()`). `vault::transfer_images` (new bulk driver),
`transfer_gallery`, and `export_images` take an optional `OpProgress*`; a cancelled
gallery Move leaves the source intact (recoverable duplicate, never a loss). The
`docs/` plan doc is removed and gitignored; the README carries the AI-driven note.
