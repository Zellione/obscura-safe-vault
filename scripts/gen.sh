#!/usr/bin/env bash
# gen.sh — generate build files from premake5.lua
# Defaults to Ninja; pass --gmake to use gmake2 instead.
#
# Usage:
#   scripts/gen.sh           # Ninja (recommended)
#   scripts/gen.sh --gmake   # GNU Make fallback

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

PREMAKE="$REPO_ROOT/bin/premake5"
if [ ! -f "$PREMAKE" ]; then
    echo "premake5 not found at bin/premake5 — run scripts/setup.sh first"
    exit 1
fi

if [ "${1:-}" = "--gmake" ]; then
    echo "==> Generating GNU Makefiles..."
    "$PREMAKE" gmake2
else
    echo "==> Generating Ninja build files..."
    "$PREMAKE" ninja
fi
