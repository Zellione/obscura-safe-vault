#!/usr/bin/env bash
# Build the pinned SQLCipher submodule as a static validation dependency.
# Phase 105 introduced the validation probe; Phase 107 links this build into
# both the production application and tests.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SQLCIPHER_SRC="$REPO_ROOT/vendor/sqlcipher"
BUILD_SUFFIX=""
CFLAGS_VALUE="-O2 -fPIC"
OPENSSL_ARGS=()

if [[ "${1:-}" == "--asan" ]]; then
    BUILD_SUFFIX="-asan"
    CFLAGS_VALUE="-O1 -g -fPIC -fsanitize=address,undefined -fno-omit-frame-pointer"
    OPENSSL_ARGS+=("--asan")
elif [[ $# -ne 0 ]]; then
    echo "Usage: $0 [--asan]" >&2
    exit 1
fi

BUILD_DIR="$REPO_ROOT/vendor/.sqlcipher-build${BUILD_SUFFIX}"
OPENSSL_PREFIX="$REPO_ROOT/vendor/openssl-prefix${BUILD_SUFFIX}"

"$REPO_ROOT/scripts/build_openssl.sh" "${OPENSSL_ARGS[@]}"

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
            CPPFLAGS="-I$OPENSSL_PREFIX/include -DSQLITE_HAS_CODEC -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown" \
            LIBS="$OPENSSL_PREFIX/lib/libcrypto.a -ldl -pthread"
    )
fi

make -C "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)" libsqlite3.a
