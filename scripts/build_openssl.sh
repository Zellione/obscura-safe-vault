#!/usr/bin/env bash
# Build the pinned static OpenSSL LTS provider used by SQLCipher. The build also
# emits libssl, but the application and SQLCipher link only libcrypto.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE_DIR="$REPO_ROOT/vendor/openssl"
SUFFIX=""
EXTRA_CFLAGS=()

if [[ "${1:-}" == "--asan" ]]; then
    SUFFIX="-asan"
    EXTRA_CFLAGS+=("-O1" "-g" "-fsanitize=address,undefined" "-fno-omit-frame-pointer")
elif [[ $# -ne 0 ]]; then
    echo "Usage: $0 [--asan]" >&2
    exit 1
fi

BUILD_DIR="$REPO_ROOT/vendor/.openssl-build${SUFFIX}"
PREFIX="$REPO_ROOT/vendor/openssl-prefix${SUFFIX}"

if [[ ! -f "$SOURCE_DIR/Configure" ]]; then
    echo "OpenSSL submodule is missing; run git submodule update --init vendor/openssl" >&2
    exit 1
fi

if [[ ! -f "$BUILD_DIR/Makefile" ]]; then
    mkdir -p "$BUILD_DIR"
    (
        cd "$BUILD_DIR"
        perl "$SOURCE_DIR/Configure" linux-x86_64 \
            --prefix="$PREFIX" \
            --libdir=lib \
            no-shared no-tests no-apps no-docs no-legacy no-module no-dso no-engine \
            "${EXTRA_CFLAGS[@]}"
    )
fi

make -C "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)" build_libs
make -C "$BUILD_DIR" install_dev
