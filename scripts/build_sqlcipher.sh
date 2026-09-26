#!/usr/bin/env bash
# Build the pinned SQLCipher submodule as a static validation dependency.
# Phase 105 links it only into osv_tests; production integration is Phase 107.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SQLCIPHER_SRC="$REPO_ROOT/vendor/sqlcipher"
BUILD_SUFFIX=""
CFLAGS_VALUE="-O2 -fPIC"

if [[ "${1:-}" == "--asan" ]]; then
    BUILD_SUFFIX="-asan"
    CFLAGS_VALUE="-O1 -g -fPIC -fsanitize=address,undefined -fno-omit-frame-pointer"
elif [[ $# -ne 0 ]]; then
    echo "Usage: $0 [--asan]" >&2
    exit 1
fi

BUILD_DIR="$REPO_ROOT/vendor/.sqlcipher-build${BUILD_SUFFIX}"

if [[ ! -x "$SQLCIPHER_SRC/configure" ]]; then
    echo "SQLCipher submodule is missing; run git submodule update --init vendor/sqlcipher" >&2
    exit 1
fi

if [[ ! -f "$BUILD_DIR/Makefile" ]]; then
    mkdir -p "$BUILD_DIR"
    (
        cd "$BUILD_DIR"
        "$SQLCIPHER_SRC/configure" \
            --disable-shared \
            --enable-static \
            --disable-load-extension \
            --with-tempstore=yes \
            CFLAGS="$CFLAGS_VALUE" \
            CPPFLAGS="-DSQLITE_HAS_CODEC -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown" \
            LIBS="-lcrypto"
    )
fi

make -C "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)" libsqlite3.a
