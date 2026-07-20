#!/usr/bin/env bash
# build_ffmpeg_windows.sh — build vendor/ffmpeg for Windows using FFmpeg's
# --toolchain=msvc (native MSVC-ABI objects/libs, directly linkable into osv's
# MSVC-built exe — unlike a MinGW build, which is a different runtime/ABI and
# would need a compatibility shim).
#
# FFmpeg's own build system always names static libs `lib<name>.a` regardless
# of toolchain (unlike cmake, which switches to `<name>.lib` under MSVC) — see
# link_av() in premake5.lua, which checks lib/libavcodec.a on every platform to
# detect the FFmpeg leg (that check needs no change here). But premake's
# `links {"avformat","avcodec",...}` — identical on every platform — has MSVC
# look for bare `<name>.lib`, so this script additionally copies each
# lib<name>.a to <name>.lib after the build.
#
# Must run inside an MSYS2 bash shell with cl.exe/link.exe/lib.exe AND nasm on
# PATH (msvc-dev-cmd run first; MSYS2 path-type: inherit) — see the Windows
# legs in .github/workflows/ci.yml. MSYS2 ships its own coreutils `link.exe`
# (a symlink tool) that shadows MSVC's `link.exe` if found first on PATH; the
# CI step renames it out of the way before invoking this script.
set -euo pipefail
cd "$(dirname "$0")/.."
CODEC_PREFIX="$(pwd)/vendor/codecs-prefix"

if [[ -f "$CODEC_PREFIX/lib/libavcodec.a" ]]; then
    echo "==> ffmpeg already installed — skipping."
    exit 0
fi

echo "==> Building vendored ffmpeg for Windows (MSVC toolchain, decode-only, static)..."
# eac3 alongside ac3: ac3dec_float.c gates its E-AC-3 code paths with a runtime
# `if (CONFIG_EAC3_DECODER)` (not a preprocessor #if), which GCC/Clang dead-
# code-eliminate but this MSVC build does not — with eac3 disabled,
# ac3dec_float.o still references eac3_data.o's tables (ff_eac3_*), which then
# never gets compiled/archived: an unresolved-external link error that only
# surfaces on the MSVC leg.
#
# AV1: FFmpeg's own native "av1" decoder is a hwaccel-dispatch shim only (no
# software decode path). Real software AV1 decode reuses the already-vendored
# libaom (built by build_codecs.bat) as libaom-av1 — see build_codecs.sh's
# build_ffmpeg() for the full explanation, including the libaom_av1 (configure
# component name) vs libaom-av1 (runtime decoder name) naming gotcha.
# PKG_CONFIG_PATH points configure at the aom.pc build_codecs.bat installed
# into $CODEC_PREFIX; requires the `pkgconf` MSYS2 package (see ci.yml's
# MSYS2 setup step).
#
# D3D11VA (Phase 43 Part 1): a hwaccel *dispatch registration* flag, not a
# new dependency — FFmpeg's hwcontext_d3d11va.c loads d3d11.dll/dxgi.dll via
# LoadLibrary/GetProcAddress at runtime (confirmed in
# vendor/ffmpeg/libavutil/hwcontext_d3d11va.c), so this needs no new
# --extra-libs and no Windows SDK .lib linking beyond what MSVC already
# provides. Covers h264/hevc/vp9 hwaccel (vp8/mjpeg/av1/prores/dnxhd/qtrle/
# cinepak have no D3D11VA path in this FFmpeg version — see
# docs/superpowers/specs/2026-07-17-hardware-video-decode-design.md's
# codec-coverage table).
(
    cd vendor/ffmpeg
    PKG_CONFIG_PATH="$CODEC_PREFIX/lib/pkgconfig" ./configure \
        --toolchain=msvc \
        --target-os=win64 \
        --arch=x86_64 \
        --prefix="$CODEC_PREFIX" \
        --enable-static --disable-shared \
        --disable-everything --disable-programs --disable-doc \
        --disable-network --disable-encoders --disable-muxers \
        --disable-protocols --disable-devices --disable-filters \
        --disable-bsfs --disable-autodetect \
        --enable-libaom \
        --enable-decoder=h264,hevc,prores,dnxhd,mjpeg,vp8,vp9,libaom_av1,qtrle,cinepak,gif,aac,opus,mp3,vorbis,flac,ac3,eac3 \
        --enable-demuxer=mov,matroska,webm,gif \
        --enable-parser=h264,hevc,dnxhd,mjpeg,gif,aac,vorbis,opus,flac,ac3,mpegaudio \
        --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb \
        --enable-swscale \
        --enable-d3d11va \
        --enable-pic
    make -j"$(nproc)"
    make install
)

# See the file header: premake expects bare <name>.lib on Windows/MSVC, FFmpeg
# always produces lib<name>.a — keep both so detection and linking each resolve.
for name in avformat avcodec swscale swresample avutil; do
    cp "$CODEC_PREFIX/lib/lib$name.a" "$CODEC_PREFIX/lib/$name.lib"
done

echo "==> ffmpeg installed into $CODEC_PREFIX"
