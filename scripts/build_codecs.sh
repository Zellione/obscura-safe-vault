#!/usr/bin/env bash
# build_codecs.sh — build the vendored image codecs (WebP, HEIC/AVIF) as static
# libraries and install them into one staging prefix (vendor/codecs-prefix).
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
CODEC_PREFIX="$REPO_ROOT/vendor/codecs-prefix"

build_codec() {  # name  src_dir  extra_cmake_args...
    local name="$1"; local src="$2"; shift 2
    if [[ -d "$CODEC_PREFIX/lib" ]] && \
       find "$CODEC_PREFIX/lib" -name "*${name}*" 2>/dev/null | grep -q .; then
        echo "==> codec $name already installed — skipping."
        return
    fi
    echo "==> Building vendored $name (static)..."
    cmake -S "$src" -B "$src/build"                 \
        -DCMAKE_BUILD_TYPE=Release                  \
        -DBUILD_SHARED_LIBS=OFF                     \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON        \
        -DCMAKE_INSTALL_PREFIX="$CODEC_PREFIX"      \
        -DCMAKE_INSTALL_LIBDIR=lib                  \
        -DCMAKE_PREFIX_PATH="$CODEC_PREFIX"         \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5          \
        "$@" -G "Ninja"
    cmake --build "$src/build" --parallel "$NPROC"
    cmake --install "$src/build"
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
    -DENABLE_PLUGIN_LOADING=OFF -DBUILD_TESTING=OFF

echo "==> Codecs installed into vendor/codecs-prefix"
