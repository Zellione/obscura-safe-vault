#!/bin/bash
# Generate media test fixtures using system ffmpeg
# Tier-1 legacy codec fixtures for test_legacy_decode.cpp

set -o pipefail

# Check if ffmpeg is available
if ! command -v ffmpeg &> /dev/null; then
    echo "Error: ffmpeg is not installed or not in PATH"
    exit 1
fi

# Output directory
FIXTURE_DIR="tests/media/fixtures"
mkdir -p "$FIXTURE_DIR"

echo "Generating test fixtures in $FIXTURE_DIR..."

# Helper: Generate a small test video with testsrc
# Usage: gen_video <filename> <codec> <container> <width> <height> <frames> [extra_args]
gen_video() {
    local filename="$1"
    local codec="$2"
    local ext="$3"
    local width="$4"
    local height="$5"
    local frames="$6"
    local extra_args="${7:-}"

    local filepath="$FIXTURE_DIR/$filename"

    # Skip if already exists
    if [ -f "$filepath" ]; then
        return
    fi

    ffmpeg -f lavfi -i "testsrc=s=${width}x${height}:d=1" \
        -frames:v "$frames" \
        -c:v "$codec" \
        $extra_args \
        -y "$filepath" 2>/dev/null || {
        echo "  [SKIP] $filename - encoder or muxer not available"
        rm -f "$filepath"
        return 1
    }
}

# Helper: Generate a small test video with testsrc2
gen_video2() {
    local filename="$1"
    local codec="$2"
    local ext="$3"
    local width="$4"
    local height="$5"
    local frames="$6"
    local extra_args="${7:-}"

    local filepath="$FIXTURE_DIR/$filename"

    if [ -f "$filepath" ]; then
        return
    fi

    ffmpeg -f lavfi -i "testsrc2=s=${width}x${height}:d=1" \
        -frames:v "$frames" \
        -c:v "$codec" \
        $extra_args \
        -y "$filepath" 2>/dev/null || {
        echo "  [SKIP] $filename - encoder or muxer not available"
        rm -f "$filepath"
        return 1
    }
}

# Helper: Generate a video with audio
# Usage: gen_video_audio <filename> <v_codec> <a_codec> <width> <height> <frames> [extra_args]
gen_video_audio() {
    local filename="$1"
    local v_codec="$2"
    local a_codec="$3"
    local width="$4"
    local height="$5"
    local frames="$6"
    local extra_args="${7:-}"

    local filepath="$FIXTURE_DIR/$filename"

    if [ -f "$filepath" ]; then
        return
    fi

    ffmpeg -f lavfi -i "testsrc=s=${width}x${height}:d=1" \
        -f lavfi -i "sine=f=440:d=1" \
        -frames:v "$frames" \
        -c:v "$v_codec" \
        -c:a "$a_codec" \
        $extra_args \
        -y "$filepath" 2>/dev/null || {
        echo "  [SKIP] $filename - encoder or muxer not available"
        rm -f "$filepath"
        return 1
    }
}

# Helper: Generate audio-only fixture
# Usage: gen_audio <filename> <codec> <container> [extra_args]
gen_audio() {
    local filename="$1"
    local codec="$2"
    local ext="$3"
    local extra_args="${4:-}"

    local filepath="$FIXTURE_DIR/$filename"

    if [ -f "$filepath" ]; then
        return
    fi

    ffmpeg -f lavfi -i "sine=f=440:d=1" \
        -c:a "$codec" \
        $extra_args \
        -y "$filepath" 2>/dev/null || {
        echo "  [SKIP] $filename - encoder or muxer not available"
        rm -f "$filepath"
        return 1
    }
}

