# Phase 8 — Cross-Platform Ports Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Windows (MSVC) and macOS (AppleClang) builds + a 3-OS GitHub Actions CI matrix + per-platform packaging, per ROADMAP Phase 8.

**Architecture:** The platform-specific C++ (`_WIN32`/`__APPLE__` branches in `random.cpp`, `secure_mem.h`, `file_util.h`) already exists; this phase is build-system, script, CI, and packaging work plus three small portability fixes (an MSVC-invalid format spec, a hardcoded x86_64 architecture, and cwd-relative font loading). Windows CI uses `premake5 vs2022` + MSBuild; macOS CI uses `premake5 gmake2` + make. Packaging produces a Linux tar.gz (+install.sh), a Windows NSIS installer, and a macOS .app bundle (ad-hoc signed; real code-signing needs an Apple Developer ID and is out of scope).

**Tech Stack:** premake5 beta8, GitHub Actions (`ubuntu-latest`, `windows-latest`, `macos-latest`), CMake (vendored SDL3), NSIS (preinstalled on Windows runners), bash/batch.

**Constraint:** Only Linux is testable locally. Windows/macOS validation happens on CI after the PR is opened — expect an iterate-on-CI loop at the end.

---

### Task 1: Source portability fixes

**Files:**
- Modify: `src/crypto/random.cpp:27` (invalid `{:lx}` format spec — compile error on MSVC)
- Modify: `src/app/app.cpp:24` (font path falls back to `SDL_GetBasePath()` so packaged apps find assets)

- [ ] **Step 1: Fix the MSVC-invalid format spec in random.cpp**

`std::println` validates format strings at compile time; `{:lx}` is not a valid spec (only uppercase `L` exists, as the locale option). This branch only compiles on Windows, so it has never been built. Replace:

```cpp
        std::println(stderr, "[crypto] BCryptGenRandom failed (0x{:lx})",
                     static_cast<unsigned long>(s));
```

with:

```cpp
        std::println(stderr, "[crypto] BCryptGenRandom failed (0x{:08x})",
                     static_cast<uint32_t>(s));
```

- [ ] **Step 2: Font path fallback in app.cpp**

In `App::init` (around line 24), the font bakes from the cwd-relative `OSV_DEFAULT_FONT`. A packaged app (installed Linux/Windows, macOS .app) is rarely launched with the repo root as cwd. Try cwd first (dev workflow unchanged), then resolve relative to the executable via `SDL_GetBasePath()` (which returns `Contents/Resources/` inside a macOS bundle — exactly where the packaging task puts `assets/`). Replace:

```cpp
    font_ready_ = font_.bake_from_file(OSV_DEFAULT_FONT, 28.0f);
```

with:

```cpp
    // Dev runs launch from the repo root (cwd-relative); packaged apps resolve
    // assets next to the executable (= Contents/Resources inside a mac bundle).
    font_ready_ = font_.bake_from_file(OSV_DEFAULT_FONT, 28.0f);
    if (!font_ready_) {
        if (const char* base = SDL_GetBasePath(); base) {
            const std::string fallback = std::string{base} + OSV_DEFAULT_FONT;
            font_ready_ = font_.bake_from_file(fallback.c_str(), 28.0f);
        }
    }
```

(`SDL_GetBasePath()` in SDL3 returns a cached `const char*` owned by SDL — do not free. Check the existing includes: `app.cpp` must include `<SDL3/SDL.h>` (it already does, transitively via window.h — verify) and `<string>`.)

- [ ] **Step 3: Build + run full test suite**

Run: `scripts/test.sh`
Expected: all tests pass (146+).

- [ ] **Step 4: Commit**

```bash
git add src/crypto/random.cpp src/app/app.cpp
git commit -m "fix: MSVC-invalid format spec + asset path fallback for packaged apps"
```

---

### Task 2: premake5.lua cross-platform configs

**Files:**
- Modify: `premake5.lua` (link_sdl3 multi-path, macOS architecture/frameworks, Windows libs)

- [ ] **Step 1: Make `link_sdl3()` find the vendored static lib on all three platforms**

CMake names the static lib `libSDL3.a` on Linux/macOS but `SDL3-static.lib` on Windows; the VS generator additionally nests it under a per-config dir. Replace the function body's detection with:

