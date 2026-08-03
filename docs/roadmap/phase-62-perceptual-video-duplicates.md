# Phase 62 — Perceptual video duplicates

Status: **done** — design decisions made interactively with the owner
(2026-08-03); implemented as planned in
`docs/superpowers/plans/2026-08-03-perceptual-video-duplicates.md` (local
working doc).

## Problem

The Phase 61 duplicate finder detects videos only when they are byte-identical:
the exact pass buckets by (type, size) and BLAKE2b-hashes the full plaintext,
and the perceptual dHash pass explicitly excludes videos. The same clip
re-encoded, resized, or remuxed into a different container — the common way
video duplicates actually arise — is invisible to it.

**Goal:** the existing "Exact + visually similar" scan mode also groups
visually-identical videos, with no new UI mode and no `.osv` format change.

## Design (owner-approved)

A three-stage funnel, cheapest evidence first:

1. **Duration gate — zero I/O.** `vmeta.duration_us` is already in the index.
   Only videos within 2 % of each other's duration (500 ms absolute floor for
   short clips) can pair. This kills almost all of the O(n²) pair space before
   any byte is decrypted.
2. **Poster prefilter.** Each surviving video's stored first-frame JPEG poster
   is decrypted (`read_thumb_span`) and decoded exactly like an image
   thumbnail, then dHashed. Pairs must be within Hamming ≤ 10 (looser than the
   image threshold 5 — posters re-encode) or lack a poster on either side.
3. **Frame confirm.** For videos still in play, decode one frame at 10/30/50/
   70/90 % of the timeline (software decode only, no hwaccel) and dHash each.
   A pair matches when ≥ 4 of the 5 per-position hashes are within Hamming ≤ 7.
   Union-find clusters matching pairs into groups tagged **"Similar video"**;
   review/marking/apply work unchanged (members are ordinary `DupMember`s).

**Threading (the one hard constraint):** the scan worker may only read the
vault through the any-thread `vault::read_thumb_span`. `media::VideoSource`
borrows the main-thread `read_fp_` handle and is therefore off-limits — frame
decode gets its own AVIO over a chunk-caching `read_thumb_span` byte stream
(the `VideoSource::fill_one` logic re-hosted on the thread-safe handle).

**Non-FFmpeg builds:** stage 3 compiles to a stub; a duration-close,
poster-close pair is accepted on that evidence alone.

**Signals deliberately NOT used:** audio fingerprinting, per-frame motion
statistics, cross-duration matching (a trimmed clip is a different video for
this feature's purposes).

## Implementation outline

| Piece | Where |
|---|---|
| `VideoSig` (poster hash + 5 frame hashes + validity), `duration_close`, `video_sig_match` (frame evidence wins; poster fallback), `cluster_video_sigs`, `DupGroup::Kind::SimilarVideo` | `ui/dup_model.*` (pure, unit-tested) |
| `compute_video_frame_sig` — seek/decode/dHash over a callback byte stream, `OSV_VENDORED_AV`-guarded with a stub fallback | new `ui/dup_video_sig.*` |
| Snapshot gains `duration_us` + `chunk_size`; worker gains the three-stage video pass after the image perceptual pass | `ui/dup_scan.*` |
| "Similar video" group header | `ui/duplicates_screen.cpp` |
| Same-content-two-codecs fixtures (`dup_a_h264.mp4` / `dup_a_vp9.webm` / `dup_other.mp4`) | `scripts/gen_media_fixtures.sh` |

## Implementation notes (found while building)

- **Decode-loop EOF bug:** after the demux-EOF flush, `av_packet_unref` resets
  `stream_index` to 0 — which is usually the video stream — so the stale
  packet must never be sent after flushing; doing so errored out late timeline
  positions (90 %) on single-keyframe MP4s. `decode_until` flushes exactly
  once and then only drains.
- **Fixture determinism:** lavfi `gradients` randomizes its colors per
  invocation, so two "same content" encodings genuinely differed; the fixtures
  use `testsrc2` (deterministic). Cross-codec per-position dHash distances on
  the final fixtures: 0–1 bits.
- The frame signatures for candidates stay index-aligned with the candidate
  list by construction (parallel vectors pushed in lockstep) — the same
  invariant whose violation caused the Phase 61 image-pass mapping bug.

## Tests

`test_dup_model.cpp` (duration gate boundaries, frame/poster match semantics
incl. the non-FFmpeg poster fallback, signature clustering + duration veto),
`test_dup_video_sig.cpp` (cross-codec fixture match, different-content
non-match, garbage-input failure; mem-buffer reader), `test_dup_scan.cpp`
(vault-level: re-encoded pair groups as SimilarVideo while the equal-duration
different clip stays out; exact-only scans decode no frames). End-to-end
headless: scan → "Similar video" group → keep-only → apply → rescan clean.

1774 tests / 0 failed; ASAN clean; TSAN clean in project code.

## Acceptance criteria

- The same clip vaulted as H.264/MP4 and VP9/WebM lands in one "Similar
  video" group; an equal-duration different clip does not (unit + integration
  + headless end-to-end against a planted vault).
- Exact video groups are unchanged; images are unchanged; scans without the
  perceptual option decode no video frames.
- Cancellation between frames; manual lock mid-scan ends the worker before key
  wipe; a video that fails to open/seek is skipped per-position, never aborts.
- All six security invariants hold (frames/chunks in wiped buffers; signatures
  session-lifetime only). `scripts/test.sh`, `--asan`, `--tsan` green.
- Phase 61's YAGNI list ("Perceptual matching for videos") is updated to point
  here.
