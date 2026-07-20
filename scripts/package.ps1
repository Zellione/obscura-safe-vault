# package.ps1 — build a Windows release zip: dist\osv-<version>-windows-x64.zip
# Contains: osv.exe, assets\, LICENSE, README.md.
#
# Portable by design: unzip anywhere and run osv.exe. There is no installer —
# the app writes nothing outside its own folder and the user's vault files, so
# "uninstall" is deleting the folder. See docs/roadmap/phase-08-cross-platform-ports.md.
#
# Usage:
#   msbuild ObscuraSafeVault.sln /p:Configuration=Release /p:Platform=x64
#   $env:OSV_VERSION = "1.2.0"; scripts\package.ps1
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

# Avoid `??` and other 7-only syntax so this also runs under Windows PowerShell 5.1.
$version = $env:OSV_VERSION
if (-not $version) { $version = (git describe --tags --always 2>$null) }
if (-not $version) { $version = "dev" }
$bin = "build\bin\Release\osv.exe"

if (-not (Test-Path $bin)) {
    throw "Release binary missing at $bin — build the Release x64 configuration first."
}

$pkg = "osv-$version-windows-x64"
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString())
$stagePkg = Join-Path $stage $pkg

try {
    New-Item -ItemType Directory -Force -Path $stagePkg | Out-Null

    Copy-Item $bin              -Destination $stagePkg
    Copy-Item "assets"          -Destination $stagePkg -Recurse
    Copy-Item "LICENSE"         -Destination $stagePkg
    Copy-Item "README.md"       -Destination $stagePkg

    New-Item -ItemType Directory -Force -Path "dist" | Out-Null
    $out = Join-Path $repoRoot "dist\$pkg.zip"
    Remove-Item $out -ErrorAction SilentlyContinue
    Compress-Archive -Path $stagePkg -DestinationPath $out

    Write-Host "Packaged: dist\$pkg.zip"
}
finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
