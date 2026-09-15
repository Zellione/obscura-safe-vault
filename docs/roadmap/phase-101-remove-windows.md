# Phase 101 — Remove Windows support ✅

**Status:** ✅ shipped
**Date:** 2026-09-15

The Linux-only dev box hadn't seen a Windows CI build green in months, and
the MSVC leg's ccache + MSYS2 + NASM dance cost ~290 lines of `ci.yml` for
one matrix dimension with no active users. The platform support set becomes
**Linux x86_64 only** (macOS was dropped earlier — its `#error` guard in
`crypto/random.cpp` is kept as documentation).

## What changed

### Source tree — every `#ifdef _WIN32` is gone

`src/` collapsed to POSIX-only implementations:

- `crypto/random.cpp` — drops the `BCryptGenRandom` / `<bcrypt.h>` /
  `<windows.h>` block; keeps the `getrandom` path.
- `crypto/secure_mem.h` — collapses the 5 `_WIN32` blocks (`VirtualLock` /
  `VirtualUnlock` / `VirtualQuery` / `SYSTEM_INFO` page-size /
  `mlock_fail_hint`'s "working set" branch) to POSIX.
- `platform/atomic_file.cpp` — drops the entire `CreateFileW` +
  `GetFinalPathNameByHandleW` containment path (final-path-within check).
- `platform/harden.cpp` — drops `SetProcessWorkingSetSize` +
  `AddSecureMemoryCacheCallback` and the Windows-only core-dump no-op;
  `redirect_diagnostics_to_log_file()` becomes a documented POSIX no-op
  (stderr stays on the terminal).
- `platform/locale_init.h` — drops the `#if !defined(_WIN32)` guard; the
  `setlocale(LC_CTYPE, ...)` call is now unconditional (the comment was
  Linux-only all along — libarchive's locale-dependent behaviour on Win32
  was described as already-handled).
- `platform/path_utf8.h` — `fopen_path` / `freopen_path` collapse to plain
  `fopen` / `freopen` (Linux `path::c_str()` already IS the native
  `char*`); the rationale comment shrinks to its Linux-only claim.
- `platform/paths.cpp` — drops the entire Windows DACL block
  (`OwnerOnlyAcl`, `build_owner_only_acl`, `SECURITY_DESCRIPTOR`,
  `CreateFileW` for keyfiles/vaults); POSIX `O_EXCL` + mode 0600 only.
- `ui/archive_reader.cpp` — `open_filenames_portable` uses the narrow
  `archive_read_open_filenames()` (Linux paths are already UTF-8).
- `vault/file_util.h` — drops `_fseeki64` / `_ftelli64` / `_commit` /
  `_chsize_s` / `_fstat64` branches.

### Tests — every `#ifndef _WIN32` / `#if defined(_WIN32)` guard is gone

- `crypto/test_secure_mem.cpp` — `mlock_fail_hint` asserts the `ulimit`
  advice unconditionally.
- `platform/test_atomic_file.cpp` — symlink-at-candidate / dangling /
  destination-is-file tests run unconditionally.
- `platform/test_autoplay_pref.cpp` — `ScopedDataHome` uses
  `XDG_DATA_HOME` unconditionally (no `APPDATA` branch).
- `platform/test_harden.cpp` — drops the `_dup` / `_dup2` /
  `_wfreopen` / `_open_osfhandle` dance; the file is now Linux-only and
  verifies `PR_GET_DUMPABLE` after `disable_core_dumps()`.
- `platform/test_paths.cpp` — `<sys/stat.h>` included unconditionally;
  the keyfile-mode `_WIN32` guard is gone.
- `ui/test_export.cpp` — symlink-not-followed test runs unconditionally.
- `vault/test_chunk_store.cpp` — `/dev/full` test runs unconditionally.
- `vault/test_vault.cpp` — `<sys/stat.h>` included unconditionally;
  owner-only `_WIN32` guard is gone.
- `vault/test_vault_compact.cpp` — `allocated_on_disk` uses
  `st_blocks` unconditionally.

### Build scripts (deleted)

- `scripts/build_codecs.bat` — replaced by `build_codecs.sh`
- `scripts/build_ffmpeg_windows.sh` — MSVC FFmpeg build
- `scripts/setup.bat` — Windows bootstrap
- `scripts/package.ps1` — PowerShell packaging (portable `.zip`)

### Build system — `premake5.lua`

- `link_sdl3()` — only the Linux `libSDL3.a` candidate; the
  `SDL3-static.lib` / `Release/SDL3-static.lib` / `C:/SDL3` fallback are
  gone.
- `link_platform_extras()` — only the `dl` + `pthread` + `m` Linux triple.
- `link_image_codecs()` — single Linux link line; no Windows-only
  `lib/libwebp` / `libsharpyuv` prefixed names.
- `link_av()` — drops the `OSV_HWACCEL_D3D11VA` `system:windows` filter +
  define (Phase 43 Part 1 D3D11VA backend is gone — Linux VAAPI only).
- `link_archive()` — single Linux link line; no `zs` (zlib's Windows-only
  static-suffix) variant.
- `cmake_static_lib()` — single Linux candidate path.
- Workspace-level: drop `multiprocessorcompile "On"` + `debugformat "c7"`
  + `runtime "Release"` + `_ITERATOR_DEBUG_LEVEL=0` + the `NOMINMAX` /
  `WIN32_LEAN_AND_MEAN` / `_CRT_SECURE_NO_WARNINGS` defines + the
  `/guard:cf` / `/CETCOMPAT` blocks (MSVC-only).
- Drop the `WindowedApp` + `mainCRTStartup` block in `osv` (the only
  Release builds a console-less app on Windows).

### CI / Release workflows

- `ci.yml` — delete the entire `windows:` job (~290 lines: ccache shim,
  MSYS2, NASM, vs2022, msbuild dance, portable-zip packaging).
- `release.yml` — delete the Windows job + Windows asset upload;
  `publish.needs` drops `windows` to just `[linux]`.
- Codec cache key bumped from `codecs-v4-` to `codecs-v6-` (the hashed
  build-script set shrank: `build_codecs.bat` + `build_ffmpeg_windows.sh`
  are gone, only `build_codecs.sh` is hashed now).

### Documentation

- `AGENTS.md` — "Primary platform | Linux → Windows" → "Linux x86_64
  (Arch). Windows support removed (Phase 101); macOS was dropped
  earlier." Drop the Windows-specific notes from the hardening section
  (`VirtualLock` cap + `hiberfil.sys`, `0xc0000374` swscale overrun
  symptom, `redirect_diagnostics_to_log_file` Windows-specific behavior).
  Trim `crypto/random.cpp` / `secure_mem.h` / `vault/file_util.h` to
  Linux-only claims.
- `ROADMAP.md` — add a Phase 101 row + this follow-up note on Phase 8.
- `README.md` — drop the "Windows — manual build walkthrough" section,
  the `scripts\setup.bat` line, the Windows-specific `nasm` /
  `pkg-config` install rows, and the `scripts\package.ps1` reference.
- `docs/roadmap/phase-08-cross-platform-ports.md` — add the existing-style
  follow-up note ("Windows support described in this phase was later
  removed entirely in Phase 101; the entire `system:windows` build
  pipeline and CI matrix leg were deleted").
- `.serena/memories/tech_stack.md` — drop the D3D11VA paragraph,
  `OSV_HWACCEL_D3D11VA` mention, the Windows `.a` → `.lib` rename
  paragraph, the `-v5` Windows-only codec cache key.
- `.serena/memories/core.md` — "Multi-platform (Linux → Windows; no macOS)"
  → "Linux-only; Windows support removed; macOS never supported".
- `.serena/memories/conventions.md` — drop the "Cross-platform (MSVC vs
  libstdc++)" section (its guidance was MSVC-specific).

## Acceptance criterion

- `scripts/test.sh` green (Debug + FFmpeg).
- `scripts/test.sh --release` green.
- `bin/premake5 --no-av ninja --cc=gcc && ninja Debug_x64` green.
- `bin/premake5 --asan ninja && ninja Debug_x64` green.
- `bin/premake5 --tsan ninja && ninja Debug-tsan` green.
- `scripts/test.sh --asan` green.
- CI: 5 legs (gcc/clang × Debug/Release + ASAN + TSan + no-av + SonarQube).
  Codec cache key is `codecs-v6-`.
- AGENTS.md / `.serena/memories/*` updates committed in the same PR.

## Out of scope (deliberate)

- Don't remove `vendor/ffmpeg`'s `--disable-everything` generality or
  any vendor code — compiles unchanged on Linux.
- Don't remove `scripts/package.sh` (Linux tarball stays).
- Don't rename the deleted `osv-windows-zip` artifact anywhere except
  where it disappears with the deleted CI / release job.
- Don't touch the macOS `#error` guard in `crypto/random.cpp` — it's
  still a useful clear-message guard for anyone who tries to build on
  a platform that was previously listed.
