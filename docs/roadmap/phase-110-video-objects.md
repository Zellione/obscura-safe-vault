# Random-access encrypted video objects (Phase 110)

**Status:** not started

## Goal

Store each imported video as one independently encrypted object file while retaining efficient authenticated random access for FFmpeg and bounded secure memory.

## Step-by-step work

1. Choose and freeze a fixed plaintext frame size (target approximately 1 MiB, benchmarked) and an authenticated seek table mapping logical plaintext ranges to ciphertext records.
2. Bind every frame to object/vault/node identity, video role, zero-based frame sequence, total frame count, and plaintext length. Reordering, duplication, omission, or cross-video copying must fail.
3. Stream imports directly from secure plaintext buffers into frames; never assemble the whole video in ordinary memory or write plaintext staging files.
4. Implement `VideoObjectSource` with `read_at`/seek, overflow-safe range math, a small wipe-on-evict decrypted-frame cache, cancellation, and thread-safe lifetime rules.
5. Adapt the custom `AVIOContext` to the new source while retaining the legacy `ChunkStore` source behind a format-neutral interface.
6. Preserve secure FFmpeg buffer ownership, Phase 80 swscale padding, hardware/software decode fallback, poster generation, and degraded-memory reporting.
7. Benchmark sequential playback, random seeks, thumbnail/poster extraction, and large-file import against the legacy implementation; tune only format parameters that remain forward-compatible.
8. Add corruption diagnostics that identify object/frame numbers only and never database names or decrypted bytes.

## TDD and security tests

- Boundary reads across one/many frames, final short frame, EOF, backward seeks, and concurrent cancellation.
- Frame swap/replay/truncation/table corruption/wrong object identity all fail authentication before bytes reach FFmpeg.
- Existing media fixtures produce equivalent stream bytes, duration, frames, audio, seek behavior, and fallback behavior through both backends.
- Multi-gigabyte synthetic source verifies bounded memory and checked 64-bit arithmetic.

## Acceptance criterion

All supported video/audio playback and seek behavior works from a single independently encrypted v3 object per original video, with no plaintext disk writes, authenticated random access, bounded memory, and performance within the owner-approved budget.
