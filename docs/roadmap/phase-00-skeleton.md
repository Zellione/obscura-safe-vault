## Phase 0 — Skeleton & minimal window ✅

**Goal:** Establish the project structure, build system, and a compilable application that opens a window.

### Tasks
- [x] Choose and record the full technology stack (see `CLAUDE.md`).
- [x] Write `ROADMAP.md` and `CLAUDE.md`.
- [x] Add vendored git submodules: `vendor/SDL3`, `vendor/monocypher`, `vendor/stb`.
- [x] Write `premake5.lua` (workspace + monocypher static lib + osv app project).
- [x] Write `scripts/setup.sh` — init submodules, download `bin/premake5`, cmake-build SDL3.
- [x] Write `scripts/gen.sh` / `scripts/build.sh`.
- [x] Create module stub files: `src/crypto/`, `src/vault/`, `src/image/`, `src/ui/`, `src/platform/`.
- [x] Implement `src/gfx/window.{h,cpp}` — SDL3 window + SDL_Renderer init/shutdown/event loop.
- [x] Implement `src/app/app.{h,cpp}` + `src/app/main.cpp` — state machine, clear+present loop.

### Acceptance criterion
Running `build/bin/Debug/osv` opens a titled window that clears to a dark colour, responds to window-close and `Escape`, and exits cleanly with no errors printed to stderr.
