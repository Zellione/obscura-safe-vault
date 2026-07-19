#!/usr/bin/env bash
# build_codecs.sh — build the vendored image codecs (WebP, HEIC/AVIF) as static
# libraries and install them into one staging prefix (vendor/codecs-prefix).
#
# Usage:
#   scripts/build_codecs.sh         # Default Release build
#   scripts/build_codecs.sh --asan  # ASAN-instrumented build into vendor/codecs-prefix-asan/
#
# Each codec is cmake-built static and `cmake --install`ed into the shared prefix
# so that dependents (libheif) discover their dependencies (libde265/libaom) via
# CMAKE_PREFIX_PATH, and premake5.lua links the whole set from a single dir.
#
# Called by scripts/setup.sh and directly by CI. Idempotent: a codec whose lib is
# already present in the prefix is skipped. Requires cmake, ninja, and nasm (for
# libaom's assembly).
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
ASAN=false

for arg in "$@"; do
    case "$arg" in
        --asan) ASAN=true ;;
        *)
            echo "Unknown option: $arg"
            exit 1
            ;;
    esac
done

if [[ "$ASAN" == true ]]; then
    CODEC_PREFIX="$REPO_ROOT/vendor/codecs-prefix-asan"
else
    CODEC_PREFIX="$REPO_ROOT/vendor/codecs-prefix"
fi

build_codec() {  # name  src_dir  [--builddir=path]  extra_cmake_args...
    local name="$1"; local src="$2"; shift 2
    local builddir="$src/build"
    if [[ "${1:-}" == --builddir=* ]]; then
        # libarchive ships its OWN tracked build/ directory (custom cmake
        # helper modules, autoconf leftovers) — an out-of-tree build at
        # $src/build would collide with and clobber those source files.
        builddir="${1#--builddir=}"
        shift
    fi
    # Check for the actual static lib, not a name-glob: a glob like "*z*" or
    # "*lzma*" also matches leftover pkgconfig/cmake-config files from a prior
    # partial/failed install (they're written before the .a, so a build that
    # fails mid-compile can still leave them behind), which falsely skipped a
    # real rebuild and left libarchive linked against whatever the system
    # happened to provide instead of the vendored static lib.
    if [[ -f "$CODEC_PREFIX/lib/lib${name}.a" ]]; then
        echo "==> codec $name already installed — skipping."
        return
    fi
    echo "==> Building vendored $name (static)..."

    # ASAN+UBSan instrumentation flags if --asan was passed.
    local cmake_c_flags=""
    local cmake_cxx_flags=""
    if [[ "$ASAN" == true ]]; then
        cmake_c_flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
        cmake_cxx_flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
    fi

    cmake -S "$src" -B "$builddir"                  \
        -DCMAKE_BUILD_TYPE=Release                  \
        -DBUILD_SHARED_LIBS=OFF                     \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON        \
        -DCMAKE_INSTALL_PREFIX="$CODEC_PREFIX"      \
        -DCMAKE_INSTALL_LIBDIR=lib                  \
        -DCMAKE_PREFIX_PATH="$CODEC_PREFIX"         \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5          \
        -DCMAKE_C_FLAGS="$cmake_c_flags"            \
        -DCMAKE_CXX_FLAGS="$cmake_cxx_flags"        \
        "$@" -G "Ninja"
    cmake --build "$builddir" --parallel "$NPROC"
    cmake --install "$builddir"
}

build_codec webp "$REPO_ROOT/vendor/libwebp"                            \
    -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF                  \
    -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF                    \
    -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF                    \
    -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF                  \
    -DWEBP_BUILD_EXTRAS=OFF

# HEIC decoder (HEVC). Skip the bundled example apps (dec265/enc265/...).
build_codec de265 "$REPO_ROOT/vendor/libde265"                         \
    -DENABLE_DECODER=OFF -DENABLE_ENCODER=OFF -DENABLE_SDL=OFF

# AV1 decoder for AVIF. Decoder-only (no encoder/tests/tools/docs) to cut the
# build down. Requires nasm for its assembly.
build_codec aom "$REPO_ROOT/vendor/libaom"                             \
    -DCONFIG_AV1_ENCODER=0 -DENABLE_TESTS=OFF -DENABLE_EXAMPLES=OFF     \
    -DENABLE_TOOLS=OFF -DENABLE_DOCS=OFF

# libheif ties it together; decoders are baked in statically (no plugin loading)
# and it finds libde265/libaom via CMAKE_PREFIX_PATH (the staging prefix).
build_codec heif "$REPO_ROOT/vendor/libheif"                           \
    -DWITH_LIBDE265=ON -DWITH_AOM_DECODER=ON -DWITH_AOM_ENCODER=OFF     \
    -DWITH_X265=OFF -DWITH_EXAMPLES=OFF -DWITH_GDK_PIXBUF=OFF           \
    -DENABLE_PLUGIN_LOADING=OFF -DBUILD_TESTING=OFF                     \
    -DCMAKE_DISABLE_FIND_PACKAGE_Doxygen=ON                            \
    -DCMAKE_C_FLAGS=-DLIBDE265_STATIC_BUILD                            \
    -DCMAKE_CXX_FLAGS=-DLIBDE265_STATIC_BUILD

