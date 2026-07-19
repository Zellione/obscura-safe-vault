#!/usr/bin/env bash
# build_pdfium.sh — Build upstream PDFium as a static library using its native GN build system.
#
# This build requires network access (depot_tools clone + gclient sync).
# PDFium is the ONE exception to this project's "offline after checkout" discipline —
# see docs/VENDORED_DEPS.md for rationale.
#
# Usage:
#   scripts/build_pdfium.sh         # Build into vendor/codecs-prefix
#   scripts/build_pdfium.sh --asan  # ASAN-instrumented build into vendor/codecs-prefix-asan/
#
# Requires: git, python3, ninja (https://github.com/ninja-build/ninja/releases)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

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

# Skip if already built
if [[ -f "$CODEC_PREFIX/lib/libpdfium.a" ]]; then
    echo "==> pdfium already installed — skipping."
    exit 0
fi

echo "==> Building PDFium (upstream, native GN build, static library)..."

# Paths
PDFIUM_SRC="$REPO_ROOT/vendor/pdfium"
BUILD_DIR="$PDFIUM_SRC/build-osv"
DEPOT_TOOLS_DIR="$BUILD_DIR/../depot-tools"

# Ensure depot_tools is present (network access required)
if [[ ! -d "$DEPOT_TOOLS_DIR" ]]; then
    echo "==> Fetching depot_tools from chromium.googlesource.com (network access)..."
    mkdir -p "$(dirname "$DEPOT_TOOLS_DIR")"
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS_DIR"
fi

# Add depot_tools to PATH
export PATH="$DEPOT_TOOLS_DIR:$PATH"

# Disable auto-update
export DEPOT_TOOLS_UPDATE=0
export DEPOT_TOOLS_WIN_TOOLCHAIN=0

# Clean old build
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Run gclient to fetch PDFium and dependencies (requires network)
cd "$PDFIUM_SRC"
echo "==> Running gclient sync to fetch PDFium dependencies..."
gclient config --unmanaged "https://chromium.googlesource.com/pdfium.git"
gclient sync -r HEAD --no-history --shallow

# Generate GN build files (standalone static library configuration)
echo "==> Generating GN build configuration..."
GN_ARGS="is_debug=false"
GN_ARGS="$GN_ARGS pdf_use_partition_alloc=false"
GN_ARGS="$GN_ARGS is_component_build=false"
GN_ARGS="$GN_ARGS pdf_is_standalone=true"
GN_ARGS="$GN_ARGS pdf_enable_v8=false"
GN_ARGS="$GN_ARGS pdf_enable_xfa=false"
GN_ARGS="$GN_ARGS treat_warnings_as_errors=false"
GN_ARGS="$GN_ARGS use_xcode_clang=false"

# ASAN instrumentation if requested
if [[ "$ASAN" == true ]]; then
    GN_ARGS="$GN_ARGS is_asan=true"
    GN_ARGS="$GN_ARGS is_ubsan=true"
fi

# Run gn gen to generate Ninja build files
gn gen "$BUILD_DIR" --args="$GN_ARGS"

# Build with ninja
echo "==> Building PDFium with Ninja..."
ninja -C "$BUILD_DIR" pdfium

# Extract built library and install
echo "==> Installing PDFium static library..."
mkdir -p "$CODEC_PREFIX/lib"
mkdir -p "$CODEC_PREFIX/include/pdfium"

# Find and copy the built static library
find "$BUILD_DIR" -name "libpdfium.a" -exec cp {} "$CODEC_PREFIX/lib/" \;

# Copy public headers
if [[ -d "$PDFIUM_SRC/public" ]]; then
    cp -r "$PDFIUM_SRC/public"/* "$CODEC_PREFIX/include/pdfium/"
fi

echo "==> PDFium build complete. Library at: $CODEC_PREFIX/lib/libpdfium.a"
