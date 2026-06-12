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
    -- CMake names the static lib libSDL3.a on Linux/macOS but SDL3-static.lib
    -- on Windows; the Visual Studio generator nests it in a per-config dir.
    local sdl3_build = path.join(os.getcwd(), "vendor/SDL3/build")
    local candidates = {
        { lib = "SDL3",        file = "libSDL3.a",               dir = sdl3_build },                        -- Linux/macOS (Ninja/Make)
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
    filter "system:macosx"
        includedirs { "/usr/local/include/SDL3" }
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

-- ---------------------------------------------------------------------------
-- Vendored image codecs — WebP (+ HEIC/AVIF stack in Phase 9 Stage B).
-- scripts/setup.sh cmake-builds each static and installs into one staging
-- prefix (vendor/codecs-prefix); we link the whole set from there. Shared by
-- the app and the test runner (osv_tests compiles src/image/*.cpp).
-- ---------------------------------------------------------------------------
local function link_image_codecs()
    local prefix = path.join(os.getcwd(), "vendor/codecs-prefix")
    if os.isdir(path.join(prefix, "include")) then
        includedirs { path.join(prefix, "include") }
        libdirs     { path.join(prefix, "lib") }
        -- Static-link order matters: dependents before dependencies
        -- (heif → de265, aom ; webp → sharpyuv). The lib filenames differ by
        -- platform: on Unix `-lNAME` finds `libNAME.a`, but MSVC links the name
        -- verbatim and cmake keeps the `lib` prefix on libde265/libwebp/libsharpyuv
        -- (heif/aom have none), so Windows needs the prefixed names.
        defines { "OSV_VENDORED_CODECS" }
        filter "system:windows"
            links { "heif", "libde265", "aom", "libwebp", "libsharpyuv" }
        filter { "system:not windows" }
            links { "heif", "de265", "aom", "webp", "sharpyuv" }
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
        defines  { "OSV_DEBUG", "_DEBUG" }
        symbols  "On"
        optimize "Off"

    filter "configurations:Release"
        defines  { "NDEBUG" }
        optimize "Speed"
        symbols  "Off"

    -- The "x64" platform name implicitly sets architecture x86_64, so macOS
    -- must override it to the host arch: arm64 machines need to build natively
    -- to link the natively-cmake-built vendored SDL3.
    filter { "platforms:x64", "system:linux or windows" }
        architecture "x86_64"
    filter { "platforms:x64", "system:macosx" }
        architecture (string.find(os.outputof("uname -m") or "", "arm64") and "ARM64" or "x86_64")

    -- Windows: stop windows.h defining min/max macros (they break std::min/max),
    -- trim its include surface, and silence MSVC's fopen_s nagging (portable
    -- stdio is deliberate; see vault/file_util.h).
    filter "system:windows"
        defines { "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS" }

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
        "vendor/stb",
    }

    links { "monocypher" }

    -- SDL3: vendored cmake build if present, else system SDL3.
    link_sdl3()
    link_platform_extras()
    link_image_codecs()

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

    files {
        "tests/**.cpp",
        "tests/**.h",
        "src/crypto/*.cpp",
        "src/crypto/*.h",
        "src/vault/*.cpp",
        "src/vault/*.h",
        "src/image/*.cpp",
        "src/image/*.h",
        -- gfx: only the headless-testable units (NOT window.cpp, which needs a
        -- real display). texture_cache + text + renderer run against an SDL
        -- software renderer in tests/gfx/.
        "src/gfx/texture_cache.cpp",
        "src/gfx/text.cpp",
        "src/gfx/renderer.cpp",
        "src/platform/paths.cpp",
        "src/ui/input.cpp",
        "src/ui/nav_model.cpp",
        "src/ui/passphrase.cpp",
        "src/ui/secure_text_field.cpp",
        "src/ui/unlock_logic.cpp",
        "src/ui/widgets.cpp",
    }

    includedirs {
        "src",                      -- so tests can #include "crypto/aead.h"
        "tests",
        "vendor/monocypher/src",
        "vendor/stb",
    }

    links { "monocypher" }

    -- gfx units pull in SDL3 (software renderer is created headlessly in tests).
    link_sdl3()
    link_platform_extras()
    link_image_codecs()