# zlib — gzip filter for libarchive (.tar.gz). Skip its bundled example/test
# binaries; only the static lib + headers are needed.
build_codec z "$REPO_ROOT/vendor/zlib"                                 \
    -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_TESTING=OFF

# xz / liblzma — LZMA2 filter for libarchive (most real-world .7z entries use
# LZMA2, and it also covers .txz). Skip the xz/xzdec/lzmadec/lzmainfo CLI tools
# and docs; only the static liblzma + headers are needed. XZ_SANDBOX=no: xz's
# own Landlock-sandboxing configure check hard-errors when it sees
# '-fsanitize=' in CFLAGS (introduced for --asan builds), unrelated to
# anything we actually need from its optional seccomp/Landlock hardening.
build_codec lzma "$REPO_ROOT/vendor/xz"                                \
    -DBUILD_SHARED_LIBS=OFF -DXZ_NLS=OFF -DXZ_DOC=OFF -DXZ_SANDBOX=no  \
    -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF         \
    -DXZ_TOOL_LZMAINFO=OFF

# libarchive — read-only 7z/RAR/TAR support (Phase 34). Finds the vendored
# zlib/liblzma above via CMAKE_PREFIX_PATH (same pattern libheif uses for
# libde265/libaom). Every other optional codec/library is disabled: no
# bzip2/lz4/lzo/zstd (not vendored — .tbz2 is out of scope, see ROADMAP),
# no crypto backend (OpenSSL/mbedTLS/Nettle/CNG — not needed for read-only,
# unencrypted archives yet), no libxml2/expat (xar format, unused), no
# ACL/xattr/iconv, and no bsdtar/bsdcpio/bsdcat/test binaries — we only need
# libarchive.a itself, linked in-process via archive_read_open_memory.
build_codec archive "$REPO_ROOT/vendor/libarchive"                     \
    --builddir="$REPO_ROOT/vendor/.libarchive-build"                   \
    -DENABLE_ZLIB=ON -DENABLE_LZMA=ON                                  \
    -DENABLE_BZip2=OFF -DENABLE_LZ4=OFF -DENABLE_LZO=OFF -DENABLE_ZSTD=OFF \
    -DENABLE_LIBB2=OFF -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF       \
    -DENABLE_NETTLE=OFF -DENABLE_CNG=OFF -DENABLE_LIBGCC=OFF           \
    -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_WIN32_XMLLITE=OFF \
    -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF                     \
    -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_ICONV=OFF             \
    -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF \
    -DENABLE_TEST=OFF -DENABLE_WERROR=OFF

# VAAPI dlopen shim (Phase 43 Part 2) -- must be built and its pkgconfig
# files installed into $CODEC_PREFIX BEFORE FFmpeg's own ./configure runs
# below, since that configure invocation's --enable-vaapi probes resolve
# against this shim's synthesized libva.pc/libva-drm.pc (see
# vendor/vaapi-shim/CMakeLists.txt and
# docs/superpowers/specs/2026-07-17-hardware-video-decode-design.md).
build_codec osv_vaapi_shim "$REPO_ROOT/vendor/vaapi-shim" \
    -DLIBVA_SRC="$REPO_ROOT/vendor/libva"

