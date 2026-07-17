## Phase 42 — ThreadSanitizer CI leg ✅

**Goal:** Phase 41 introduced this project's first genuinely concurrent data
pipeline outside the already-proven `image::DecodeWorker` pattern
(`media::VideoDecodeWorker`, a producer/consumer packet-and-frame queue
across two threads). The existing CI only ran ASAN/UBSan
(`scripts/test.sh --asan`), which does not catch data races. This phase adds
a ThreadSanitizer (TSan) CI leg so a race in that code — or any future
threading this codebase grows — fails CI immediately.

Full design rationale: `docs/superpowers/specs/2026-07-17-phase42-tsan-ci-design.md`
(kept local/untracked per this repo's `docs/`-is-gitignored convention — this
file is the durable summary).

### Tasks
- **`premake5.lua`** — new `--tsan` `newoption`, applying
  `-fsanitize=thread -fno-omit-frame-pointer` to every project via a
  `filter { "options:tsan", "toolset:gcc or clang" }` block, mirroring the
  existing `--asan` filter. A hard error if `--asan` and `--tsan` are both
  passed (they cannot be combined in one binary). Unlike `--asan`, no
  parallel `vendor/codecs-prefix-tsan` — the codec-prefix-selection blocks
  are untouched, so `--tsan` builds always link the plain
  `vendor/codecs-prefix`/`vendor/SDL3/build` every other job already builds.
  This is a deliberate scope difference from ASAN: ASAN's codec rebuild
  exists to catch memory bugs *inside* the vendored C parsers on untrusted
  input, a concern specific to that sanitizer; TSan's job is catching races
  in *our own* threading code, which is fully exercised without the codec
  libraries themselves being instrumented.
- **`scripts/test.sh`** — new `--tsan` flag (mirrors `--asan`'s shape,
  mutually exclusive with it), setting
  `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1` on the run step.
- **`.github/workflows/ci.yml`** — new `tests-tsan` job: Linux, gcc-14,
  Debug-only, running on every PR/push (same cadence as `tests-asan`, not
  the manual/weekly cadence of `tests-asan-codecs`) since a threading
  regression should fail the PR that introduced it. Reuses the existing
  `codecs-v4-...`/`sdl3-linux-...` CI cache keys — no new vendored-build
  cache dimension.
- **`.github/actions/setup-apt-deps/` + `.github/actions/setup-premake-sdl3/`**
  — two new local composite actions extracting the apt-install and
  premake5/SDL3-build steps that `build-and-test`, `tests-asan`,
  `tests-asan-codecs`, and the new `tests-tsan` all needed identically,
  so the new job didn't become a 5th verbatim copy of that ~80-line block.
  `sonarqube` was deliberately left out (its `if: env.SONAR_TOKEN != ''`
  guard on every step is a pre-existing special case, not worth folding in).

### Acceptance criterion
`scripts/test.sh --tsan` runs clean (no TSan reports, `0 failed`), and the
new `tests-tsan` CI job is green, alongside every existing job staying green
(`scripts/test.sh` and `scripts/test.sh --asan` unaffected).
