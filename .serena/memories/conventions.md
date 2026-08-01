# Conventions

## Naming
- `snake_case` — everything except `ClassName` (PascalCase) and `CONSTANT` (UPPER_SNAKE).
- Member variables: trailing underscore (`name_`).

## Error handling
- No exceptions. Functions that can fail return `bool` or a `std::expected`-like result.
- Log failures to `stderr` with `[Module]` prefix (e.g. `[Vault]`, `[Crypto]`).

## Headers
- `#pragma once` (not include guards).
- Minimal includes; forward-declare where possible.

## Comments
- Document *why*, not *what*. Non-obvious invariants and `// TODO(PhaseN):` only.

## UI colours & drawing
- Pull every colour from `gfx::theme` (theme.h) — do NOT hardcode inline `gfx::Color{...}` literals in screens/widgets.
- Use `draw_round_rect` / `draw_selection_glow` for surfaces and selection; `theme::RADIUS` / `RADIUS_SMALL` for corner radii.
- Keep pixel/layout maths in pure, headless, unit-tested helpers (e.g. `strip_layout`, `scroll_model`, `viewer_model.h`); screens own only SDL plumbing.
- **Any** string drawn into a fixed-width box must be elided first — never `draw_text` it raw. This is not only about unbounded vault-derived strings (paths, names, tags): a *static* hint authored to fit at an older font size is the recurring shape of this bug, and one of them ("Clear search? Resets all parameters." in the advanced-search confirm box) overflowed its fixed 440 px at every window size. Two helpers in `widgets.h`, and picking the wrong one makes it worse:
  - `ui::fit_text(font, s, max_w)` — **middle** elision, the default. Keeps both ends, so a modal hint retains its trailing `[Esc] Close`.
  - `ui::fit_text_tail(font, s, max_w)` — **tail** elision, for strings whose START carries the meaning. Help-popup lines read `[key]  description`, so middle elision mangles exactly the words being scanned for.
  Both are thin bindings over the pure, unit-tested `ui::elide_middle` / `ui::elide_tail` templates (templated on the measure callable, ASCII `"..."` since the atlas bakes 32–126 only). PR #54 and PR #128 each swept the UI for this.
- **Never hardcode a text-line pitch.** Derive it from `font.pixel_height()` via `ui::line_pitch(font_px)` → `ceil(font_px * 1.25)`. The 1.25 leading ensures each line exceeds the font height, so adjacent lines cannot touch and a clip band cannot cut a descender. The single source of truth prevents silent regressions from surface-specific constants drifting apart.

## Cross-platform (MSVC vs libstdc++)
- `std::array`/`std::vector` iterators are raw pointers in libstdc++ but class types in MSVC's STL.
  Never declare one as `auto*`: `const auto* it = std::ranges::find_if(...)` compiles clean on Linux
  and fails MSVC with C3535/C2440/C2679. Use plain `const auto`.
- clang-tidy's `readability-qualified-auto` will keep suggesting `auto*` for these on Linux, where
  they happen to be pointers. Do not take the fix — leave a comment saying why. Lint warnings are
  non-fatal in CI, so the suggestion costs nothing to ignore; following it breaks the Windows legs.
- Local runs are Linux-only and cannot catch this class of break. The MSVC CI legs are the gate.

### Paths and files (learned porting the Qt UI to MSVC, `qtui-v1.3.3-beta1`)
- **Never pass `std::filesystem::path::c_str()` to `std::fopen`.** `path::value_type` is `wchar_t`
  on Windows, so it is a hard compile error there. Use `path.string().c_str()`, or better
  `platform::read_file()` — which additionally does one sized read instead of a growing vector, so
  key material isn't strewn across reallocated heap blocks.
- **Never build a file URL as `"file://" + path`.** On Windows the path starts with a drive letter,
  so you get `file://D:/...` where `D:` parses as the URL *host* and the path is silently lost. Use
  `QUrl::fromLocalFile()`.
- **Never compare `QUrl::toLocalFile()` against `path::string()`.** Qt returns forward slashes,
  `string()` returns native separators. Compare `generic_string()` on both sides.
- **Close a file before unlinking it.** POSIX unlinks open files happily; Windows refuses with
  "being used by another process". `Vault::lock()` does NOT close the file (documented) — destroy or
  move-assign over the `Vault`. In a gtest fixture that means doing it in `TearDown()`, which runs
  *before* the fixture destructor. Prefer the `std::error_code` overload of `remove`/`remove_all` in
  teardown so cleanup can't throw out of it.
- **No hardcoded `/tmp`** in tests or defaults — `std::filesystem::temp_directory_path()`. Likewise
  no `mkdtemp()` (POSIX-only; it also rewrites its argument in place, so holding the returned `char*`
  past the buffer's scope dangles).

## Module boundaries
- `src/crypto/` wraps Monocypher — no SDL or UI deps.
- `src/vault/` depends on crypto only.
- `src/image/` depends on crypto + vault for decryption, stb_image for decode.
- `src/gfx/` depends on SDL3 only.
- `src/ui/` depends on gfx + vault + image.
- `src/platform/` wraps OS-specific paths + SDL file dialogs.

## Gallery model
- Galleries nest freely; may hold any mix of images, videos, and sub-galleries as direct children (Phase 46). Grid displays sub-galleries first, then media.

## Testing
- Unit tests in `tests/<module>/`, integration tests exercise full round-trips.
- Crypto tests must include known-answer vectors (Monocypher suite / RFC vectors).
- Tests must pass before a phase is complete.

## Security invariants
See `mem:core` — six hard invariants, never relax them.
