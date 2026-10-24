#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Generate CC0 synthetic VP8 color bars; requires ffmpeg with libvpx.

No external imagery. Integer BT.601 limited-range YUV primaries, 65x49,
12 frames at 6 fps; bars reverse halfway through to detect stale frames.
Bitexact muxing and one encoder thread make regeneration deterministic for
one ffmpeg/libvpx version (codec upgrades may change the encoded bytes).
"""
from pathlib import Path
import subprocess

WIDTH, HEIGHT = 65, 49
COLORS = ((81, 90, 240), (145, 54, 34), (41, 240, 110))
frames = bytearray()
for frame in range(12):
    colors = COLORS if frame < 6 else COLORS[::-1]
    for plane in range(3):
        width = WIDTH if plane == 0 else (WIDTH + 1) // 2
        height = HEIGHT if plane == 0 else (HEIGHT + 1) // 2
        # Boundaries align to chroma samples (x=22 and x=44).
        row = bytes(colors[min((x if plane == 0 else x * 2) // 22, 2)][plane]
                    for x in range(width))
        frames.extend(row * height)
subprocess.run([
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo",
    "-pixel_format", "yuv420p", "-video_size", f"{WIDTH}x{HEIGHT}",
    "-framerate", "6", "-i", "pipe:0", "-an", "-c:v", "libvpx",
    "-threads", "1", "-deadline", "best", "-cpu-used", "0", "-g", "3",
    "-auto-alt-ref", "0", "-lag-in-frames", "0", "-qmin", "4", "-qmax", "4",
    "-b:v", "200k", "-color_range", "tv", "-colorspace", "smpte170m",
    "-fflags", "+bitexact", "-flags:v", "+bitexact", "-map_metadata", "-1",
    str(Path(__file__).with_name("vp8_colors_odd.webm")),
], input=frames, check=True)
