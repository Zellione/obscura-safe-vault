@echo off
rem build_codecs.bat — build vendored image codecs (WebP; HEIC/AVIF in Stage B)
rem as static libs into vendor\codecs-prefix. Called by setup.bat and by CI.
rem Requires cmake, Ninja, and (for libaom, Stage B) nasm on PATH.
setlocal enabledelayedexpansion
pushd "%~dp0.."
set "CODEC_PREFIX=%CD%\vendor\codecs-prefix"

call :build_codec webp vendor\libwebp ^
    -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF ^
    -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF ^
    -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF ^
    -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF ^
    -DWEBP_BUILD_EXTRAS=OFF || goto :fail

rem HEIC/AVIF stack (libde265 + libaom + libheif) is appended in Phase 9 Stage B.

echo ==^> Codecs installed into vendor\codecs-prefix
popd
exit /b 0

:build_codec
rem %1 = name  %2 = src dir  %3.. = extra cmake args
set "CNAME=%~1"
set "CSRC=%~2"
shift
shift
set "CARGS="
:collect_args
if "%~1"=="" goto :do_build
set "CARGS=!CARGS! %~1"
shift
goto :collect_args
:do_build
if exist "%CODEC_PREFIX%\lib\*%CNAME%*" (
    echo ==^> codec %CNAME% already installed — skipping.
    exit /b 0
)
echo ==^> Building vendored %CNAME% ^(static^)...
cmake -S "%CSRC%" -B "%CSRC%\build" -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DCMAKE_INSTALL_PREFIX="%CODEC_PREFIX%" ^
    -DCMAKE_INSTALL_LIBDIR=lib ^
    -DCMAKE_PREFIX_PATH="%CODEC_PREFIX%" ^
    !CARGS! || exit /b 1
cmake --build "%CSRC%\build" --parallel || exit /b 1
cmake --install "%CSRC%\build" || exit /b 1
exit /b 0

:fail
echo build_codecs.bat failed (see error above).
popd
exit /b 1
