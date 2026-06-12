#!/usr/bin/env bash
# build.sh — compile the project.
# Defaults to Debug build; pass --release for optimised.
#
# Usage:
#   scripts/build.sh             # Debug, Ninja
#   scripts/build.sh --release   # Release, Ninja
#   scripts/build.sh --gmake     # Debug, GNU Make

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Core count: nproc is Linux-only; macOS uses sysctl.
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

CONFIG="Debug"
USE_GMAKE=false

for arg in "$@"; do
    case "$arg" in
        --release) CONFIG="Release" ;;
        --gmake)   USE_GMAKE=true   ;;
        *)
            echo "Unknown option: $1"
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
    make config="${CONFIG_LC}_x64" -j"$NPROC"
else
    if [[ ! -f "build.ninja" ]]; then
        echo "No build.ninja found — run: scripts/gen.sh"
        exit 1
    fi
    # premake5-generated Ninja targets use <Config>_<Platform> (e.g. Debug_x64)
    ninja "${CONFIG}_x64"
fi

echo ""
echo "Built: build/bin/${CONFIG}/osv"
