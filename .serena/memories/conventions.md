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

## Module boundaries
- `src/crypto/` wraps Monocypher — no SDL or UI deps.
- `src/vault/` depends on crypto only.
- `src/image/` depends on crypto + vault for decryption, stb_image for decode.
- `src/gfx/` depends on SDL3 only.
- `src/ui/` depends on gfx + vault + image.
- `src/platform/` wraps OS-specific paths + SDL file dialogs.

## Gallery model
- Galleries nest freely; images only in leaf galleries (never mixed sub-gallery + image).

## Testing
- Unit tests in `tests/<module>/`, integration tests exercise full round-trips.
- Crypto tests must include known-answer vectors (Monocypher suite / RFC vectors).
- Tests must pass before a phase is complete.

## Security invariants
See `mem:core` — five hard invariants, never relax them.
