#!/usr/bin/env bash
# package_macos.sh — build dist/ObscuraSafeVault-<version>-macos-<arch>.zip
# containing "Obscura Safe Vault.app". Ad-hoc signed (codesign -s -); real
# Developer-ID signing/notarisation requires Apple credentials (out of scope).
#
# Usage:
#   scripts/build.sh --release && scripts/package_macos.sh
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

VERSION="${OSV_VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}"
ARCH="$(uname -m)"
BIN="build/bin/Release/osv"
[[ -x "$BIN" ]] || { echo "Release binary missing — run: scripts/build.sh --release"; exit 1; }

APP="dist/Obscura Safe Vault.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/osv"
# SDL_GetBasePath() inside a bundle = Contents/Resources/ — assets go there.
cp -r assets "$APP/Contents/Resources/"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>               <string>Obscura Safe Vault</string>
    <key>CFBundleDisplayName</key>        <string>Obscura Safe Vault</string>
    <key>CFBundleIdentifier</key>         <string>dev.zellione.obscurasafevault</string>
    <key>CFBundleVersion</key>            <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key> <string>${VERSION}</string>
    <key>CFBundleExecutable</key>         <string>osv</string>
    <key>CFBundlePackageType</key>        <string>APPL</string>
    <key>NSHighResolutionCapable</key>    <true/>
    <key>LSMinimumSystemVersion</key>     <string>13.0</string>
</dict>
</plist>
EOF

codesign --force --deep -s - "$APP" || echo "codesign (ad-hoc) failed — unsigned bundle"

mkdir -p dist
ZIP="dist/ObscuraSafeVault-${VERSION}-macos-${ARCH}.zip"
rm -f "$ZIP"
(cd dist && zip -qry "$(basename "$ZIP")" "Obscura Safe Vault.app")
echo "Packaged: $ZIP"
