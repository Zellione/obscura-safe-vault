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

set "EXTRA=-DWITH_LIBDE265=ON -DWITH_AOM_DECODER=ON -DWITH_AOM_ENCODER=OFF -DWITH_X265=OFF -DWITH_EXAMPLES=OFF -DWITH_GDK_PIXBUF=OFF -DENABLE_PLUGIN_LOADING=OFF -DBUILD_TESTING=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Doxygen=ON -DCMAKE_C_FLAGS=-DLIBDE265_STATIC_BUILD -DCMAKE_CXX_FLAGS=-DLIBDE265_STATIC_BUILD"
call :build_codec heif vendor\libheif || goto :fail

rem zlib — gzip filter for libarchive (.tar.gz).
set "EXTRA=-DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_TESTING=OFF"
call :build_codec z vendor\zlib || goto :fail

rem xz / liblzma — LZMA2 filter for libarchive (most .7z entries + .txz).
set "EXTRA=-DXZ_NLS=OFF -DXZ_DOC=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF"
call :build_codec lzma vendor\xz || goto :fail

rem libarchive — read-only 7z/RAR/TAR support (Phase 34). Finds the vendored
rem zlib/liblzma above via CMAKE_PREFIX_PATH. Builds out-of-tree into a sibling
rem dir (not vendor\libarchive\build) because libarchive's OWN source tree
rem ships a tracked build\ directory (cmake helper modules) that an in-tree
rem out-of-tree build would collide with. POSIX_REGEX_LIB=NONE: we only need
rem read/extract (archive_read_*), never the pattern-matching archive_match_*
rem API that needs POSIX regex — on Windows the default AUTO setting falls
rem through libc/libregex (absent) to a LIBPCREPOSIX branch that hard-errors
rem ("libgcc not found") unless ENABLE_LIBGCC=ON, even though we disabled
rem PCREPOSIX outright; NONE skips that whole optional cascade.
set "EXTRA=-DENABLE_ZLIB=ON -DENABLE_LZMA=ON -DENABLE_BZip2=OFF -DENABLE_LZ4=OFF -DENABLE_LZO=OFF -DENABLE_ZSTD=OFF -DENABLE_LIBB2=OFF -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF -DENABLE_NETTLE=OFF -DENABLE_CNG=OFF -DENABLE_LIBGCC=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_WIN32_XMLLITE=OFF -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF -DPOSIX_REGEX_LIB=NONE -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_ICONV=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF -DENABLE_TEST=OFF -DENABLE_WERROR=OFF"
call :build_codec archive vendor\libarchive vendor\.libarchive-build || goto :fail

echo ==^> Codecs installed into vendor\codecs-prefix
popd
exit /b 0

:build_codec
rem %1 = name  %2 = src dir  %3 = optional build dir override (default: %2\build)
rem (extra cmake flags come from EXTRA)
set "CNAME=%~1"
set "CSRC=%~2"
set "CBUILD=%~3"
if "%CBUILD%"=="" set "CBUILD=%CSRC%\build"
if exist "%CODEC_PREFIX%\lib\*%CNAME%*" (
    echo ==^> codec %CNAME% already installed — skipping.
    exit /b 0
)
echo ==^> Building vendored %CNAME% ^(static^)...
cmake -S "%CSRC%" -B "%CBUILD%" -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DCMAKE_INSTALL_PREFIX="%CODEC_PREFIX%" ^
    -DCMAKE_INSTALL_LIBDIR=lib ^
    -DCMAKE_PREFIX_PATH="%CODEC_PREFIX%" ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
    !EXTRA! || exit /b 1
cmake --build "%CBUILD%" --parallel || exit /b 1
cmake --install "%CBUILD%" || exit /b 1
exit /b 0

:fail
echo build_codecs.bat failed (see error above).
popd
exit /b 1
