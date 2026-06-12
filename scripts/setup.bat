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
echo   bin\premake5.exe vs2022
echo   msbuild ObscuraSafeVault.sln /m /p:Configuration=Debug /p:Platform=x64
popd
exit /b 0

:fail
echo setup.bat failed (see error above).
popd
exit /b 1
