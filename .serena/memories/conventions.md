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
- Any unbounded vault-derived string (paths, names, tags) drawn into a fixed-width box must be middle-elided first via `ui::fit_text(font, s, max_w)` (widgets.h) — never `draw_text` it raw (PR #54 swept the whole UI for this).
- **Never hardcode a text-line pitch.** Derive it from `font.pixel_height()` via `ui::line_pitch(font_px)` → `ceil(font_px * 1.25)`. The 1.25 leading ensures each line exceeds the font height, so adjacent lines cannot touch and a clip band cannot cut a descender. The single source of truth prevents silent regressions from surface-specific constants drifting apart.

## Cross-platform (MSVC vs libstdc++)
- `std::array`/`std::vector` iterators are raw pointers in libstdc++ but class types in MSVC's STL.
  Never declare one as `auto*`: `const auto* it = std::ranges::find_if(...)` compiles clean on Linux
  and fails MSVC with C3535/C2440/C2679. Use plain `const auto`.
- clang-tidy's `readability-qualified-auto` will keep suggesting `auto*` for these on Linux, where
  they happen to be pointers. Do not take the fix — leave a comment saying why. Lint warnings are
  non-fatal in CI, so the suggestion costs nothing to ignore; following it breaks the Windows legs.
- Local runs are Linux-only and cannot catch this class of break. The MSVC CI legs are the gate.

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
