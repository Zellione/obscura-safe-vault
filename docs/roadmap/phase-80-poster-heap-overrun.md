# sws_scale heap overrun — tight destination buffers (Phase 80)

**Status:** ✅ shipped
**Date:** 2026-08-12

## Problem

Owner report: the (re-run) 400 GB vault upgrade crashed on Windows with no
error message, roughly half to two-thirds through the repair phase. Windows
Event Viewer: exception `0xc0000374` (STATUS_HEAP_CORRUPTION) in `ntdll.dll`.

## Root cause (from crash-dump forensics)

The owner provided a full-memory minidump (629 MB). Without PDBs (Release
builds have `symbols "Off"`), analysis proceeded by scan-walking the crashing
thread (rust-minidump), extracting the in-memory PE image, attributing frame
addresses to exact function bounds via the `.pdata` RUNTIME_FUNCTION table,
and identifying functions by their RIP-relative string references:

- The crashing thread was a migration worker inside
  `media::VideoDecoder::decode_poster_rgb` (identified by its unique
  `"src_format"`/`"dst_format"` av_opt strings) with `sws_scale`
  (`libswscale/swscale.c` strings) on the stack, probing a video for its
  poster. The fault fired during the function's cleanup frees.
- Disassembly of a neighboring small frame identified FFmpeg's Windows
  `av_malloc`: it over-allocates, returns an aligned pointer, and stashes the
  real base pointer at `[aligned_ptr - 8]` — i.e. **in the inter-block gap
  directly after the previous allocation's end**. A write even a few bytes
  past one FFmpeg buffer therefore corrupts the *next* block's hidden base
  pointer; the later `av_free` hands the scribbled pointer to the CRT and
  `RtlFreeHeap` reports `0xc0000374`.

The overrunner: `decode_poster_rgb` allocated its `sws_scale` RGB24
destination with `av_image_get_buffer_size(..., align=1)` — an exactly-sized,
unaligned buffer. libswscale's vectorized RGB24 writers store whole SIMD
vectors per row, so the final row's store runs past a tight buffer's end.

**Empirical confirmation (canary sweep):** a standalone probe replicating the
old allocation pattern against the vendored libswscale measured overshoots in
**1179** format×flags×width combinations (`yuv420p`/`yuvj420p`/`yuv422p`,
including the exact `SWS_BILINEAR` flag the poster path uses), worst case
**42 bytes past the end of the buffer** — and up to 16 bytes past even a
64-aligned `linesize*h` allocation, proving linesize padding alone is not
sufficient (FFmpeg's own `av_frame_get_buffer` pads the allocation tail for
the same reason).

Why nothing caught it earlier:

- **Linux:** FFmpeg's `av_malloc` is `posix_memalign` there — no hidden base
  pointer; a ≤42-byte overrun lands in allocator slack. glibc never noticed.
  The same corruption very likely occurred silently during the Linux upgrade.
- **ASAN suite:** the store happens in uninstrumented vendored assembly; ASAN
  only checks instrumented load/store sites, so the overrun is invisible to
  it. (Valgrind would see it, but Arch's stripped `ld.so` currently blocks
  valgrind runs.)
- **Fixtures:** every existing video fixture had `w*3 % 32 == 0` (160→480,
  320→960, 64→192) — never in the overshooting class.

## What shipped

- **`decode_poster_rgb` allocates a padded destination** (`src/media/
  video_decoder.cpp`): `dst_linesize = FFALIGN(w*3, 64)` and the allocation is
  `linesize*h + 128` tail bytes (`POSTER_TAIL_PAD`), mirroring FFmpeg's own
  frame-buffer practice. Aligned rows absorb mid-row overshoot; the tail pad
  absorbs the final row's. The canary sweep re-run against this pattern shows
  **zero** escaping stores across the full parameter space. The tight
  `ImageData::pixels` buffer is filled by a stride-aware row copy (same
  pattern as the libheif plane copy).
- **Real frame geometry**: the conversion now uses `frame_->width/height`
  (the decoded frame) instead of the `open()`-time codecpar values, closing an
  OOB-read for containers whose headers lie about stream dimensions.
- **Same fix in the GIF playback decoder** (`src/media/gif_decoder.cpp`): the
  audit for other tight sws destinations found a second instance — animated
  GIF playback scaled into an exactly-sized `std::vector` (RGBA). The RGBA
  sweep measures up to **56 bytes** of overshoot there. It now scales into a
  padded scratch (aligned linesize + 128-byte tail) and row-copies into the
  tight `AnimFrame` buffer.
- **Odd-stride regression fixture**: `tiny_oddstride.mp4` (H.264, 106×64 —
  `w mod 16 = 10`, in the measured overshooting class) plus
  `probe_video_odd_stride_poster_stays_in_bounds`, so the poster path is
  exercised at a hostile width on every suite run (and trips valgrind /
  future instrumented runs if the class ever regresses).
- **Test-runner filter**: `osv_tests <substring>` runs only matching tests —
  added so valgrind/gdb sessions can target a single test instead of all
  2000+ (documented in README's Debugging section).

## Deliberately unchanged

- No FFmpeg patch: the overshoot is standard libswscale behavior; the
  documented contract is that destination buffers come padded (FFmpeg's own
  allocators do the same). Patching vendored FFmpeg would be lost on every
  bump.
- `FrameConverter::to_i420` (the remaining sws user) writes into an
  FFmpeg-allocated `AVFrame` buffer (`av_frame_get_buffer`), which is already
  padded — audited, no change needed. The zero-copy playback path does no
  conversion at all.
- The Windows vault from the crashed run needs no repair: the migration
  commits its index once at the end, so the crash left the index untouched;
  the half-appended thumbnails are orphaned waste reclaimed by the next
  successful upgrade's compaction.

2014 tests / 0 failed; ASAN clean.
