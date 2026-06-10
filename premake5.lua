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
    local vendored_sdl3_lib = path.join(os.getcwd(), "vendor/SDL3/build/libSDL3.a")
    if os.isfile(vendored_sdl3_lib) then
        includedirs { "vendor/SDL3/include" }
        libdirs     { "vendor/SDL3/build" }
        links       { "SDL3" }
        defines     { "OSV_VENDORED_SDL3" }
    else
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

    filter "platforms:x64"
        architecture "x86_64"

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

    -- Platform link extras
    filter "system:linux"
        links { "dl", "pthread", "m" }
    filter "system:windows"
        links { "winmm", "imm32", "version", "setupapi" }
    filter {}

-- ---------------------------------------------------------------------------
-- osv_tests — unit/integration tests for the crypto layer (Phase 1+)
-- Compiles the test harness + the crypto sources directly (NOT src/app/main.cpp).
-- ---------------------------------------------------------------------------
project "osv_tests"
    kind        "ConsoleApp"
    targetname  "osv_tests"

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

    filter "system:linux"
        links { "dl", "pthread", "m" }
    filter "system:windows"
        links { "bcrypt", "winmm", "imm32", "version", "setupapi" }
    filter {}
