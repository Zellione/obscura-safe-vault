#!/usr/bin/env bash
# test.sh — generate, build, and run the osv unit/integration tests.
#
# Usage:
#   scripts/test.sh             # Debug, Ninja
#   scripts/test.sh --release   # Release, Ninja
#   scripts/test.sh --asan      # Debug + AddressSanitizer/UBSan
#   scripts/test.sh --tsan      # Debug + ThreadSanitizer
#   scripts/test.sh --gmake     # Debug, GNU Make
#
# Exit code is non-zero if any test fails (CI-friendly).

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Core count fallback for systems without nproc.
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

CONFIG="Debug"
USE_GMAKE=false
ASAN=false
TSAN=false

for arg in "$@"; do
    case "$arg" in
        --release) CONFIG="Release" ;;
        --gmake)   USE_GMAKE=true   ;;
        --asan)    ASAN=true        ;;
        --tsan)    TSAN=true        ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

if [[ "$ASAN" == true && "$TSAN" == true ]]; then
    echo "--asan and --tsan cannot be combined in one binary"
    exit 1
fi

PREMAKE="$REPO_ROOT/bin/premake5"
if [[ ! -f "$PREMAKE" ]]; then
    echo "premake5 not found at bin/premake5 — run scripts/setup.sh first"
    exit 1
fi

# --- Generate build files (re-run so the --asan/--tsan flag takes effect) ---
PREMAKE_OPTS=()
$ASAN && PREMAKE_OPTS+=("--asan")
$TSAN && PREMAKE_OPTS+=("--tsan")

if [[ "$USE_GMAKE" = true ]]; then
    echo "==> Generating GNU Makefiles..."
    "$PREMAKE" "${PREMAKE_OPTS[@]}" gmake2
    # Build the whole config (osv + osv_tests), not just osv_tests: a compile
    # error in an app-only file (e.g. a screen class not linked into the test
    # binary) must fail this gate, not slip through to build.sh / CI.
    echo "==> Building osv + osv_tests ($CONFIG)..."
    CONFIG_LC="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
    make config="${CONFIG_LC}_x64" -j"$NPROC"
else
    echo "==> Generating Ninja build files..."
    "$PREMAKE" "${PREMAKE_OPTS[@]}" ninja
    # Repair header-dependency tracking the premake beta8 ninja exporter omits.
    "$REPO_ROOT/scripts/fix_ninja_deps.sh"
    # Build the whole config (osv + osv_tests), not just osv_tests: a compile
    # error in an app-only file (e.g. a screen class not linked into the test
    # binary) must fail this gate, not slip through to build.sh / CI.
    echo "==> Building osv + osv_tests ($CONFIG)..."
    ninja "${CONFIG}_x64"
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
# Stop at the first race with a full report (no-op when not built with TSan).
export TSAN_OPTIONS="${TSAN_OPTIONS:-}:halt_on_error=1:second_deadlock_stack=1"
"$BIN"
