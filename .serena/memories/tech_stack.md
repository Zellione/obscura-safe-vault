# Tech Stack

## Language / standard
- C++23 — use `std::span`, designated initialisers, `[[nodiscard]]`, `constexpr` freely.

## Build system
- **premake5 beta8** → **Ninja** (gmake2 fallback). Binary at `bin/premake5` (downloaded by `scripts/setup.sh`, not committed).
- Lua config: `premake5.lua` at project root.
- Generated files: `osv.ninja`, `osv_tests.ninja`, `monocypher.ninja`, `build.ninja`, `compile_commands.json` (gitignored).
- **Sanitizer build isolation (PR #122):** `--asan`/`--tsan` generations suffix targetdir/objdir
  (`build/bin/<Config>-asan` / `-tsan`), so sanitizer builds never overwrite plain binaries, and
  `scripts/test.sh` restores plain build files on EXIT after a sanitizer run. Root cause fixed: an
  `--asan` gate used to rebuild `build/bin/Debug/osv` instrumented AND leave `-fsanitize` in
  build.ninja for every later `build.sh` — the launched app then ran 3-10x slower. CI's
  `tests-asan`/`tests-tsan`/`tests-asan-codecs` legs run the suffixed binary paths.

## Runtime deps (vendored git submodules under `vendor/`)
| Library | Version | How compiled |
|---|---|---|
| SDL3 | 3.4.10 | cmake once via `setup.sh`; static lib at `vendor/SDL3/build/libSDL3.a` (Linux) or `vendor/SDL3/build/SDL3-static.lib` / `Release/SDL3-static.lib` (Windows) |
| Monocypher | 4.0.2 | single `monocypher.c` compiled by premake |
| stb | head | header-only (`stb_image.h`, `stb_truetype.h`) |
| libwebp | 1.4.0 | WebP decode; cmake → `vendor/codecs-prefix`. Phase 57 also links **libwebpdemux** (`WebPAnimDecoder`) for animated WebP — the same cmake build already produced `libwebpdemux.a`, so only the premake `links` line changed. It is NOT gated: unlike GIF (FFmpeg), animated WebP plays in every build |
| libde265 | 1.0.15 | HEIC (HEVC) decode; cmake → `vendor/codecs-prefix` |
| libaom | 3.14.1 | AVIF (AV1) *stills* decode via libheif, decoder-only; needs **nasm**; cmake → `vendor/codecs-prefix`. Phase 40: also linked a second time into FFmpeg as the `libaom-av1` decoder for AV1 *video* (see FFmpeg row) — one vendored copy, two independent consumers |
| libheif | 1.18.2 | HEIC/AVIF container; one `decode_heif_from_memory` covers both |
| FFmpeg/libav | 7.1.1 | Video & audio decode-only (H.264/H.265 + ProRes/DNxHD-DNxHR/MJPEG for `.mov` pro codecs, Phase 28; VP8/VP9 for `.webm`, Phase 38; AV1 for `.webm`/`.mov` + QTRLE/Cinepak for `.mov`, Phase 40; aac/opus/mp3/vorbis/flac/ac3 audio; mov/mp4/m4v + matroska/webm demux; libswscale for video, swresample linked as transitive dependency of audio decoders — we do NOT use swresample for audio conversion, SDL_AudioStream handles that); configure-built static → `vendor/codecs-prefix`; needs **nasm**; linked by `link_av()` (avformat/avcodec/swscale/swresample/avutil, **then `aom` a second time** — see gotcha below) under `OSV_VENDORED_AV` (Phase 15–16). **AV1 gotcha (Phase 40):** FFmpeg's own native `av1` decoder is a hwaccel-dispatch-only shim (`AVERROR(ENOSYS)` without a HW accelerator — confirmed by direct testing, not documented in FFmpeg's own `--enable-decoder` help text); real software AV1 decode requires `--enable-libaom --enable-decoder=...,libaom_av1,...` (configure component name is `libaom_av1`, **underscore** — derived from the `ff_libaom_av1_decoder` extern symbol — while the runtime/display decoder name is `libaom-av1`, **hyphen**; passing the hyphenated form to `--enable-decoder` silently no-ops with a `did not match anything` warning easy to miss in a long build log). `PKG_CONFIG_PATH=$CODEC_PREFIX/lib/pkgconfig` points configure at the `aom.pc` `build_codec aom` already installed (Windows needs the `pkgconf` MSYS2 package, see ci.yml). Because the image-codec chain (`heif → de265 → aom → webp → sharpyuv`) links `aom` *before* `avformat`/`avcodec` in `premake5.lua`, and GNU ld's static-archive resolution is single-pass (a library is never re-scanned once ld has moved past it), avcodec's new `aom_codec_*` references go unresolved unless `aom` is **also** listed again after the `link_av()` block — `link_codecs()`'s occurrence still satisfies libheif's needs, the second one satisfies avcodec's. Also: **`ninja` does not detect a rebuilt `vendor/codecs-prefix/lib/libavcodec.a` as a relink trigger** (it's a prebuilt external file, not a ninja-generated build edge) — after rebuilding the vendored codecs with a changed decoder list, `rm -rf build/` before `scripts/gen.sh && scripts/test.sh`, or the test binary silently keeps running against the stale `.a`. |
| nlohmann/json | v3.12.0 | Archive `meta.json` parsing (Phase 27). Header-only single-header MIT lib; include path `vendor/json/single_include` (no build step, no premake project). Used exception-free: `json::parse(..., allow_exceptions=false)` → discarded value on malformed input. Only consumer: `src/ui/meta_json.cpp` |
| miniz | master commit `e78dfd2` | ZIP reader (Phase 17). Plain-C static lib compiled by premake from the modern split sources (`miniz.c`/`miniz_tdef.c`/`miniz_tinfl.c`/`miniz_zip.c`); the only release tags v112–v114 are ancient SVN snapshots, so pinned to a master commit. Built + consumed with `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` (else its `compress`/`crc32`/`inflate` clash with the libz avformat links). `vendor/miniz-shim/miniz_export.h` supplies the one CMake-generated header so the submodule stays pristine; consumers include the umbrella `"miniz.h"` (not `miniz_zip.h`, which lacks `mz_alloc_func`/`MZ_BEST_SPEED`) |
| zlib | 1.3.2 | gzip filter dep for libarchive (Phase 34). cmake → `vendor/codecs-prefix`; `-DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_TESTING=OFF`. Windows static-lib output name is `zs.lib` (`zlib_static_suffix="s"` under `if(WIN32)` in its own CMakeLists.txt) — `link_archive()` in premake5.lua branches on `system:windows` for this |
| xz / liblzma | 5.8.3 | LZMA2 filter dep for libarchive, covers `.7z`/`.txz` (Phase 34). cmake → `vendor/codecs-prefix`; `-DXZ_SANDBOX=no` is required for `--asan` builds — xz's own Landlock-sandboxing configure check hard-errors on seeing `-fsanitize=` in CFLAGS otherwise. Static lib output name is `lzma` on every platform (no suffix quirk, unlike zlib) |
| libarchive | 3.8.8 (BSD-2-Clause) | 7z/RAR/TAR read-only import (Phase 34). cmake → `vendor/codecs-prefix`, out-of-tree build dir `vendor/.libarchive-build` (NOT `vendor/libarchive/build`, which the submodule's own source tree already tracks — cmake helper modules that an out-of-tree build there would clobber). Finds zlib/liblzma via `CMAKE_PREFIX_PATH` (same pattern libheif uses for libde265/libaom); every optional codec/crypto backend disabled except zlib+lzma (no bzip2/lz4/lzo/zstd — bzip2 has no CMake build upstream, so `.tbz2` is out of scope; no OpenSSL/mbedTLS/Nettle/CNG; no libxml2/expat; no ACL/xattr/iconv; no bsdtar/bsdcpio/bsdcat/test binaries). Linked by `link_archive()` under `OSV_VENDORED_ARCHIVE`; static lib output name is `archive` on every platform |

Image codecs are built by `scripts/build_codecs.{sh,bat}` (shared by `setup.{sh,bat}` and CI)
and installed into `vendor/codecs-prefix/`; premake's `link_image_codecs()` links them in
order `heif → de265 → aom → webpdemux → webp → sharpyuv` (demux before webp: static-archive
resolution is single-pass and libwebpdemux depends on libwebp). The build passes
`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (libde265's pre-3.5 cmake_minimum under CMake 4.x).

**FFmpeg (Phase 15–16)** is a sibling vendored submodule (`vendor/ffmpeg`) built via **configure**
(not cmake) into the same `vendor/codecs-prefix` by `build_codecs.{sh,bat}`: decode-only
(`--disable-everything` then opt-in h264/hevc/prores/dnxhd/mjpeg/vp8/vp9/libaom_av1/qtrle/cinepak
decoders (pro `.mov` codecs Phase 28; `.webm` VP8/VP9 Phase 38; AV1 + legacy `.mov` codecs Phase 40);
Phase 52 additions: mpeg1video/mpeg2video/mpeg4/msmpeg4v1/msmpeg4v2/msmpeg4v3/wmv1/wmv2/wmv3/vc1/h263/flv1/vp6/vp6a/vp6f/svq1/svq3/dvvideo/msvideo1/rpza/huffyuv/ffv1/theora/rv10/rv20/rv30/rv40 video decoders,
aac/opus/mp3/vorbis/flac/ac3/mp2/wmav1/wmav2/cook/ra_144/ra_288/eac3/pcm_s16le/pcm_u8/adpcm_ms/adpcm_ima_wav audio decoders,
mov/mp4/matroska/avi/mpegps/mpegts/asf/flv/ogg/rm demuxers,
parsers mpegvideo/mpeg4video/h263/vc1/mpegaudio,
`--enable-libaom --enable-avfilter --enable-filter=yadif` (yadif deinterlacing),
swscale + swresample; no encoders/muxers/protocols/network/programs).
See the FFmpeg row above for the `libaom_av1` naming/link-order gotchas.
**CRITICAL GOTCHA (Phase 52):** After changing the decoder list in `build_codecs.sh`, **`rm -rf build/` before `scripts/gen.sh`**.
Ninja does not treat a rebuilt `libavcodec.a` as a build edge (it's a prebuilt external file), so the test binary
silently keeps running against the stale archive. This cost a debugging session. Recorded in this memory.

**Windows `.a` → `.lib` rename (`scripts/build_ffmpeg_windows.sh`):** premake emits bare
`links{"<name>"}` on every platform, which MSVC resolves to `<name>.lib`, but FFmpeg always installs
`lib<name>.a`. The script copies each one after `make install` — **every FFmpeg library premake links
must appear in that loop's name list** (`avfilter avformat avcodec swscale swresample avutil`).
avfilter was missed when Phase 52 enabled yadif: `libavfilter.a` existed, so premake's
`os.isfile(lib/libavfilter.a)` guard fired and emitted the link, while `avfilter.lib` never did —
both MSVC legs died at link with LNK1181/LNK1104 while every Linux leg stayed green.

**Phase 47 addition:** the `gif` **decoder**, **demuxer**, AND **parser** are enabled.
The parser is essential: FFmpeg n7.1.1's gif demuxer emits raw 1024-byte chunks and sets
`need_parsing = AVSTREAM_PARSE_FULL_RAW` (libavformat/gifdec.c:224), delegating frame reassembly
to libavcodec/gif_parser.c. Without the parser, `av_read_frame` returns unparsed chunks and
decoding fails on the second chunk with `AVERROR_INVALIDDATA`. **Document this in the build scripts
— a future FFmpeg bump must not silently regress it.**

**Phase 43 Part 1:** `--enable-d3d11va` added to the Windows FFmpeg configure
invocation (`scripts/build_ffmpeg_windows.sh`) — a hwaccel dispatch-registration
flag, not a new dependency (FFmpeg's `hwcontext_d3d11va.c` loads `d3d11.dll`/
`dxgi.dll` via `LoadLibrary`/`GetProcAddress` at runtime). `premake5.lua`
defines `OSV_HWACCEL_D3D11VA` only on Windows, gated on `OSV_VENDORED_AV`
already being present. `media::HwAccelContext` (`src/media/hw_accel.{h,cpp}`)
attempts real hw device creation once per process (cached outcome) and
`VideoDecodeWorker` (`src/media/video_decode_worker.{h,cpp}`) attaches it via
`try_attach_hwaccel()` when opening its codec context; any hard decode
failure with a hw context active drops it and reopens a fresh software-only
context for the rest of that clip (`reopen_software_only()`).
`media::test_only_force_hwaccel_unavailable(bool)` makes this fallback path
deterministic in tests, since no CI runner has a real GPU decode block.

**PHASE 43 GAP (FIXED IN PHASE 52):** Phase 43 declared `--enable-d3d11va` / `--enable-vaapi`
but never passed `--enable-hwaccel=`, so `CONFIG_HWACCELS` remained 0 and NO hwaccel symbols
were compiled. Hardware decode was a silent no-op from Phase 43 through Phase 51. Phase 52 adds
the complete `--enable-hwaccel=` lists: VAAPI (Linux): h264,hevc,vp8,vp9,av1,mpeg2,mpeg4,vc1,wmv3,h263;
D3D11VA (Windows): h264,hevc,vp9,av1,mpeg2,vc1,wmv3. Now hwaccels actually compile and function.
Codec coverage confirmed against `vendor/ffmpeg/configure`'s `*_d3d11va_hwaccel_deps`/`*_vaapi_hwaccel_deps`
entries. Linux VAAPI (`OSV_HWACCEL_VAAPI`) requires the dlopen shim (not a direct system `libva`
link) to keep hw decode optional at the binary level; see `docs/superpowers/specs/2026-07-17-hardware-video-decode-design.md`.

**Phase 43 Part 2:** `vendor/libva` (headers-only submodule, pinned to tag 2.22.0/commit 217da1c28336d6a7e9c0c4cb8f1c303968a675f1) supplies the `va.h`/`va_drm.h` headers needed for FFmpeg's `hwcontext_vaapi.c` configure-time link probe. `vendor/vaapi-shim` (static library, osv_vaapi_shim.a) provides the ~36 `va*` symbols FFmpeg's hwcontext_vaapi.c/vaapi_decode.c/vaapi_h264.c/vaapi_hevc.c/vaapi_vp8.c/vaapi_vp9.c/vaapi_mjpeg.c reference, implemented as dlopen("libva.so.2"/"libva-drm.so.2") + dlsym() forwarding — this keeps the real libva.so.2 dependency 100% optional at runtime (no DT_NEEDED entry, silently unavailable if absent, matching the "linked only when present" pattern link_av()/link_archive() use). `--enable-vaapi` added to FFmpeg's configure in `scripts/build_codecs.sh`; `premake5.lua` defines `OSV_HWACCEL_VAAPI` only on Linux, gated on `OSV_VENDORED_AV` already being present. `media::HwAccelContext` (same file as Part 1) gains the VAAPI backend (AV_HWDEVICE_TYPE_VAAPI, DRM render-node path — no X11 dependency). `vaGetDisplay` (X11 variant) and `vaGetDisplayWin32` are excluded from the shim; only `vaGetDisplayDRM` is forwarded.

`link_av()` links avformat/avcodec/swscale/swresample/avutil and defines `OSV_VENDORED_AV` **only
when `lib/libavcodec.a` is present**, so non-FFmpeg builds stay green. Index format is now
`INDEX_VERSION = 4` (adds `Type::Video` + `VideoMeta`; v1–v3 read back-compat). Audio samples decoded
by `AudioDecoder` (planar→interleaved F32) flow into `SDL_AudioStream` (SDL does rate/format/channel
conversion using its own resampler — swresample is a transitive dependency but not used by our code).

**libarchive + zlib + xz (Phase 34)** extend archive import beyond ZIP/CBZ (miniz, unchanged) to
`.7z`/`.rar`/`.tar`(`.gz`/`.xz`)/`.cbr`/`.cb7`/`.cbt`, cmake-built into the same `vendor/codecs-prefix`
by `build_codecs.{sh,bat}`. `link_archive()` defines `OSV_VENDORED_ARCHIVE` only when
`lib/libarchive.a` is present (same presence-gating pattern as `link_av()`), so a build without it
still links — `src/ui/archive_import.*` is declared unconditionally and returns a graceful "not
supported" outcome, and `.zip`/`.cbz` keep working via miniz regardless. `ArchiveReader`
(`src/ui/archive_reader.*`) wraps libarchive's streaming read API; because libarchive has no
random-access API, `extract(index, out)` re-opens and re-scans the stream from the start on every
call rather than caching all decompressed entries in memory. `scripts/build_codecs.sh`'s
idempotency check (`build_codec()`'s "already installed" skip) was fixed during Phase 34 to check
for the actual `lib${name}.a` file rather than a loose `find -name "*${name}*"` glob — the glob also
matched leftover pkgconfig/cmake-config files from a prior partial install, silently skipping a real
rebuild and leaving libarchive linked against the *host system's* zlib/liblzma instead of the
vendored static libs.

## Crypto
- AEAD: XChaCha20-Poly1305 (192-bit nonce, random per chunk)
- KDF: Argon2id (via Monocypher)
- RNG shim: `src/crypto/random.*` — `getrandom` (Linux), `BCryptGenRandom` (Windows)

## Platforms
- Primary: Linux x86_64 (Arch). Also: Windows x86_64. macOS is not supported
  (dropped from CI/build/source — see `#error` guard in `src/crypto/random.cpp`).
- Windows Release builds as `WindowedApp` (no console); Debug keeps console.

## Asset loading
App tries `assets/…` relative to cwd first, then `SDL_GetBasePath()` (packaged installs).

## CI
`.github/workflows/` — ci.yml matrix covers Linux and Windows (macOS support dropped).
release.yml runs on tag pushes (`v*`): rebuilds the two Release packages with OSV_VERSION
from the tag (mirrors ci.yml's Release legs + cache keys — keep in sync), runs tests, then
attaches packages + SHA256SUMS.txt to the tag's GitHub release (draft-created if absent).
The ASAN job (Linux-only) builds vendored SDL3 (since Phase 4) and the image codecs (since
Phase 9): running the C decoders under ASAN/UBSan on untrusted input is high value. nasm is
installed on every leg for libaom.

The ThreadSanitizer job (`tests-tsan`, Phase 42) also runs on every PR:
`--tsan` premake option, gcc-14, Debug-only, Linux-only. Unlike the ASAN
job, it does NOT rebuild vendored SDL3/codecs under TSan — it reuses the
same plain `vendor/codecs-prefix`/`vendor/SDL3/build` every other job
builds, since TSan's target here (races in `media::VideoDecodeWorker`,
`image::DecodeWorker`, and any future concurrent code) lives entirely in
our own `src/`/`tests/`, not inside the vendored C libraries' internals.
`--asan` and `--tsan` are mutually exclusive (premake hard-errors if both
are passed) — they cannot instrument the same binary. Phase 42 also
extracted the apt-install and premake5/SDL3-build steps shared by
`build-and-test`/`tests-asan`/`tests-asan-codecs`/`tests-tsan` into two
local composite actions, `.github/actions/setup-apt-deps/` and
`.github/actions/setup-premake-sdl3/` (`sonarqube` was deliberately left
out — its `if: env.SONAR_TOKEN != ''` guard on every step is a pre-existing
special case).

The no-FFmpeg job (`tests-no-av`, Phase 57 follow-up) also runs on every PR:
`--no-av` premake option, gcc-14, Debug-only, Linux-only. It makes `link_av()`
return early so `OSV_VENDORED_AV` stays undefined, WITHOUT touching the shared
`vendor/codecs-prefix` cache — the image codecs are still linked (libwebp is a
hard dependency), only FFmpeg is dropped. It builds the full `Debug_x64` target,
app included, because `osv` compiles ~25 `src/ui/*.cpp` files the `osv_tests`
target does not list. Rationale: before it, no CI job compiled the `#else` half
of any `#ifdef OSV_VENDORED_AV`, which let `ui/gif_playback.cpp` stay
un-compilable in that configuration from Phase 47 to Phase 57 (it opened
`namespace ui` INSIDE the #ifdef); adding the job immediately exposed two more
test files broken the same way. It is also CI's only proof that animated WebP
plays without FFmpeg.

**Codec cache auto-bust (Phase 52):** The CI cache key for the vendored codec build
is generated from the hash of `.gitmodules` + `scripts/build_codecs.sh` + `scripts/build_ffmpeg_windows.sh`.
When either build script changes (e.g., new decoder list, new demuxer, new hwaccel registration),
the cache automatically misses and a full FFmpeg rebuild is triggered on both CI legs — no
manual version bump needed.

## Qt UI experiment (`src/qtui/`) — build, packaging, release

Standalone CMake project (`src/qtui/CMakeLists.txt`, Qt6 6.7+ Core/Gui/GuiPrivate/Qml/Quick/
ShaderTools/Test/Widgets), NOT part of the premake `osv` target — `premake5.lua` explicitly
`removefiles`s `src/qtui/**` from `osv` since it needs Qt and is built separately. Links the same
`src/crypto`/`src/vault`/`src/media`/`src/image` core plus a large `src/ui/*` subset against Qt6
Quick instead of SDL3+the software renderer; still statically links vendored SDL3 and
`vendor/codecs-prefix` (same build as the SDL app). Local dev build: `scripts/build_qt_experiment.sh`
→ `build/qt-experiment/osv-qt` (Debug, standalone CMake, Linux-only — no Windows dev recipe exists
locally). No `ctest`/`enable_testing()` is configured; the ~40+ `osv_qt_*_test` binaries are run
directly (`QT_QPA_PLATFORM=offscreen ./osv_qt_*_test`), which is also how CI runs them (see below) —
per-commit "Gate status" messages on the qtui-parity branches predate any CI enforcement of this.

**QML is resolved at runtime, not baked in at compile time** (fixed on `experiment/qtui-release-packaging`,
merges into `experiment/qt-quick-ui`): `qml_dir.cpp`/`qml_dir.h`'s `resolveQmlDir()` returns
`OSV_QT_QML_DIR` env var if set, else `QCoreApplication::applicationDirPath() + "/qml"`. `main.cpp`
and `selftest.cpp` (both compiled only into the `osv-qt` target) use this instead of the
`QTUI_QML_DIR` compile-define the ~40+ test executables still use directly (those stay in-tree,
no packaging concern). A CMake `POST_BUILD` step on the `osv-qt` target copies `src/qtui/qml/` next
to the built binary, so dev builds and packaged releases share the same on-disk layout. Before this
fix, a release binary only ever found its QML on the exact machine/path it was built on — the app
was not packageable at all.

**Release packaging** (`.github/workflows/release-qtui.yml`, tag-triggered on `qtui-v*` — deliberately
NOT `v*`, since that's `release.yml`'s trigger and would double-build/mislabel the plain SDL app on
the same tag): installs Qt6 via `jurplel/install-qt-action`, builds `osv-qt` Release via CMake+Ninja,
runs the full `osv_qt_*_test` suite, then deploys platform-native: Linux via `linuxdeploy` +
`linuxdeploy-plugin-qt` into an AppDir (tarball, entry point `AppRun`) — requires `NO_STRIP=1` env var
or the continuous-build `linuxdeploy`'s bundled `strip` aborts the whole run on RELR (`.relr.dyn`)
relocations some distros' system libs now use; the placeholder icon file's basename must exactly
match the `.desktop` file's `Icon=` key and be >=8x8, or linuxdeploy fails late with a confusing
"could not find suitable icon" error. Windows via `windeployqt --qmldir src/qtui/qml` (portable zip).
Reuses `release.yml`'s exact SDL3/codec cache keys on both platforms so the two workflows share warm
caches. Publishes a draft **pre-release** (owner publishes manually, same convention as `release.yml`).

**`install-qt-action` `modules:` gotcha (first beta tag, `qtui-v1.3.3-beta1`):** `qtdeclarative`
is NOT an installable add-on module for Qt 6.7.3 — QML/Quick ship as part of the essential base
install, so requesting it via `--modules` makes `aqt` fail with `The packages ['qtdeclarative']
were not found while parsing XML of package information!`, on both Linux and Windows legs. Confirmed
via `aqt list-qt linux desktop --long-modules 6.7.3 linux_gcc_64` — only `qtshadertools` (plus
unrelated addons like `qtquick3d`) is listed; `qtdeclarative` isn't. Fix: `modules: 'qtshadertools'`
only, on both `install-qt-action` steps.