# FFmpeg — decode-only static. Video (h264/hevc + prores/dnxhd/mjpeg for .mov,
# Phase 28; vp8/vp9 for .webm, Phase 38; av1 for .webm/.mov + qtrle/cinepak for
# .mov, Phase 40) + audio (aac/opus/mp3/vorbis/flac/ac3). configure-built (not
# cmake), so it gets its own function. Idempotent: skip if libavcodec is
# already installed. Needs nasm (already required by libaom).
#
# AV1: FFmpeg's own native "av1" decoder is a hwaccel-dispatch shim only (no
# software decode path — it returns ENOSYS without a HW accelerator, confirmed
# by direct testing during Phase 40 implementation, contradicting the "native
# decoder, no new vendored dependency" pattern Phase 38 used for vp8/vp9).
# Real software AV1 decode needs libaom or dav1d; this project already vendors
# libaom (vendor/libaom, decoder-only, built by build_codec above for AVIF
# stills via libheif) — reused here as libaom-av1 rather than adding a new
# dependency. PKG_CONFIG_PATH points configure at the aom.pc this script just
# installed into $CODEC_PREFIX. Note the configure component name is
# `libaom_av1` (underscore — derived from the `ff_libaom_av1_decoder` extern
# symbol), distinct from the runtime/display decoder name `libaom-av1`
# (hyphen, VideoDecoder::open() matches on AV_CODEC_ID_AV1, not the name
# string, so this distinction is configure-only).
#
# VAAPI (Phase 43 Part 2): a hwaccel *dispatch registration* flag, just like
# Windows' --enable-d3d11va (scripts/build_ffmpeg_windows.sh) -- not a new
# system dependency. FFmpeg's hwcontext_vaapi.c and the vaapi_*.c decode glue
# reference real libva/libva-drm symbols directly (unlike hwcontext_d3d11va.c,
# which LoadLibrary/GetProcAddress's d3d11.dll/dxgi.dll itself), so those
# symbols are provided by vendor/vaapi-shim's own internal
# dlopen("libva.so.2")/dlsym() forwarding instead -- see
# docs/superpowers/specs/2026-07-17-hardware-video-decode-design.md. The
# build_codec osv_vaapi_shim call above installs the synthetic libva.pc/
# libva-drm.pc this configure invocation's --enable-vaapi probes against.
build_ffmpeg() {
    if [[ -f "$CODEC_PREFIX/lib/libavcodec.a" ]]; then
        echo "==> ffmpeg already installed — skipping."
        return
    fi
    echo "==> Building vendored ffmpeg (decode-only, static)..."
    local src="$REPO_ROOT/vendor/ffmpeg"

    # ASAN+UBSan instrumentation flags if --asan was passed.
    local extra_cflags=""
    local extra_ldflags=""
    if [[ "$ASAN" == true ]]; then
        extra_cflags="-fsanitize=address,undefined -fno-omit-frame-pointer"
        extra_ldflags="-fsanitize=address,undefined"
    fi

    ( cd "$src" && PKG_CONFIG_PATH="$CODEC_PREFIX/lib/pkgconfig" ./configure \
        --prefix="$CODEC_PREFIX"                                            \
        --enable-static --disable-shared                                    \
        --disable-everything --disable-programs --disable-doc               \
        --disable-network --disable-encoders --disable-muxers               \
        --disable-protocols --disable-devices --disable-filters             \
        --disable-bsfs --disable-autodetect                                 \
        --enable-libaom                                                     \
        --enable-vaapi                                                      \
        --enable-decoder=h264,hevc,prores,dnxhd,mjpeg,vp8,vp9,libaom_av1,qtrle,cinepak,aac,opus,mp3,vorbis,flac,ac3 \
        --enable-demuxer=mov,matroska,webm                                  \
        --enable-parser=h264,hevc,dnxhd,mjpeg,aac,vorbis,opus,flac,ac3,mpegaudio \
        --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb                      \
        --enable-swscale                                                    \
        --enable-pic                                                        \
        --extra-cflags="$extra_cflags"                                      \
        --extra-ldflags="$extra_ldflags" )
    make -C "$src" -j"$NPROC"
    make -C "$src" install
}

build_ffmpeg

# PDFium — PDF rendering, decode-only (Phase 30). Vendored via
# OlexiyKhokhlov/PDFium fork with CMake support. Generates static libpdfium.a.
# USE_STATIC_LIBRARY=ON forces STATIC target (defaults to SHARED).
# Note: PDFium's CMakeLists.txt has install(EXPORT...) that fails due to
# dependency export issues when building STATIC. We build the library directly
# then manually copy the .a file to avoid the export error.
build_pdfium() {
    local src="$REPO_ROOT/vendor/pdfium"
    local builddir="$src/build"

    if [[ -f "$CODEC_PREFIX/lib/libpdfium.a" ]]; then
        echo "==> pdfium already installed — skipping."
        return
    fi

    echo "==> Building vendored pdfium (static)..."

    local cmake_c_flags=""
    local cmake_cxx_flags=""
    if [[ "$ASAN" == true ]]; then
        cmake_c_flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
        cmake_cxx_flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
    fi

    cmake -S "$src" -B "$builddir"                  \
        -DCMAKE_BUILD_TYPE=Release                  \
        -DUSE_STATIC_LIBRARY=ON                     \
        -DBUILD_SHARED_LIBS=OFF                     \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON        \
        -DCMAKE_INSTALL_PREFIX="$CODEC_PREFIX"      \
        -DCMAKE_INSTALL_LIBDIR=lib                  \
        -DCMAKE_PREFIX_PATH="$CODEC_PREFIX"         \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5          \
        -DCMAKE_C_FLAGS="$cmake_c_flags"            \
        -DCMAKE_CXX_FLAGS="$cmake_cxx_flags"        \
        -G "Ninja"

    cmake --build "$builddir" --parallel "$NPROC"

    # Copy the built .a directly to avoid install(EXPORT) errors
    mkdir -p "$CODEC_PREFIX/lib" "$CODEC_PREFIX/include"
    find "$builddir" -name "libpdfium.a" -exec cp {} "$CODEC_PREFIX/lib/" \;
    cp -r "$src/public" "$CODEC_PREFIX/include/pdfium" 2>/dev/null || true
}

build_pdfium

echo "==> Codecs installed into $CODEC_PREFIX"
