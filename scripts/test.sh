#!/usr/bin/env bash
# test.sh — generate, build, and run the osv unit/integration tests.
#
# Usage:
#   scripts/test.sh             # Debug, Ninja
#   scripts/test.sh --release   # Release, Ninja
#   scripts/test.sh --asan      # Debug + AddressSanitizer/UBSan
#   scripts/test.sh --gmake     # Debug, GNU Make
#
# Exit code is non-zero if any test fails (CI-friendly).

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

CONFIG="Debug"
USE_GMAKE=false
ASAN=false

for arg in "$@"; do
    case "$arg" in
        --release) CONFIG="Release" ;;
        --gmake)   USE_GMAKE=true   ;;
        --asan)    ASAN=true        ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

PREMAKE="$REPO_ROOT/bin/premake5"
if [[ ! -f "$PREMAKE" ]]; then
    echo "premake5 not found at bin/premake5 — run scripts/setup.sh first"
    exit 1
fi

# --- Generate build files (re-run so the --asan flag takes effect) ----------
PREMAKE_OPTS=()
$ASAN && PREMAKE_OPTS+=("--asan")

if [[ "$USE_GMAKE" = true ]]; then
    echo "==> Generating GNU Makefiles..."
    "$PREMAKE" "${PREMAKE_OPTS[@]}" gmake2
    echo "==> Building osv_tests ($CONFIG)..."
    make config="${CONFIG,,}_x64" osv_tests -j"$(nproc)"
else
    echo "==> Generating Ninja build files..."
    "$PREMAKE" "${PREMAKE_OPTS[@]}" ninja
    # Repair header-dependency tracking the premake beta8 ninja exporter omits.
    "$REPO_ROOT/scripts/fix_ninja_deps.sh"
    echo "==> Building osv_tests ($CONFIG)..."
    ninja "osv_tests_${CONFIG}_x64"
fi

# --- Run --------------------------------------------------------------------
BIN="build/bin/${CONFIG}/osv_tests"
if [[ ! -x "$BIN" ]]; then
    echo "Test binary not found at $BIN"
    exit 1
fi

echo ""
echo "==> Running $BIN"
# Surface leaks on exit (no-op when not built with ASAN).
export ASAN_OPTIONS="${ASAN_OPTIONS:-}:detect_leaks=1"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-}:print_stacktrace=1:halt_on_error=1"
"$BIN"
