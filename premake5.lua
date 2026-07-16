-- Obscura-Safe-Vault — premake5 workspace
-- Run:  ./bin/premake5 ninja    (primary)
--       ./bin/premake5 gmake2   (fallback)
--
-- Pass --asan to add AddressSanitizer + UBSan to all native projects, e.g.:
--       ./bin/premake5 --asan ninja

newoption {
    trigger     = "asan",
    description = "Build with AddressSanitizer + UndefinedBehaviorSanitizer",
}

newoption {
    trigger     = "coverage",
    description = "Build with gcov code-coverage instrumentation (GCC/Clang)",
}

-- ---------------------------------------------------------------------------
-- SDL3 linkage helper — shared by the app and the test runner.
-- Uses the vendored cmake build if scripts/setup.sh has produced it, otherwise
-- falls back to a system SDL3 install.
-- ---------------------------------------------------------------------------
local function link_sdl3()
    -- CMake names the static lib libSDL3.a on Linux but SDL3-static.lib
    -- on Windows; the Visual Studio generator nests it in a per-config dir.
    local sdl3_build = path.join(os.getcwd(), "vendor/SDL3/build")
    local candidates = {
        { lib = "SDL3",        file = "libSDL3.a",               dir = sdl3_build },                        -- Linux (Ninja/Make)
        { lib = "SDL3-static", file = "SDL3-static.lib",         dir = sdl3_build },                        -- Windows (Ninja)
        { lib = "SDL3-static", file = "Release/SDL3-static.lib", dir = path.join(sdl3_build, "Release") },  -- Windows (VS generator)
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
    filter {}
end

-- OS libraries/frameworks needed by a static SDL3 link + our crypto shim.
-- Shared by the app and the test runner.
local function link_platform_extras()
    filter "system:linux"
        links { "dl", "pthread", "m" }
    filter "system:windows"
        -- bcrypt: crypto/random.cpp. The rest: static SDL3.
        links { "bcrypt", "winmm", "imm32", "version", "setupapi",
                "ole32", "oleaut32", "advapi32", "shell32", "user32",
                "gdi32", "uuid" }
    filter {}
end

-- ---------------------------------------------------------------------------
-- Vendored image codecs — WebP (+ HEIC/AVIF stack in Phase 9 Stage B).
-- scripts/setup.sh cmake-builds each static and installs into one staging
-- prefix (vendor/codecs-prefix); we link the whole set from there. Shared by
-- the app and the test runner (osv_tests compiles src/image/*.cpp).
-- When --asan is passed, prefer vendor/codecs-prefix-asan/ (built by
-- scripts/build_codecs.sh --asan) with a fallback + warning to the normal prefix.
-- ---------------------------------------------------------------------------
local function link_image_codecs()
    local prefix = path.join(os.getcwd(), "vendor/codecs-prefix")
    -- Try ASAN prefix first if --asan was passed.
    if _OPTIONS["asan"] then
        local asan_prefix = path.join(os.getcwd(), "vendor/codecs-prefix-asan")
        if os.isdir(path.join(asan_prefix, "include")) then
            prefix = asan_prefix
        else
            -- Fallback with a warning.
            premake.warn("--asan requested but ASAN codec prefix not found at " .. asan_prefix .. "; falling back to " .. prefix .. " (not instrumented)")
        end
    end

    if os.isdir(path.join(prefix, "include")) then
        includedirs { path.join(prefix, "include") }
        libdirs     { path.join(prefix, "lib") }
        -- Static-link order matters: dependents before dependencies
        -- (heif → de265, aom ; webp → sharpyuv). The lib filenames differ by
        -- platform: on Unix `-lNAME` finds `libNAME.a`, but MSVC links the name
        -- verbatim and cmake keeps the `lib` prefix on libde265/libwebp/libsharpyuv
        -- (heif/aom have none), so Windows needs the prefixed names.
        -- We link libheif statically; without this its headers declare the API
        -- as __declspec(dllimport) on MSVC (no-op elsewhere), which breaks the link.
        defines { "OSV_VENDORED_CODECS", "LIBHEIF_STATIC_BUILD" }
        filter "system:windows"
            links { "heif", "libde265", "aom", "libwebp", "libsharpyuv" }
        filter { "system:not windows" }
            links { "heif", "de265", "aom", "webp", "sharpyuv" }
        filter {}
    end
end

-- ---------------------------------------------------------------------------
-- FFmpeg/libav (decode-only static; Phase 15; Windows leg via MSYS2 +
-- --toolchain=msvc, see scripts/build_ffmpeg_windows.sh). Same staging prefix
-- as the image codecs. Linked only when present so a build missing it stays
-- green; OSV_VENDORED_AV gates the dependent code/tests.
-- Static-link order: dependents before dependencies (format → codec → swscale → util).
-- When --asan is passed, prefer vendor/codecs-prefix-asan/ (built by
-- scripts/build_codecs.sh --asan) with a fallback + warning to the normal prefix.
-- ---------------------------------------------------------------------------
local function link_av()
    local prefix = path.join(os.getcwd(), "vendor/codecs-prefix")
    -- Try ASAN prefix first if --asan was passed.
    if _OPTIONS["asan"] then
        local asan_prefix = path.join(os.getcwd(), "vendor/codecs-prefix-asan")
        if os.isfile(path.join(asan_prefix, "lib/libavcodec.a")) then
            prefix = asan_prefix
        else
            -- Fallback with a warning (only if we'd link AV at all).
            if os.isfile(path.join(path.join(os.getcwd(), "vendor/codecs-prefix"), "lib/libavcodec.a")) then
                premake.warn("--asan requested but ASAN FFmpeg prefix not found at " .. asan_prefix .. "; falling back to " .. path.join(os.getcwd(), "vendor/codecs-prefix") .. " (not instrumented)")
            end
        end
    end

    if os.isfile(path.join(prefix, "lib/libavcodec.a")) then
        -- externalincludedirs (not includedirs): libavutil/common.h's inline
        -- clip helpers (av_clip_uint8 etc.) trip MSVC's C4244 (int -> uint8_t/
        -- int16_t narrowing in a return), which our own fatalwarnings{"All"}
        -- promotes to a hard error. Marking this path external (MSVC:
        -- /external:I, defaults to /external:W3 with warnings not promoted to
        -- errors) keeps our own code at full W4/WX while FFmpeg's headers
        -- just compile. GCC/Clang ignore externalincludedirs here (no
        -- externalwarnings set), so behaviour there is unchanged.
        externalincludedirs { path.join(prefix, "include") }
        libdirs     { path.join(prefix, "lib") }
        defines     { "OSV_VENDORED_AV" }
        links       { "avformat", "avcodec", "swscale", "swresample", "avutil" }
        -- libavutil needs libm/pthread/dl on Linux; bz2/lzma may be referenced
        -- by demuxers. link_platform_extras() already covers pthread/dl/m.
        filter "system:linux"
            links { "z" }
        filter {}
    end
end

-- ---------------------------------------------------------------------------
-- libarchive (read-only 7z/RAR/TAR; Phase 34), + its vendored zlib/liblzma
-- filters. Same staging prefix as the image codecs / FFmpeg. Linked only when
-- present so a build missing it stays green; OSV_VENDORED_ARCHIVE gates the
-- dependent code/tests. Static-link order: dependents before dependencies
-- (archive → lzma → z).
-- When --asan is passed, prefer vendor/codecs-prefix-asan/ (built by
-- scripts/build_codecs.sh --asan) with a fallback + warning to the normal prefix.
-- ---------------------------------------------------------------------------
local function link_archive()
    local prefix = path.join(os.getcwd(), "vendor/codecs-prefix")
    if _OPTIONS["asan"] then
        local asan_prefix = path.join(os.getcwd(), "vendor/codecs-prefix-asan")
        if os.isfile(path.join(asan_prefix, "lib/libarchive.a")) then
            prefix = asan_prefix
        else
            if os.isfile(path.join(path.join(os.getcwd(), "vendor/codecs-prefix"), "lib/libarchive.a")) then
                premake.warn("--asan requested but ASAN libarchive prefix not found at " .. asan_prefix .. "; falling back to " .. path.join(os.getcwd(), "vendor/codecs-prefix") .. " (not instrumented)")
            end
        end
    end

    if os.isfile(path.join(prefix, "lib/libarchive.a")) then
        includedirs { path.join(prefix, "include") }
        libdirs     { path.join(prefix, "lib") }
        defines     { "OSV_VENDORED_ARCHIVE" }
        -- zlib's static target is named "zs" on Windows (its CMakeLists appends
        -- a static-only suffix there) but plain "z" everywhere else; liblzma and
        -- libarchive itself keep the same OUTPUT_NAME on every platform.
        filter "system:windows"
            links { "archive", "lzma", "zs" }
        filter "system:not windows"
            links { "archive", "lzma", "z" }
        filter {}
    end
end

workspace "ObscuraSafeVault"
    configurations { "Debug", "Release" }
    platforms      { "x64" }

    -- All projects share these
    language   "C++"
    cppdialect "C++23"
    warnings   "Extra"

    -- Path (relative to the repo root / process cwd) of the bundled UI font.
    defines { 'OSV_DEFAULT_FONT="assets/fonts/NotoSans-Regular.ttf"' }

    objdir  "build/obj/%{cfg.buildcfg}/%{prj.name}"
    targetdir "build/bin/%{cfg.buildcfg}"

    filter "configurations:Debug"
        defines  { "OSV_DEBUG" }
        symbols  "On"
        optimize "Off"
    -- _DEBUG selects the debug CRT + _ITERATOR_DEBUG_LEVEL=2 on MSVC, which can't
    -- link against our Release-built vendored static libs (SDL3/codecs). Keep it
    -- off on Windows; see the system:windows filter below which pins the release
    -- runtime there for all configs.
    filter { "configurations:Debug", "system:not windows" }
        defines  { "_DEBUG" }

    filter "configurations:Release"
        defines  { "NDEBUG" }
        optimize "Speed"
        symbols  "Off"

    -- The "x64" platform name sets architecture x86_64 on Linux/Windows.
    filter { "platforms:x64" }
        architecture "x86_64"

    -- Windows: stop windows.h defining min/max macros (they break std::min/max),
    -- trim its include surface, and silence MSVC's fopen_s nagging (portable
    -- stdio is deliberate; see vault/file_util.h).
    filter "system:windows"
        defines { "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS" }
        -- All vendored static libs (SDL3, libheif/libde265/libaom/libwebp) are
        -- built Release, so pin the whole workspace to the release dynamic CRT and
        -- release iterators in every config; otherwise a Debug build hits LNK2038
        -- (RuntimeLibrary / _ITERATOR_DEBUG_LEVEL mismatch) against them.
        runtime "Release"
        defines { "_ITERATOR_DEBUG_LEVEL=0" }

    -- Opt-in sanitizers (gcc/clang). Applies to every project in the workspace.
    filter { "options:asan", "toolset:gcc or clang" }
        buildoptions { "-fsanitize=address,undefined", "-fno-omit-frame-pointer" }
        linkoptions  { "-fsanitize=address,undefined" }

    -- Opt-in gcov coverage instrumentation. Used by the SonarCloud CI job.
    filter { "options:coverage", "toolset:gcc or clang" }
        buildoptions { "--coverage" }
        linkoptions  { "--coverage" }

    filter {}

-- ---------------------------------------------------------------------------
-- Monocypher — compiled from the single vendored .c file
-- ---------------------------------------------------------------------------
project "monocypher"
    kind        "StaticLib"
    language    "C"   -- plain C, override workspace default
    cdialect    "C17"

    files   { "vendor/monocypher/src/monocypher.c" }
    includedirs { "vendor/monocypher/src" }

    -- Disable warnings we don't control in vendored code
    filter "toolset:gcc or clang"
        buildoptions { "-w" }
    filter {}

-- ---------------------------------------------------------------------------
-- miniz — ZIP reader (Phase 17). Plain C static lib built from the vendored
-- split sources (no CMake). vendor/miniz-shim supplies miniz_export.h — the one
-- header CMake would generate — so the pristine vendor/miniz submodule is never
-- mutated. MINIZ_NO_ZLIB_COMPATIBLE_NAMES is REQUIRED: without it miniz exports
-- compress/uncompress/crc32/inflate which clash at link time with the real libz
-- that avformat pulls in. _POSIX_C_SOURCE exposes fseeko/ftello for miniz_zip.c.
-- Consumers include the umbrella "miniz.h" (miniz_zip.h alone lacks mz_alloc_func
-- and MZ_BEST_SPEED). Linkage of the full ZIP API is proven by test_zip_linkage.
-- ---------------------------------------------------------------------------
project "miniz"
    kind        "StaticLib"
    language    "C"
    cdialect    "C17"

    files       { "vendor/miniz/miniz.c", "vendor/miniz/miniz_tdef.c",
                  "vendor/miniz/miniz_tinfl.c", "vendor/miniz/miniz_zip.c" }
    includedirs { "vendor/miniz", "vendor/miniz-shim" }
    defines     { "MINIZ_NO_ZLIB_COMPATIBLE_NAMES", "_POSIX_C_SOURCE=200809L" }

    filter "toolset:gcc or clang"
        buildoptions { "-w" }   -- silence warnings in vendored code
    filter {}

-- ---------------------------------------------------------------------------
-- osv — the main application
-- ---------------------------------------------------------------------------
project "osv"
    kind        "ConsoleApp"
    targetname  "osv"

    files {
        "src/**.cpp",
        "src/**.h",
    }

    includedirs {
        "src",
        "vendor/monocypher/src",
        "vendor/miniz",
        "vendor/miniz-shim",
        "vendor/stb",
        "vendor/json/single_include",
    }

    -- Keep miniz's zlib-compatible names out of our TUs too (the lib is built
    -- without them); we only use the mz_* ZIP API.
    defines { "MINIZ_NO_ZLIB_COMPATIBLE_NAMES" }

    links { "monocypher", "miniz" }

    -- SDL3: vendored cmake build if present, else system SDL3.
    link_sdl3()
    link_platform_extras()
    link_image_codecs()
    link_av()
    link_archive()

    -- Strict warnings for project code (not vendored)
    fatalwarnings { "All" }
    filter "toolset:gcc or clang"
        buildoptions { "-Wshadow", "-Wconversion" }
    filter {}
    -- clang's -Wconversion implies -Wsign-conversion (gcc's does not); disable for uniform semantics across CI compilers. 134 sites — candidate for a future dedicated cleanup.
    filter "toolset:clang"
        buildoptions { "-Wno-sign-conversion" }
    filter {}

    -- Release hardening (Linux)
    filter { "configurations:Release", "toolset:gcc or clang" }
        buildoptions { "-fstack-protector-strong" }
        defines { "_FORTIFY_SOURCE=3" }
    filter {}

    -- GUI app on Windows (no console window) for Release; keep the console in
    -- Debug so stderr logging is visible.
    filter { "system:windows", "configurations:Release" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"   -- keep standard main(), not WinMain
    filter {}

-- ---------------------------------------------------------------------------
-- osv_tests — unit/integration tests for the crypto layer (Phase 1+)
-- Compiles the test harness + the crypto sources directly (NOT src/app/main.cpp).
-- ---------------------------------------------------------------------------
project "osv_tests"
    kind        "ConsoleApp"
    targetname  "osv_tests"

    -- Absolute path to committed binary fixtures (WebP/HEIC/AVIF) so the test
    -- runner finds them regardless of the working directory it is launched from.
    defines { 'OSV_FIXTURE_DIR="' .. path.join(os.getcwd(), "tests/image/fixtures") .. '"' }
    defines { 'OSV_VAULT_FIXTURE_DIR="' .. path.join(os.getcwd(), "tests/vault/fixtures") .. '"' }
    defines { 'OSV_MEDIA_FIXTURE_DIR="' .. path.join(os.getcwd(), "tests/media/fixtures") .. '"' }
    defines { 'OSV_UI_FIXTURE_DIR="' .. path.join(os.getcwd(), "tests/ui/fixtures") .. '"' }

    files {
        "tests/**.cpp",
        "tests/**.h",
        "src/crypto/*.cpp",
        "src/crypto/*.h",
        "src/vault/*.cpp",
        "src/vault/*.h",
        "src/image/*.cpp",
        "src/image/*.h",
        "src/media/*.cpp",
        "src/media/*.h",
        -- gfx: only the headless-testable units (NOT window.cpp, which needs a
        -- real display). texture_cache + text + renderer run against an SDL
        -- software renderer in tests/gfx/.
        "src/gfx/texture_cache.cpp",
        "src/gfx/text.cpp",
        "src/gfx/renderer.cpp",
        "src/gfx/yuv_texture.cpp",
        "src/gfx/theme.cpp",
        "src/platform/error_log.cpp",
        "src/platform/harden.cpp",
        "src/platform/paths.cpp",
        "src/platform/vault_registry.cpp",
        "src/platform/theme_pref.cpp",
        "src/platform/volume_pref.cpp",
        "src/platform/file_dialog.cpp",
        "src/ui/input.cpp",
        "src/ui/nav_model.cpp",
        "src/ui/passphrase.cpp",
        "src/ui/secure_text_field.cpp",
        "src/ui/unlock_logic.cpp",
        "src/ui/widgets.cpp",
        "src/ui/strip_layout.cpp",
        "src/ui/cover_layout.cpp",
        "src/ui/gallery_cover.cpp",
        "src/ui/scroll_model.cpp",
        "src/ui/grid_layout.cpp",
        "src/ui/meta_format.cpp",
        "src/ui/selection_model.cpp",
        "src/ui/search_model.cpp",
        "src/ui/advanced_search_model.cpp",
        "src/ui/result_grid.cpp",
        "src/ui/export.cpp",
        "src/ui/delete_summary.cpp",
        "src/ui/natural_sort.cpp",
        "src/ui/meta_json.cpp",
        "src/ui/zip_plan.cpp",
        "src/ui/import_common.cpp",
        "src/ui/zip_encoding.cpp",
        "src/ui/zip_import.cpp",
        "src/ui/zip_import_job.cpp",
        "src/ui/file_op_job.cpp",
        "src/ui/progress_modal.cpp",
        "src/ui/tag_list_parse.cpp",
        "src/ui/tag_inherit.cpp",
        "src/ui/tag_suggest.cpp",
        "src/ui/tag_overview_model.cpp",
        "src/ui/slideshow_model.cpp",
        "src/ui/playback_model.cpp",
        "src/ui/video_playback.cpp",
        "src/ui/archive_reader.cpp",
        "src/ui/archive_import.cpp",
        "src/ui/gallery_sort.cpp",
        "src/ui/help_popup.cpp",
        "src/ui/tile_thumb.cpp",
    }

    includedirs {
        "src",                      -- so tests can #include "crypto/aead.h"
        "tests",
        "vendor/monocypher/src",
        "vendor/miniz",
        "vendor/miniz-shim",
        "vendor/stb",
        "vendor/json/single_include",
    }

    defines { "MINIZ_NO_ZLIB_COMPATIBLE_NAMES" }

    links { "monocypher", "miniz" }

    -- gfx units pull in SDL3 (software renderer is created headlessly in tests).
    link_sdl3()
    link_platform_extras()
    link_image_codecs()
    link_av()
    link_archive()

    -- Strict warnings for project code (not vendored)
    fatalwarnings { "All" }
    filter "toolset:gcc or clang"
        buildoptions { "-Wshadow", "-Wconversion" }
    filter {}
    -- clang's -Wconversion implies -Wsign-conversion (gcc's does not); disable for uniform semantics across CI compilers. 134 sites — candidate for a future dedicated cleanup.
    filter "toolset:clang"
        buildoptions { "-Wno-sign-conversion" }
    filter {}

    -- Release hardening (Linux)
    filter { "configurations:Release", "toolset:gcc or clang" }
        buildoptions { "-fstack-protector-strong" }
        defines { "_FORTIFY_SOURCE=3" }
    filter {}
