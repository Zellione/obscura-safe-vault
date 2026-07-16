## Phase 23 — UI colour schemes ✅

**Goal:** Offer several selectable UI colour themes, switchable at runtime and
remembered across launches in global config (no secrets).

### Tasks
- [x] **Runtime theme** — refactor `src/gfx/theme.h` from compile-time constants into a runtime `Theme` value (a struct of the existing colour tokens) plus a table of **built-in presets**: Refined Slate (current, default), a light theme, a high-contrast theme, and one more (e.g. sepia/midnight). A `gfx::active_theme()` accessor + `gfx::set_theme(id)` back the active selection; every `theme::…` call site reads the active theme (broad but mechanical — the token set is defined once and each preset fills it).
- [x] `src/platform/theme_pref.{h,cpp}` — persist the chosen theme id in the config dir (atomic write, **stores no secrets**, mirroring `vault_registry`); loaded at startup, saved on change; an unknown/absent id falls back to the default.
- [x] **UI** — a theme picker reachable from the **vault manager** (proposed key `C`) listing the presets with live apply-on-select; the choice persists immediately.
- [x] Update `CLAUDE.md` (runtime theme + `platform/theme_pref.*`) + `mem:core`.
- [x] `tests/` — every preset defines every colour token (no missing/zeroed tokens); `theme_pref` round-trip (save id → reload id; unknown id → default); a pure "next/select theme" helper if one is added. The broad refactor is additionally validated by the existing test suite staying green and the app building on all platforms.

### Acceptance criterion
The user can pick from several built-in colour themes; the choice applies
immediately, persists across restarts via global config (no secrets stored), and
all existing screens render correctly under every preset.

**Status:** ✅ 539/539 tests pass (`scripts/test.sh`); `scripts/test.sh --asan` clean.
`gfx::theme.h` is now a runtime `Theme` value with four built-in presets (Refined
Slate / Light / High Contrast / Midnight); `gfx::set_theme()` swaps the active one
and the existing `gfx::theme::X` tokens are references into it, so every call site
picks up a switch with no change. `platform::ThemePref` persists the chosen theme's
stable slug to `config_dir()/theme.conf` (atomic temp+rename, no secrets); it is
loaded in `App::init()` and saved live as the picker selection moves. The `ui::ThemePicker`
overlay (`C` in the vault manager, QuickSwitch-style) previews each preset on Up/Down —
the preview *is* the choice, persisted immediately; Enter/Esc just close.
