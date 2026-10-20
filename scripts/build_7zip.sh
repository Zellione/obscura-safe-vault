#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
suffix=""
flags=""
case "${1:-}" in
    --asan) suffix="-asan"; flags="-fsanitize=address,undefined -fno-omit-frame-pointer" ;;
    --tsan) suffix="-tsan"; flags="-fsanitize=thread -fno-omit-frame-pointer" ;;
    "") ;;
    *) echo "Usage: scripts/build_7zip.sh [--asan|--tsan]" >&2; exit 1 ;;
esac
cmake -S "$REPO_ROOT/vendor/7zip-shim" -B "$REPO_ROOT/vendor/.7zip-build$suffix" \
    -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$REPO_ROOT/vendor/codecs-prefix$suffix" \
    -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
    -DCMAKE_SHARED_LINKER_FLAGS="$flags"
cmake --build "$REPO_ROOT/vendor/.7zip-build$suffix" --parallel "$(nproc)"
cmake --install "$REPO_ROOT/vendor/.7zip-build$suffix"
# An SDK-only rebuild need not relink the app, but existing runnable targets
# must use the rebuilt shared library rather than a stale post-build copy.
for config in Debug Release; do
    target_dir="$REPO_ROOT/build/bin/$config$suffix"
    if [[ -d "$target_dir" ]]; then
        cp "$REPO_ROOT/vendor/codecs-prefix$suffix/lib/libosv7zip.so" "$target_dir/libosv7zip.so.new"
        mv -f "$target_dir/libosv7zip.so.new" "$target_dir/libosv7zip.so"
    fi
done