```lua
local function link_sdl3()
    local sdl3_build = path.join(os.getcwd(), "vendor/SDL3/build")
    local candidates = {
        { lib = "SDL3",        file = "libSDL3.a",              dir = sdl3_build },                          -- Linux/macOS (Ninja/Make)
        { lib = "SDL3-static", file = "SDL3-static.lib",        dir = sdl3_build },                          -- Windows (Ninja)
        { lib = "SDL3-static", file = "Release/SDL3-static.lib", dir = path.join(sdl3_build, "Release") },   -- Windows (VS generator)
    }
    for _, c in ipairs(candidates) do
        if os.isfile(path.join(sdl3_build, c.file)) then
            includedirs { "vendor/SDL3/include" }
            libdirs     { c.dir }
            links       { c.lib }
            defines     { "OSV_VENDORED_SDL3" }
            return
        end
    end
    -- Fall back to a system SDL3 install.
    filter "system:linux"
        includedirs { "/usr/include/SDL3" }
        links       { "SDL3" }
    filter "system:windows"
        includedirs { "C:/SDL3/include" }
        libdirs     { "C:/SDL3/lib/x64" }
        links       { "SDL3" }
    filter "system:macosx"
        includedirs { "/usr/local/include/SDL3" }
        links       { "SDL3" }
    filter {}
end
```

- [ ] **Step 2: Stop forcing x86_64 on macOS**

