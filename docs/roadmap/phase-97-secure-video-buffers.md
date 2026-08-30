# Phase 97 — Secure video/FFmpeg intermediates

**Status:** implementation complete; PR/CI/SonarCloud gates pending

**Audit mapping:** Phase D, OSV-AUD-003

**Format impact:** none (`INDEX_VERSION` remains 12)

## Goal

Complete the video/audio half of the decrypted-media remediation without wiping a shared live
FFmpeg buffer. Application-controlled plaintext is page-locked best-effort and wiped on release;
storage that third-party APIs do not let the application govern is reported honestly in F1.

## Ownership inventory

| Buffer | Allocator / owner | Plaintext | Caller storage? | Phase 97 release rule |
|---|---|---|---|---|
| `ChunkAvio` / `MemAvio` input | `av_malloc`, owned by `AVIOContext` wrapper | encoded media | initial buffer yes; FFmpeg may replace | lock initial; wipe/unlock actual final pointer; replacement marks opaque boundary |
| Demux packets | libavformat `AVBufferRef` | compressed media | no direct allocator hook | replace the packet's actual `AVBufferRef` with a shared secure wrapper; final clone release wipes/unlocks |
| Software decode frames | libavcodec frame pool | decoded video/audio planes | `get_buffer2` hook | `secure_get_buffer2` wraps every plane buffer; final shared release wipes/unlocks |
| Audio frames published by app | `crypto::SecureVector<float>` | interleaved F32 PCM | yes | allocator header records lock result; every deallocation wipes then unlocks |
| Filter output frames | libavfilter | decoded planes | allocated inside graph | wrap returned `AVFrame` buffers; filter graph scratch remains opaque and marks session degraded |
| Hardware surfaces | GPU/driver | decoded pixels | no | never treat device handles as CPU bytes; transfer result is wrapped; device storage is opaque/degraded |
| `sws_scale` poster/GIF scratch | `crypto::SecureBytes` | RGB/RGBA | yes | wipe/unlock on scope exit; 64-byte row alignment + 128-byte final tail retained |
| Conversion frame (`FrameConverter`) | FFmpeg frame allocation | I420 planes | allocated before scale | wrap after `av_frame_get_buffer`; final release wipes/unlocks |
| Published video frame | `crypto::SecureBytes` | I420/NV12 | yes | move-only cross-thread owner, wiped/unlocked on replacement/destruction |
| Animated GIF/WebP output | `crypto::SecureVector<uint8_t>` | RGBA | yes | secure allocator wipes/unlocks capacity on release |
| SDL audio-stream queue | SDL-owned | copied PCM | no complete allocator hook | lifetime minimized; session is already reported degraded |

## Design

`media::secure_packet_storage` and `secure_frame_storage` replace each exposed `AVBufferRef` with
another `AVBufferRef` whose callback owns the original reference. FFmpeg clone/ref operations then
share the wrapper itself. The callback can run only after the final wrapper reference disappears;
it wipes the original bytes, releases the page lock, and finally returns the allocation to FFmpeg.
If an unwrapped reference to the underlying allocation survives, the callback does not mutate live
storage and records the opaque/degraded boundary instead.

Every software codec context installs `secure_get_buffer2`. Outputs that bypass it — yadif filter
frames and hardware-to-software transfers — are wrapped immediately after allocation/transfer.
Hardware pixel formats are detected from the pixel-format descriptor and are never wiped as though
their `data[]` device handles were CPU planes. The yadif graph uses one filter thread so its worker
cannot remain invisible to the secure buffer's final-release synchronization; codec decode remains
independently threaded.

`crypto::SecureAllocator<T>` supplies vector ergonomics for PCM and RGBA while preserving the same
page registry, warn-once behavior, `MADV_DONTDUMP`, wipe observation, and wipe-before-unlock order as
`SecureBytes`. Its allocation header records the lock result so a failed lock cannot cause a later
unconditional unlock of a neighboring secret's shared page.

## Enforceable boundary

FFmpeg codec/filter scratch, hardware surfaces, libwebp/libheif internal canvases, and SDL audio
queue storage do not expose sufficient caller allocation/final-release control. They remain tightly
scoped third-party allocations. `crypto::opaque_plaintext_seen()` records entry into this boundary,
and `secure_memory_degraded()` combines it with actual mlock failures for the F1 status line. The
wording is deliberately “may be swappable”; it no longer attributes every degraded state to an
exhausted mlock budget.

## Tests and verification

- Packet and frame clone tests prove no wipe occurs while another shared wrapper is live and that
  the final release is zeroed.
- AVIO tests prove both wrappers lock and wipe their buffers.
- PCM, GIF RGBA, poster/GIF scratch, and cross-thread frame-copy tests cover secure owners.
- Existing seek/flush/abort/hardware-fallback tests exercise packet/frame teardown under ASAN/TSan.
- FFmpeg-enabled suite: **2237 tests, 0 failed**.
- No-FFmpeg parity: **2051 tests, 0 failed**.
- Phase 80 padded canary sweep: **0 overshooting cases, worst 0 bytes**.
- ASAN/UBSan: **2237 tests, 0 failed; no sanitizer findings**.
- TSan: **2237 tests, 0 failed; no race reports**.
- Release: **2237 tests, 0 failed**.
- `serena memories check`: **no referential-integrity issues**.
- Targeted Valgrind could not start on the development host because its stripped `ld-linux` lacks
  the required `memcmp` redirection symbol; ASAN and final-release wipe-observation tests cover the
  path. Linux/Windows CI and SonarCloud remain PR delivery gates.

## Acceptance

- Every application-controlled plaintext FFmpeg buffer has a wiping final owner.
- Shared packet/frame storage is never wiped at an intermediate unref.
- Software frames are locked where FFmpeg supports the hook; mapped CPU outputs are wrapped.
- Opaque storage is documented and visible at runtime.
- The padded swscale contract and no-FFmpeg build remain intact.
