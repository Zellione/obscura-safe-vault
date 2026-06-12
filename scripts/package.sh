#!/usr/bin/env bash
# package.sh — build a Linux release tarball: dist/osv-<version>-linux-<arch>.tar.gz
# Contains: osv binary, assets/, LICENSE, README.md, install.sh (to ~/.local by default).
#
# Usage:
#   scripts/build.sh --release && scripts/package.sh
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

VERSION="${OSV_VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}"
ARCH="$(uname -m)"
BIN="build/bin/Release/osv"

[[ -x "$BIN" ]] || { echo "Release binary missing — run: scripts/build.sh --release"; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PKG="osv-${VERSION}-linux-${ARCH}"
mkdir -p "$STAGE/$PKG"

cp "$BIN" "$STAGE/$PKG/"
cp -r assets "$STAGE/$PKG/"
cp LICENSE README.md "$STAGE/$PKG/"

cat > "$STAGE/$PKG/install.sh" <<'EOF'
#!/usr/bin/env bash
# Installs osv to PREFIX (default ~/.local): bin/osv + share/osv/assets.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
mkdir -p "$PREFIX/bin" "$PREFIX/share/osv"
install -m 755 "$HERE/osv" "$PREFIX/bin/osv"
cp -r "$HERE/assets" "$PREFIX/share/osv/"
# The binary resolves assets next to itself as a fallback; symlink them in.
ln -sfn "$PREFIX/share/osv/assets" "$PREFIX/bin/assets"
echo "Installed: $PREFIX/bin/osv"
EOF
chmod +x "$STAGE/$PKG/install.sh"

mkdir -p dist
tar -czf "dist/${PKG}.tar.gz" -C "$STAGE" "$PKG"
echo "Packaged: dist/${PKG}.tar.gz"
