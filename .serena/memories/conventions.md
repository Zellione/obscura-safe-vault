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
- **`scripts/test.sh` alone is not "tests pass".** It is Debug + FFmpeg — the one
  configuration where several whole classes of break are invisible. Before calling a
  phase done, also run `scripts/test.sh --release` and
  `bin/premake5 --no-av ninja --cc=gcc && ninja Debug_x64` (then `scripts/gen.sh` to
  restore the normal build files). PR #161 shipped three separate failures that Debug
  +FFmpeg could not see; each cost a CI round-trip to find one at a time, because
  ninja stops at the first error and reports only that one.
- **Never assert on a size that the compressor decides.** A fixture that pushes highly
  compressible bytes through chunk framing and asserts the stored/wasted result exceeds
  a threshold is asserting a property of deflate. `migration_job_compaction_reclaims_
  orphaned_chunks` cleared `AUTO_COMPACT_MIN_WASTE` by under 4% (272286 vs 262144) that
  way. Use incompressible bytes (`job_incompressible`, fixed-seed splitmix64) so stored
  size tracks raw size, and leave a multiple of margin.
- **A test that has never run on Windows is not a passing test.** When an MSVC leg is
  red for a build reason, every test in it is unverified, and fixing the build can
  expose real failures that were hidden for a whole phase. Do not read "MSVC was
  already failing" as "MSVC is fine".
- **Deletion leaves different state per platform** — see `mem:module/vault`
  `auto_reclaim_space`. A test that deletes media and then asserts on `wasted_bytes()`
  must keep the waste under the auto-reclaim gate (`waste * AUTO_COMPACT_WASTE_RATIO
  < size`), or it silently asserts Linux-only behaviour.

## Build configurations (beyond MSVC-vs-libstdc++ source differences)
- An `inline` function defined in a header must be *included*, never hand-forward-declared
  in a `.cpp`. A non-inline declaration links at `-O0` only by luck — off the weak
  out-of-line copies other TUs emit — and fails every Release build with an undefined
  reference. `tests/ui/test_migration_job.cpp` did this to `fixtures::load_webp` /
  `load_anim_webp` and broke all three Release legs.
- A `static` helper used only inside `#ifdef OSV_VENDORED_AV` must carry the same guard,
  or the `--no-av` leg fails on `-Werror=unused-function`.

## Security invariants
See `mem:core` — six hard invariants, never relax them.
