#!/usr/bin/env bash
# setup.sh — one-time project bootstrap
# Run this after a fresh clone to initialise all vendored dependencies.
#
# What it does:
#   1. Init / update git submodules (SDL3, monocypher, stb)
#   2. Build vendored SDL3 as a static library with CMake
#   3. Download premake5 binary if not already present
#
# After setup.sh completes, run:
#   scripts/gen.sh    <- generate Ninja build files
#   scripts/build.sh  <- compile

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Core count: nproc is Linux-only; macOS uses sysctl.
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "==> Initialising git submodules..."
git submodule update --init --recursive

# ---------------------------------------------------------------------------
# premake5 binary — download if missing
# ---------------------------------------------------------------------------
PREMAKE_BIN="$REPO_ROOT/bin/premake5"
PREMAKE_VERSION="5.0.0-beta8"

if [[ ! -f "$PREMAKE_BIN" ]]; then
    echo "==> Downloading premake5 $PREMAKE_VERSION..."
    mkdir -p bin
    OS="$(uname -s)"
    case "$OS" in
        Linux)  ARCHIVE="premake-${PREMAKE_VERSION}-linux.tar.gz" ;;
        Darwin) ARCHIVE="premake-${PREMAKE_VERSION}-macosx.tar.gz" ;;
        *)      echo "Unsupported OS: $OS (add Windows support to this script)"; exit 1 ;;
    esac
    curl -L "https://github.com/premake/premake-core/releases/download/v${PREMAKE_VERSION}/${ARCHIVE}" \
         -o /tmp/premake5.tar.gz
    tar -xzf /tmp/premake5.tar.gz -C bin/
    chmod +x "$PREMAKE_BIN"
    echo "    premake5 ready at bin/premake5"
else
    echo "==> premake5 already present at bin/premake5 — skipping download."
fi

# ---------------------------------------------------------------------------
# Build vendored SDL3 (static lib, no shared, no tests, no tools)
# ---------------------------------------------------------------------------
SDL3_SRC="$REPO_ROOT/vendor/SDL3"
SDL3_BUILD="$REPO_ROOT/vendor/SDL3/build"

if [[ ! -f "$SDL3_BUILD/libSDL3.a" ]]; then
    echo "==> Building vendored SDL3 (static)..."
    cmake -S "$SDL3_SRC" -B "$SDL3_BUILD" \
        -DCMAKE_BUILD_TYPE=Release    \
        -DSDL_STATIC=ON               \
        -DSDL_SHARED=OFF              \
        -DSDL_TESTS=OFF               \
        -DSDL_EXAMPLES=OFF            \
        -DSDL_INSTALL_TESTS=OFF       \
        -G "Ninja"
    cmake --build "$SDL3_BUILD" --parallel "$NPROC"
    echo "    libSDL3.a built at vendor/SDL3/build/libSDL3.a"
else
    echo "==> vendored SDL3 already built — skipping cmake."
fi

# ---------------------------------------------------------------------------
# Vendored image codecs (WebP now; HEIC/AVIF stack added in Phase 9 Stage B).
# Each is cmake-built static and `cmake --install`ed into one staging prefix so
# that dependents (libheif) can find their dependencies (libde265/libaom) via
# CMAKE_PREFIX_PATH, and premake5.lua links the whole set from a single dir.
# ---------------------------------------------------------------------------
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

echo ""
echo "Setup complete. Next steps:"
echo "  scripts/gen.sh    # generate Ninja build files"
echo "  scripts/build.sh  # compile"
