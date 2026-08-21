#!/usr/bin/env bash
# build.sh — compile the project.
# Defaults to Debug build; pass --release for optimised.
#
# Usage:
#   scripts/build.sh             # Debug, Ninja
#   scripts/build.sh --release   # Release, Ninja
#   scripts/build.sh --gmake     # Debug, GNU Make
#   scripts/build.sh --clean     # wipe this config's outputs first, then build
#
# Flags combine, e.g.: scripts/build.sh --release --clean

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

"$REPO_ROOT/scripts/check_submodules.sh"

# Core count fallback for systems without nproc.
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

CONFIG="Debug"
USE_GMAKE=false
CLEAN=false

for arg in "$@"; do
    case "$arg" in
        --release) CONFIG="Release" ;;
        --gmake)   USE_GMAKE=true   ;;
        --clean)   CLEAN=true       ;;
        *)
            echo "Unknown option: $arg"
            exit 1
            ;;
    esac
done

if [[ "$USE_GMAKE" = true ]]; then
    if [[ ! -f "Makefile" ]]; then
        echo "No Makefile found — run: scripts/gen.sh --gmake"
        exit 1
    fi
    CONFIG_LC="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
    if [[ "$CLEAN" = true ]]; then
        echo "==> Cleaning ${CONFIG_LC}_x64..."
        make config="${CONFIG_LC}_x64" clean
    fi
    # Per-project target: build the app only. The aggregate config target would
    # also compile osv_tests, which recompiles the shared src/ sources a second
    # time (fixture defines differ) on top of ~60 test TUs — roughly double the
    # work for a script whose job is producing the osv binary. scripts/test.sh
    # still builds the whole config (osv + osv_tests) before running the suite.
    make osv config="${CONFIG_LC}_x64" -j"$NPROC"
else
    if [[ ! -f "build.ninja" ]]; then
        echo "No build.ninja found — run: scripts/gen.sh"
        exit 1
    fi
    # premake5-generated Ninja targets use <Project>_<Config>_<Platform>; the
    # bare <Config>_<Platform> aggregate would also build osv_tests (see the
    # gmake comment above), so build the app's own target only.
    if [[ "$CLEAN" = true ]]; then
        echo "==> Cleaning ${CONFIG}_x64..."
        ninja -t clean "${CONFIG}_x64"
    fi
    ninja "osv_${CONFIG}_x64"
fi

echo ""
echo "Built: build/bin/${CONFIG}/osv"