echo "Video fixtures (MPEG family)..."
# MPEG-1 (mpeg1video) — MKV. mpeg1 rejects rate=10 ("MPEG-1/2 does not support 10/1 fps"); use 25.
ffmpeg -y -f lavfi -i testsrc2=size=160x120:rate=25 -frames:v 8 -c:v mpeg1video -q:v 5 \
    "$FIXTURE_DIR/tinylegacy_mpeg1.mkv" 2>/dev/null || {
    echo "  [SKIP] tinylegacy_mpeg1.mkv - encoder or muxer not available"
}
# MPEG-2 (mpeg2video) — MKV
ffmpeg -y -f lavfi -i testsrc2=size=160x120:rate=10 -frames:v 6 -c:v mpeg2video -q:v 5 \
    "$FIXTURE_DIR/tinylegacy_mpeg2.mkv" 2>/dev/null || {
    echo "  [SKIP] tinylegacy_mpeg2.mkv - encoder or muxer not available"
}
# MPEG-2 (mpeg2video) — MPEG-TS (also covers the MPEGTS container end-to-end; ~21 KB)
ffmpeg -y -f lavfi -i testsrc2=size=160x120:rate=10 -frames:v 6 -c:v mpeg2video -q:v 5 -f mpegts \
    "$FIXTURE_DIR/tinylegacy_mpeg2.ts" 2>/dev/null || {
    echo "  [SKIP] tinylegacy_mpeg2.ts - encoder or muxer not available"
}
# Interlaced MPEG-2 — MKV (validates Task 7's yadif path exists for interlaced legacy content; ~55 KB)
ffmpeg -y -f lavfi -i testsrc2=size=320x240:rate=10 -frames:v 6 -vf tinterlace=interleave_top \
    -flags +ilme+ildct -c:v mpeg2video -q:v 5 "$FIXTURE_DIR/tinylegacy_mpeg2_interlaced.mkv" 2>/dev/null || {
    echo "  [SKIP] tinylegacy_mpeg2_interlaced.mkv - encoder or muxer not available"
}
# NOTE: Raw MPEG-PS (.mpg) container is excluded — decode-only vendored FFmpeg cannot identify
# the program-stream elementary codec (full system ffmpeg reads it fine; our stripped build can't).
# MPEG-1/2 content is fully supported via MKV/TS/MP4/MOV containers.

echo "Video fixtures (MPEG-4 & variants)..."
gen_video "tinylegacy_mpeg4.avi" "mpeg4" "avi" 160 120 3
gen_video "tinylegacy_msmpeg4v2.avi" "msmpeg4v2" "avi" 160 120 3
gen_video "tinylegacy_msmpeg4v3.avi" "msmpeg4" "avi" 160 120 3 "-tag:v DIV3"

echo "Video fixtures (Windows Media Video)..."
gen_video "tinylegacy_wmv1.avi" "wmv1" "avi" 160 120 3
gen_video "tinylegacy_wmv2.avi" "wmv2" "avi" 160 120 3

echo "Video fixtures (H.263 - special resolution 176x144)..."
gen_video "tinylegacy_h263.avi" "h263" "avi" 176 144 3

echo "Video fixtures (Flash & SVQ)..."
gen_video "tinylegacy_flv1.flv" "flv1" "flv" 160 120 3
gen_video "tinylegacy_svq1.mov" "svq1" "mov" 160 120 3

echo "Video fixtures (DV - special resolution 720x480 NTSC, single frame)..."
gen_video "tinylegacy_dvvideo.avi" "dvvideo" "avi" 720 480 1

echo "Video fixtures (Lossless - small resolution to stay small)..."
gen_video "tinylegacy_msvideo1.avi" "msvideo1" "avi" 160 120 3
gen_video "tinylegacy_rpza.mov" "rpza" "mov" 160 120 3
gen_video "tinylegacy_huffyuv.avi" "huffyuv" "avi" 64 48 1
gen_video "tinylegacy_ffv1.mkv" "ffv1" "mkv" 64 48 1

echo "Video fixtures (Theora & RealVideo)..."
gen_video "tinylegacy_theora.ogv" "libtheora" "ogv" 160 120 3 "-q:v 5"
gen_video "tinylegacy_rv10.rm" "rv10" "rm" 160 120 3
gen_video "tinylegacy_rv20.rm" "rv20" "rm" 160 120 3

