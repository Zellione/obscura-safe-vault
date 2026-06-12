@echo off
rem build_codecs.bat — build vendored image codecs (WebP, HEIC/AVIF) as static
rem libs into vendor\codecs-prefix. Called by setup.bat and by CI.
rem Requires cmake, Ninja, and nasm (for libaom) on PATH.
rem
rem Per-codec cmake flags are passed via the EXTRA variable rather than as
rem subroutine arguments: cmd splits a "-DVAR=OFF" token on the '=' when it is a
rem %1-style argument, which would corrupt the flags.
setlocal enabledelayedexpansion
pushd "%~dp0.."
set "CODEC_PREFIX=%CD%\vendor\codecs-prefix"

set "EXTRA=-DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF"
call :build_codec webp vendor\libwebp || goto :fail

set "EXTRA=-DENABLE_DECODER=OFF -DENABLE_ENCODER=OFF -DENABLE_SDL=OFF"
call :build_codec de265 vendor\libde265 || goto :fail

set "EXTRA=-DCONFIG_AV1_ENCODER=0 -DENABLE_TESTS=OFF -DENABLE_EXAMPLES=OFF -DENABLE_TOOLS=OFF -DENABLE_DOCS=OFF"
call :build_codec aom vendor\libaom || goto :fail

set "EXTRA=-DWITH_LIBDE265=ON -DWITH_AOM_DECODER=ON -DWITH_AOM_ENCODER=OFF -DWITH_X265=OFF -DWITH_EXAMPLES=OFF -DWITH_GDK_PIXBUF=OFF -DENABLE_PLUGIN_LOADING=OFF -DBUILD_TESTING=OFF"
call :build_codec heif vendor\libheif || goto :fail

echo ==^> Codecs installed into vendor\codecs-prefix
popd
exit /b 0

:build_codec
rem %1 = name  %2 = src dir  (extra cmake flags come from EXTRA)
set "CNAME=%~1"
set "CSRC=%~2"
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
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
    !EXTRA! || exit /b 1
cmake --build "%CSRC%\build" --parallel || exit /b 1
cmake --install "%CSRC%\build" || exit /b 1
exit /b 0

:fail
echo build_codecs.bat failed (see error above).
popd
exit /b 1