`macos-latest` runners (and the owner's eventual test machines) are arm64; forcing `x86_64` would produce binaries that can't link against a natively-built SDL3. Replace:

```lua
    filter "platforms:x64"
        architecture "x86_64"
```

with:

```lua
    -- x86_64 on Linux/Windows; macOS uses the toolchain default so arm64
    -- machines build natively (the vendored SDL3 is built natively by cmake).
    filter { "platforms:x64", "system:linux or windows" }
        architecture "x86_64"
```

- [ ] **Step 3: Add per-platform link extras shared by `osv` and `osv_tests`**

Factor a helper next to `link_sdl3()`:

```lua
-- OS libraries/frameworks needed by a static SDL3 link + our crypto shim.
local function link_platform_extras()
    filter "system:linux"
        links { "dl", "pthread", "m" }
    filter "system:windows"
        -- bcrypt: crypto/random.cpp. The rest: static SDL3.
        links { "bcrypt", "winmm", "imm32", "version", "setupapi",
                "ole32", "oleaut32", "advapi32", "shell32", "user32",
                "gdi32", "uuid" }
    filter "system:macosx"
        links {
            "iconv",
            "Cocoa.framework", "Carbon.framework", "IOKit.framework",
            "ForceFeedback.framework", "CoreAudio.framework",
            "AudioToolbox.framework", "AVFoundation.framework",
            "CoreMedia.framework", "CoreVideo.framework",
            "CoreHaptics.framework", "GameController.framework",
            "Metal.framework", "QuartzCore.framework",
            "UniformTypeIdentifiers.framework",
        }
    filter {}
end
```

In the `osv` project, replace the existing `filter "system:linux" links {...} filter "system:windows" links {...} filter {}` block with `link_platform_extras()`. Same in `osv_tests` (its Windows block already had `bcrypt`; the helper covers it).

Additionally, `osv` should not open a console window on Windows release builds — add inside the `osv` project:

```lua
    -- GUI app on Windows (no console window) for Release; keep the console in
    -- Debug so stderr logging is visible.
    filter { "system:windows", "configurations:Release" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"   -- keep standard main(), not WinMain
    filter {}
```

- [ ] **Step 4: Regenerate + full local rebuild + tests**

Run: `scripts/test.sh && scripts/build.sh`
Expected: all tests pass; `osv` links.

- [ ] **Step 5: Commit**

```bash
git add premake5.lua
git commit -m "build: premake configs for Windows (MSVC) and macOS (native arch + frameworks)"
```

---

### Task 3: Portable shell scripts + setup.bat

**Files:**
- Modify: `scripts/setup.sh`, `scripts/build.sh`, `scripts/test.sh` (`nproc` → portable core count; setup.sh claims Darwin support but `nproc` doesn't exist there)
- Create: `scripts/setup.bat`

- [ ] **Step 1: Portable core count in the three shell scripts**

In each script that uses `$(nproc)`, add near the top (after `cd "$REPO_ROOT"`):

```bash
# Core count: nproc is Linux-only; macOS uses sysctl.
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
```

and replace every `"$(nproc)"` with `"$NPROC"`.

`build.sh` also uses `${CONFIG,,}` (bash 4 lowercase expansion) — macOS ships bash 3.2. Replace `config="${CONFIG,,}_x64"` with:

```bash
    CONFIG_LC="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
    make config="${CONFIG_LC}_x64" -j"$NPROC"
```

(same change in `test.sh`'s gmake branch).

- [ ] **Step 2: Create `scripts/setup.bat`**

Windows equivalent of setup.sh: submodules → premake5.exe → SDL3 via cmake (VS default generator so no vcvars needed; `link_sdl3()` finds `build/Release/SDL3-static.lib`). Windows 10+ ships `curl.exe` and `tar.exe`.

```bat
@echo off
rem setup.bat — one-time project bootstrap (Windows).
rem Run from anywhere; requires git, cmake, and Visual Studio 2022 (C++ workload).
rem After it completes:
rem   bin\premake5.exe vs2022     <- generate ObscuraSafeVault.sln
rem   then build with msbuild or the VS IDE.

setlocal enabledelayedexpansion
pushd "%~dp0.."

echo ==^> Initialising git submodules...
git submodule update --init --recursive || goto :fail

set PREMAKE_VERSION=5.0.0-beta8
if exist bin\premake5.exe (
    echo ==^> premake5 already present at bin\premake5.exe — skipping download.
) else (
    echo ==^> Downloading premake5 %PREMAKE_VERSION%...
    if not exist bin mkdir bin
    curl -fsSL "https://github.com/premake/premake-core/releases/download/v%PREMAKE_VERSION%/premake-%PREMAKE_VERSION%-windows.zip" -o "%TEMP%\premake5.zip" || goto :fail
    tar -xf "%TEMP%\premake5.zip" -C bin || goto :fail
    echo     premake5 ready at bin\premake5.exe
)

if exist vendor\SDL3\build\Release\SDL3-static.lib (
    echo ==^> vendored SDL3 already built — skipping cmake.
) else (
    echo ==^> Building vendored SDL3 ^(static^)...
    cmake -S vendor\SDL3 -B vendor\SDL3\build ^
        -DSDL_STATIC=ON -DSDL_SHARED=OFF ^
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF || goto :fail
    cmake --build vendor\SDL3\build --config Release --parallel || goto :fail
    echo     SDL3-static.lib built at vendor\SDL3\build\Release\
)

echo.
echo Setup complete. Next steps:
echo   bin\premake5.exe vs2022                                  ^&rem generate the VS solution
echo   msbuild ObscuraSafeVault.sln /m /p:Configuration=Debug /p:Platform=x64
popd
exit /b 0

:fail
echo setup.bat failed (see error above).
popd
exit /b 1
```

- [ ] **Step 3: Verify**

Run: `bash -n scripts/setup.sh scripts/build.sh scripts/test.sh && scripts/test.sh`
Expected: no syntax errors; tests pass. (setup.bat is validated on the Windows CI runner — CI calls the same cmake/premake commands.)

- [ ] **Step 4: Commit**

```bash
git add scripts/setup.sh scripts/build.sh scripts/test.sh scripts/setup.bat
git commit -m "build: setup.bat for Windows; make shell scripts macOS-portable"
```

---

### Task 4: CI — Windows and macOS jobs

**Files:**
- Modify: `.github/workflows/ci.yml` (add `windows` and `macos` jobs after `build-and-test`)

- [ ] **Step 1: Add the Windows job**

Pattern matches the existing Linux job: cache premake + SDL3, build, run tests headless. MSVC env via `ilammy/msvc-dev-cmd` (gives cl + ninja + msbuild on PATH).

```yaml
  windows:
    name: MSVC / ${{ matrix.config }} / Windows
    runs-on: windows-latest

    strategy:
      fail-fast: false
      matrix:
        config: [Debug, Release]

    steps:
      - name: Checkout (with submodules)
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Set up MSVC environment
        uses: ilammy/msvc-dev-cmd@v1

      - name: Cache premake5 binary
        uses: actions/cache@v4
        id: cache-premake5
        with:
          path: bin/premake5.exe
          key: premake5-5.0.0-beta8-windows

      - name: Download premake5
        if: steps.cache-premake5.outputs.cache-hit != 'true'
        shell: pwsh
        run: |
          New-Item -ItemType Directory -Force -Path bin | Out-Null
          curl.exe -fsSL "https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-windows.zip" -o "$env:TEMP\premake5.zip"
          tar -xf "$env:TEMP\premake5.zip" -C bin

      - name: Cache vendored SDL3 build
        uses: actions/cache@v4
        id: cache-sdl3
        with:
          path: vendor/SDL3/build
          key: sdl3-windows-${{ hashFiles('.gitmodules') }}-${{ runner.arch }}

      - name: Build vendored SDL3
        if: steps.cache-sdl3.outputs.cache-hit != 'true'
        run: |
          cmake -S vendor/SDL3 -B vendor/SDL3/build `
            -DCMAKE_BUILD_TYPE=Release `
            -DSDL_STATIC=ON -DSDL_SHARED=OFF `
            -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF `
            -G "Ninja"
          cmake --build vendor/SDL3/build --parallel

      - name: Generate VS2022 solution
        run: bin\premake5.exe vs2022

      - name: Build (${{ matrix.config }})
        run: msbuild ObscuraSafeVault.sln /m "/p:Configuration=${{ matrix.config }}" /p:Platform=x64

      - name: Run tests (${{ matrix.config }})
        env:
          SDL_VIDEODRIVER: dummy
        run: ./build/bin/${{ matrix.config }}/osv_tests.exe
```

- [ ] **Step 2: Add the macOS job**

```yaml
  macos:
    name: AppleClang / ${{ matrix.config }} / macOS
    runs-on: macos-latest

    strategy:
      fail-fast: false
      matrix:
        config: [Debug, Release]

    steps:
      - name: Checkout (with submodules)
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Cache premake5 binary
        uses: actions/cache@v4
        id: cache-premake5
        with:
          path: bin/premake5
          key: premake5-5.0.0-beta8-macosx

      - name: Download premake5
        if: steps.cache-premake5.outputs.cache-hit != 'true'
        run: |
          mkdir -p bin
          curl -fsSL \
            "https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-macosx.tar.gz" \
            -o /tmp/premake5.tar.gz
          tar -xzf /tmp/premake5.tar.gz -C bin/
          chmod +x bin/premake5

      - name: Cache vendored SDL3 build
        uses: actions/cache@v4
        id: cache-sdl3
        with:
          path: vendor/SDL3/build
          key: sdl3-macos-${{ hashFiles('.gitmodules') }}-${{ runner.arch }}

      - name: Build vendored SDL3
        if: steps.cache-sdl3.outputs.cache-hit != 'true'
        run: |
          cmake -S vendor/SDL3 -B vendor/SDL3/build \
            -DCMAKE_BUILD_TYPE=Release \
            -DSDL_STATIC=ON -DSDL_SHARED=OFF \
            -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF
          cmake --build vendor/SDL3/build --parallel 3

      - name: Generate Makefiles
        run: bin/premake5 gmake2

      - name: Build (${{ matrix.config }})
        run: |
          CONFIG_LC="$(printf '%s' "${{ matrix.config }}" | tr '[:upper:]' '[:lower:]')"
          make config="${CONFIG_LC}_x64" -j3

      - name: Run tests (${{ matrix.config }})
        env:
          SDL_VIDEODRIVER: dummy
        run: ./build/bin/${{ matrix.config }}/osv_tests
```

- [ ] **Step 3: Validate YAML locally**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml'))" && echo OK`
Expected: `OK`

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: Windows (MSVC) and macOS (AppleClang) build+test jobs"
```

---

### Task 5: Packaging

**Files:**
- Create: `scripts/package.sh` (Linux tar.gz + install.sh)
- Create: `scripts/package_macos.sh` (.app bundle, ad-hoc signed, zipped)
- Create: `packaging/windows/osv.nsi` (NSIS installer)
- Modify: `.github/workflows/ci.yml` (package steps appended to the Release leg of each OS job)

- [ ] **Step 1: `scripts/package.sh`**

```bash
#!/usr/bin/env bash
# package.sh — build a Linux release tarball: dist/osv-<version>-linux-<arch>.tar.gz
# Contains: osv binary, assets/, LICENSE, README.md, install.sh (to ~/.local by default).
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

VERSION="${OSV_VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}"
ARCH="$(uname -m)"
BIN="build/bin/Release/osv"

[[ -x "$BIN" ]] || { echo "Release binary missing — run: scripts/build.sh --release"; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PKG="osv-${VERSION}-linux-${ARCH}"
mkdir -p "$STAGE/$PKG"

cp "$BIN" "$STAGE/$PKG/"
cp -r assets "$STAGE/$PKG/"
cp LICENSE README.md "$STAGE/$PKG/"

cat > "$STAGE/$PKG/install.sh" <<'EOF'
#!/usr/bin/env bash
# Installs osv to PREFIX (default ~/.local): bin/osv + share/osv/assets.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
mkdir -p "$PREFIX/bin" "$PREFIX/share/osv"
install -m 755 "$HERE/osv" "$PREFIX/bin/osv"
cp -r "$HERE/assets" "$PREFIX/share/osv/"
# The binary resolves assets next to itself as a fallback; symlink them in.
ln -sfn "$PREFIX/share/osv/assets" "$PREFIX/bin/assets"
echo "Installed: $PREFIX/bin/osv"
EOF
chmod +x "$STAGE/$PKG/install.sh"

mkdir -p dist
tar -czf "dist/${PKG}.tar.gz" -C "$STAGE" "$PKG"
echo "Packaged: dist/${PKG}.tar.gz"
```

- [ ] **Step 2: `scripts/package_macos.sh`**

```bash
#!/usr/bin/env bash
# package_macos.sh — build dist/ObscuraSafeVault-<version>-macos-<arch>.zip
# containing "Obscura Safe Vault.app". Ad-hoc signed (codesign -s -); real
# Developer-ID signing/notarisation requires Apple credentials (out of scope).
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

VERSION="${OSV_VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}"
ARCH="$(uname -m)"
BIN="build/bin/Release/osv"
[[ -x "$BIN" ]] || { echo "Release binary missing — run: scripts/build.sh --release"; exit 1; }

APP="dist/Obscura Safe Vault.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/osv"
# SDL_GetBasePath() inside a bundle = Contents/Resources/ — assets go there.
cp -r assets "$APP/Contents/Resources/"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>             <string>Obscura Safe Vault</string>
    <key>CFBundleDisplayName</key>      <string>Obscura Safe Vault</string>
    <key>CFBundleIdentifier</key>       <string>dev.zellione.obscurasafevault</string>
    <key>CFBundleVersion</key>          <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key> <string>${VERSION}</string>
    <key>CFBundleExecutable</key>       <string>osv</string>
    <key>CFBundlePackageType</key>      <string>APPL</string>
    <key>NSHighResolutionCapable</key>  <true/>
    <key>LSMinimumSystemVersion</key>   <string>13.0</string>
</dict>
</plist>
EOF

codesign --force --deep -s - "$APP" || echo "codesign (ad-hoc) failed — unsigned bundle"

mkdir -p dist
ZIP="dist/ObscuraSafeVault-${VERSION}-macos-${ARCH}.zip"
rm -f "$ZIP"
(cd dist && zip -qry "$(basename "$ZIP")" "Obscura Safe Vault.app")
echo "Packaged: $ZIP"
```

- [ ] **Step 3: `packaging/windows/osv.nsi`**

```nsis
; osv.nsi — NSIS installer for Obscura Safe Vault.
; Build (from the repo root, after a Release build):
;   makensis /DVERSION=x.y.z packaging\windows\osv.nsi
; Output: dist\ObscuraSafeVault-<version>-setup.exe

!ifndef VERSION
  !define VERSION "dev"
!endif

Name "Obscura Safe Vault"
OutFile "..\..\dist\ObscuraSafeVault-${VERSION}-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\ObscuraSafeVault"
RequestExecutionLevel user
Unicode true
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  File "..\..\build\bin\Release\osv.exe"
  File /r "..\..\assets"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\Obscura Safe Vault.lnk" "$INSTDIR\osv.exe"
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\Obscura Safe Vault.lnk"
  RMDir /r "$INSTDIR\assets"
  Delete "$INSTDIR\osv.exe"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
```

- [ ] **Step 4: CI package steps**

Append to each OS job, gated on the Release leg (`if: matrix.config == 'Release'`):

Linux (`build-and-test` job, additionally gated on `matrix.compiler == 'gcc'`):
```yaml
      - name: Package (tar.gz)
        if: matrix.config == 'Release' && matrix.compiler == 'gcc'
        run: scripts/package.sh
      - name: Upload package
        if: matrix.config == 'Release' && matrix.compiler == 'gcc'
        uses: actions/upload-artifact@v4
        with:
          name: osv-linux-tarball
          path: dist/*.tar.gz
          retention-days: 7
```
(Note: the Linux job builds with `ninja ${config}_x64` which builds all targets including `osv` — package.sh's input exists.)

Windows:
```yaml
      - name: Package (NSIS installer)
        if: matrix.config == 'Release'
        run: makensis /DVERSION=dev packaging\windows\osv.nsi
      - name: Upload package
        if: matrix.config == 'Release'
        uses: actions/upload-artifact@v4
        with:
          name: osv-windows-installer
          path: dist/*.exe
          retention-days: 7
```
(NSIS is preinstalled on `windows-latest`; `dist\` must exist — add `New-Item -ItemType Directory -Force dist` before makensis or let OutFile create it… NSIS does **not** create missing output dirs: create it first.)

macOS:
```yaml
      - name: Package (.app bundle)
        if: matrix.config == 'Release'
        run: scripts/package_macos.sh
      - name: Upload package
        if: matrix.config == 'Release'
        uses: actions/upload-artifact@v4
        with:
          name: osv-macos-app
          path: dist/*.zip
          retention-days: 7
```

- [ ] **Step 5: Verify package.sh locally**

Run: `scripts/build.sh --release && scripts/package.sh && tar -tzf dist/*.tar.gz | head`
Expected: tarball listing shows `osv`, `assets/fonts/NotoSans-Regular.ttf`, `install.sh`, `LICENSE`.

- [ ] **Step 6: Commit**

```bash
git add scripts/package.sh scripts/package_macos.sh packaging/windows/osv.nsi .github/workflows/ci.yml
git commit -m "feat: per-platform packaging (tar.gz / NSIS / .app) + CI artifacts"
```

---

### Task 6: Docs — CLAUDE.md platform notes + ROADMAP check-off

**Files:**
- Modify: `CLAUDE.md` (add a "Platform-specific build notes" section after "Build / run / test")
- Modify: `ROADMAP.md` (tick Phase 8 boxes, add Status + notes, following the established per-phase format)

- [ ] **Step 1: CLAUDE.md** — document: Windows = `setup.bat` → `premake5 vs2022` → msbuild; macOS = `setup.sh` → `gen.sh --gmake` → `build.sh --gmake`; macOS builds native arch (no forced x86_64); packaging scripts; NSIS note.

- [ ] **Step 2: ROADMAP.md** — mark Phase 8 tasks `[x]`, add `**Status:** ✅ …` + a "Notes / decisions" block (HiDPI was already handled by `SDL_WINDOW_HIGH_PIXEL_DENSITY`; ad-hoc codesign only; chosen packaging formats; macOS uses gmake2 in CI).

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md ROADMAP.md
git commit -m "docs: Phase 8 platform build notes + roadmap status"
```

---

### Task 7: PR + iterate on CI

- [ ] **Step 1:** Final local gate: `scripts/test.sh && scripts/test.sh --asan`
- [ ] **Step 2:** Push branch `phase-8-cross-platform`, open PR titled "Phase 8: cross-platform ports — Windows/macOS builds, CI matrix, packaging".
- [ ] **Step 3:** Watch CI (`gh run watch` / `gh pr checks`). Windows + macOS legs are the first-ever builds on those platforms — expect compiler/link errors; fix, push, repeat until all jobs are green.

**Acceptance criterion (from ROADMAP):** CI passes on all three platforms; a developer can clone on Windows or macOS and build with a single setup script.

---

## Self-review notes

- **Spec coverage:** Windows premake config (Task 2), setup.bat (Task 3), macOS config (Task 2; Xcode4 generator deliberately *not* used — gmake2 is battle-tested and the ROADMAP's intent is "builds on macOS"; xcode4 can be generated by anyone via `premake5 xcode4`, document in CLAUDE.md), CI matrix (Task 4), packaging (Task 5), CLAUDE.md (Task 6). HiDPI: already implemented (window.cpp:14), documented in ROADMAP notes.
- **Known risk:** premake beta8's `vs2022` output, MSVC `/W4` noise, SDL3 static link closure on Windows/macOS — all only verifiable on CI (Task 7 loop).
- **Type consistency:** n/a (build scripts).
