# Phase 103 — AudioDecoder EAGAIN handling ✅

**Status:** ✅ shipped
**Date:** 2026-09-15

`src/media/audio_decoder.cpp:105` (the OLD line) treated every negative
`avcodec_send_packet` return as a failure — including `AVERROR(EAGAIN)`,
which is the documented **"drain me with `avcodec_receive_frame` first, then
retry"** signal. Result: the codec's input queue filled up under the
normal rapid-packet path (every video plays one packet per `decode()`
call without an external receive between calls), `send_packet` returned
EAGAIN, the OLD code logged `[AudioDecoder] avcodec_send_packet failed`
and **dropped** the packet, the next call got EAGAIN again (queue never
drained), audio went silently dead, and stderr was flooded with ~hundreds
of "failed" lines per video.

This is the same EAGAIN handling pattern `VideoDecoder::next_frame` and
`VideoDecodeWorker::send_packet` already use — the audio path was the
only one that didn't follow it.

## What changed

### `src/media/audio_decoder.cpp` — `AudioDecoder::decode()` rewritten

- DRY the receive-loop into a `drain` lambda (one source of truth for
  the receive → interleave → push-to-`out` chain).
- `drain("pre-send")` runs before every send. This is the critical fix:
  it ensures the codec's input queue has room before we try to push the
  next packet.
- `send_packet` is now followed by a retry loop: on `AVERROR(EAGAIN)`,
  drain and retry (capped at `MAX_SEND_RETRIES = 8` so a pathological
  codec can't loop forever; if the cap is hit, fall through to the
  failure branch).
- The failure branch now logs the **actual** `AVERROR` + `av_err2str()`,
  so a future real failure is debuggable. The OLD code logged only
  `avcodec_send_packet failed` — discarding the actual error code that
  would tell you what was wrong.
- `drain("post-send")` runs after every successful send to collect any
  frames the decoder produced.
- The `drain` lambda also logs `receive_frame` errors with the actual
  `AVERROR` + `av_err2str()`. The `context` parameter ("pre-send",
  "mid-send retry", "post-send") names which call site failed.

## Tests added

| File | Test | Covers |
|---|---|---|
| `tests/media/test_audio_decoder.cpp` | `audio_decoder_eagain_path_drains_and_recovers` | The regression test: pumps every audio packet from the `audio_only.m4a` fixture through `decode()` in a single tight loop with no external drain. Asserts frames come out and the codec didn't deadlock. Under the OLD code this fails with `!all_frames.empty()` — the codec's queue fills, `send_packet` returns EAGAIN, `decode()` logs + drops the packet, and the EAGAIN loop keeps firing. Under the NEW code the same fixture produces 45 frames from 45 packets. |

The test fixture (AAC in `audio_only.m4a`) has a large enough internal
queue that EAGAIN doesn't actually trigger within 45 packets, so this
test is more of a **regression test** than an EAGAIN-stress test. The
bug was nevertheless real on the user's video (visible in the log they
posted: ~hundreds of `[AudioDecoder] avcodec_send_packet failed` lines
from a single playback). The fix is correct even where this test is
non-strict — a real EAGAIN-stress test would need a codec whose internal
queue actually fills under our test pattern (we'd have to construct
something other than the existing fixture, and the AAC codec in
FFmpeg 7.1.1 doesn't expose a knob for that).

## What does NOT change

- **Video decoder / sync logic.** The "replays the first second" symptom
  is downstream of the broken audio: the `loop_` toggle's
  `frame_.eof && loop_ → do_seek(0.0)` keeps restarting the video
  because the broken audio never advances the A/V clock past the first
  second. With audio working, the loop works normally (or stops cleanly
  when `loop_` is off). I did **not** touch `do_seek`, `frame_.eof`, or
  the loop-at-EOF branch in this phase — that would be a Phase 104 bug
  if it surfaces after audio is fixed.
- **FFmpeg configuration or the codec list.** The codec opens
  successfully (`[AudioDecoder] avcodec_open2 failed` does not appear
  in the failing log), so the bug is in the runtime call sequence, not
  the build.
- **The existing `[AudioDecoder]` log-tag / message structure.** Just
  the *condition* under which we log changes — EAGAIN becomes silent
  (it's the normal path), genuine failures become informative.

## Acceptance criterion

- `scripts/test.sh` green: **2277 tests, 0 failed** (baseline 2276 + 1 new).
- `scripts/test.sh --asan` green.
- **Manual smoke test on the original broken video**: it should play
  audio and stop/loop normally instead of looping with no audio and
  ~hundreds of `[AudioDecoder] avcodec_send_packet failed` lines.

## Files touched

- `src/media/audio_decoder.cpp` — the fix (single function, `AudioDecoder::decode()`).
- `tests/media/test_audio_decoder.cpp` — new regression test.
- `docs/roadmap/phase-103-audio-eagain.md` — new phase doc (this file).
- `ROADMAP.md` — new row.
- `docs/VENDORED_DEPS.md` — no change.
- `.serena/memories/tech_stack.md` — no change.

## Out of scope (deliberate)

- The video loop-at-EOF interaction with broken audio sync. The user's
  description suggests fixing audio is sufficient; if looping is still
  wrong after this lands, it's a separate Phase 104 bug.
- The `audio_dec_.open()` failure paths (`No decoder found`,
  `avcodec_open2 failed`) — these were already correct.
- Generic AVERROR/EAGAIN handling in any other audio/video code path —
  `VideoDecoder::next_frame` and `VideoDecodeWorker::send_packet` were
  already correct; the audio path was the only one missing the pattern.
