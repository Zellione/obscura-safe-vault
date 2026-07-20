## Phase 8 — Cross-platform ports ✅

> **Note:** macOS support described in this phase was later removed entirely
> (CI, build pipeline, and source-level `#ifdef __APPLE__` branches) — see
> `mem:tech_stack` for current platform support. The Windows NSIS installer
> described below was also later replaced by a portable `.zip` (`scripts/package.ps1`)
> — the installer did nothing a folder copy didn't, and left Start Menu/uninstaller
> traces on the system for what is deliberately a leave-no-trace tool. This
> historical record is preserved as-shipped and not rewritten.

**Goal:** Windows and macOS build configs and CI pipeline.

### Tasks
- [x] **Windows** — premake5.lua `filter "system:windows"` config; link against vendored SDL3 static build (cmake in `setup.bat`); tested on the windows-latest CI runner (Windows Server 2022+, same toolchain as Windows 10/11).
- [x] **macOS** — premake5.lua `filter "system:macosx"` (frameworks + native arch); SDL3 cmake build; HiDPI handling; tested on the macos-latest CI runner (macOS 14+, arm64).
- [x] **CI** — GitHub Actions matrix: Linux (gcc + clang), Windows (MSVC), macOS (AppleClang). All tests run on each.
- [x] **Packaging** — Linux: `.tar.gz` + `install.sh`; Windows: NSIS installer; macOS: `.app` bundle (ad-hoc signed).
- [x] `scripts/setup.bat` — Windows equivalent of `setup.sh`.
- [x] Update `CLAUDE.md` with platform-specific build notes.

### Acceptance criterion
CI passes on all three platforms. A developer can clone the repo on Windows or macOS and build a working app with a single setup script.

**Status:** ✅ CI green on Linux (gcc+clang × Debug/Release + ASAN gate), Windows
(MSVC × Debug/Release), and macOS (AppleClang × Debug/Release); Release legs upload a
Linux tarball, an NSIS installer, and a zipped `.app` bundle as artifacts.

> **Notes / decisions made during implementation**
> - **HiDPI** was already handled since Phase 0: every window is created with
>   `SDL_WINDOW_HIGH_PIXEL_DENSITY` (window.cpp), and the .app bundle's Info.plist sets
>   `NSHighResolutionCapable`.
> - **macOS generator:** CI uses `gmake2` (battle-tested) rather than the ROADMAP's
>   suggested Xcode4 generator; `premake5 xcode4` remains available for IDE users.
>   macOS builds the native architecture (no forced x86_64) so arm64 machines link
>   against the natively-built vendored SDL3.
> - **Windows:** built via `premake5 vs2022` + MSBuild. Release builds `osv` as a
>   `WindowedApp` (no console) keeping the standard `main()` entrypoint. The MSVC
>   branch of `crypto/random.cpp` had a latent compile error (invalid `{:lx}` format
>   spec, never built on Linux) — fixed.
> - **Asset path portability:** packaged apps aren't launched from the repo root, so
>   the font now falls back to `SDL_GetBasePath()` (exe dir; `Contents/Resources/` in
>   a mac bundle) when the cwd-relative path misses. install.sh symlinks `assets/`
>   next to the installed binary for the same reason.
> - **Code-signing:** the .app is ad-hoc signed (`codesign -s -`); Developer-ID
>   signing + notarisation need an Apple Developer account and are deferred.
> - **Script portability:** `nproc` (Linux-only) and `${VAR,,}` (bash 4; macOS ships
>   3.2) were removed from the shell scripts.
