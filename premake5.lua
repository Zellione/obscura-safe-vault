-- Obscura-Safe-Vault — premake5 workspace
-- Run:  ./bin/premake5 ninja    (primary)
--       ./bin/premake5 gmake2   (fallback)

workspace "ObscuraSafeVault"
    configurations { "Debug", "Release" }
    platforms      { "x64" }

    -- All projects share these
    language   "C++"
    cppdialect "C++20"
    warnings   "Extra"

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

    -- -----------------------------------------------------------------------
    -- SDL3: use vendored cmake build if setup.sh has been run,
    --       otherwise fall back to system SDL3 (pkg-config / header path).
    -- -----------------------------------------------------------------------
    local vendored_sdl3_lib = path.join(os.getcwd(), "vendor/SDL3/build/libSDL3.a")

    if os.isfile(vendored_sdl3_lib) then
        -- Hermetic vendored build (run scripts/setup.sh first)
        includedirs { "vendor/SDL3/include" }
        libdirs     { "vendor/SDL3/build" }
        links       { "SDL3" }
        defines     { "OSV_VENDORED_SDL3" }
    else
        -- System SDL3 (development shortcut; requires sdl3 system package)
        -- On Arch: sudo pacman -S sdl3
        filter "system:linux"
            includedirs { "/usr/include/SDL3" }
            links       { "SDL3" }
        filter "system:windows"
            -- Point at system or manually placed SDL3 (update path as needed)
            includedirs { "C:/SDL3/include" }
            libdirs     { "C:/SDL3/lib/x64" }
            links       { "SDL3" }
        filter "system:macosx"
            includedirs { "/usr/local/include/SDL3" }
            links       { "SDL3" }
        filter {}
    end

    -- Platform link extras
    filter "system:linux"
        links { "dl", "pthread", "m" }
    filter "system:windows"
        links { "winmm", "imm32", "version", "setupapi" }
    filter {}