echo "Special fixtures (anamorphic)..."
# 704x576 with SAR 16/15 creates display aspect ratio 16/9 or similar
gen_video "tinylegacy_anamorphic.mkv" "libx264" "mkv" 704 576 3 \
    "-vf setsar=16/15 -crf 28 -preset ultrafast"

echo "Special fixtures (interlaced)..."
# Interlaced MPEG-2 is generated in the MPEG family section (see above).

echo "Audio fixtures (video + audio tracks)..."
# Start with WMV1 (MPEG-1 + MP2 audio not required for Tier-1 coverage).
gen_video_audio "tinylegacy_wmav1_audio.asf" "wmv1" "wmav1" 160 120 3
gen_video_audio "tinylegacy_wmav2_audio.asf" "wmv2" "wmav2" 160 120 3
gen_video_audio "tinylegacy_adpcm_ms_audio.avi" "mpeg4" "adpcm_ms" 160 120 3
gen_video_audio "tinylegacy_pcm_s16le_audio.mov" "prores" "pcm_s16le" 160 120 3

echo "Special fixture (ffvhuff - unmapped codec)..."
# ffvhuff in matroska container; vendored FFmpeg has no ffvhuff decoder
ffmpeg -f lavfi -i "testsrc2=s=160x120:d=0.1" \
    -frames:v 1 \
    -c:v ffvhuff \
    -y "$FIXTURE_DIR/tinylegacy_ffvhuff.mkv" 2>/dev/null || {
    echo "  [SKIP] tinylegacy_ffvhuff.mkv - encoder not available"
}

# ---- Phase 62: perceptual-video-duplicate fixtures ----
# dup_a_h264.mp4 and dup_a_vp9.webm encode the SAME deterministic content
# (high-contrast moving box on a gradient — stable dHash across codecs);
# dup_other.mp4 is visually different with the same duration.
DUP_SRC="testsrc2=s=64x64:d=2:r=10"
if [ ! -f "$FIXTURE_DIR/dup_a_h264.mp4" ]; then
    ffmpeg -y -f lavfi -i "$DUP_SRC" -c:v libx264 -pix_fmt yuv420p -r 10 \
        "$FIXTURE_DIR/dup_a_h264.mp4" -loglevel error
fi
if [ ! -f "$FIXTURE_DIR/dup_a_vp9.webm" ]; then
    ffmpeg -y -f lavfi -i "$DUP_SRC" -c:v libvpx-vp9 -pix_fmt yuv420p -r 10 \
        "$FIXTURE_DIR/dup_a_vp9.webm" -loglevel error
fi
if [ ! -f "$FIXTURE_DIR/dup_other.mp4" ]; then
    ffmpeg -y -f lavfi -i "smptebars=s=64x64:d=2" -c:v libx264 -pix_fmt yuv420p -r 10 \
        "$FIXTURE_DIR/dup_other.mp4" -loglevel error
fi

echo ""
echo "Fixture sizes:"
for f in "$FIXTURE_DIR"/tinylegacy_*.{mpg,avi,mov,flv,ogv,mkv,rm,asf,dv}; do
    [ -f "$f" ] && ls -lh "$f" | awk '{print "  " $9 ": " $5}'
done

echo ""
echo "Verifying fixture sizes (all must be ≤100 KB)..."
FAILED=0
for f in "$FIXTURE_DIR"/tinylegacy_*; do
    if [ -f "$f" ]; then
        SIZE=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null)
        if [ "$SIZE" -gt 102400 ]; then
            echo "  [OVERSIZED] $(basename "$f"): $SIZE bytes"
            FAILED=1
        fi
    fi
done

if [ $FAILED -eq 0 ]; then
    echo "  All fixtures within size limits ✓"
fi

echo "Generation complete!"
