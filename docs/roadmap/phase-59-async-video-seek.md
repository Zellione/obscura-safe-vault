# Phase 59 — Non-blocking video seek

Full design rationale: `docs/superpowers/specs/2026-08-02-phase59-async-video-seek-design.md`

## Problem

Every seek entry point — `J`/`L` (±5 s), `,` (frame-step back), seek-bar click
and drag, loop-at-EOF, session resume — funnels into
`VideoPlayback::Impl::do_seek()`, which ran on the render thread and called the
*blocking* `decode_into_pending()`. That call loops until the
`VideoDecodeWorker` has decoded forward from the seek's anchor keyframe all the
way to the target pts. The worker silently discards every below-target frame
without publishing a result, so with a long GOP or a slow software codec (AV1,
HEVC without hwaccel) the render thread sat inside the seek for seconds: no SDL
events pumped, no frames presented, compositor marking the window unresponsive.
Scrubbing was the worst case — one full blocking decode-forward per mouse-motion
event.

## What changed (all inside `src/ui/video_playback.cpp`)

- **`do_seek()` returns immediately.** It still does the demux-side reseek
  (`seek_demux_only`), bumps `generation_`, arms the worker's decode-forward
  target (`begin_seek`), re-bases audio, and resets frame state — but instead
  of blocking it jumps the transport to the clamped target (`model_.seek_to`)
  and returns. The previously shown frame stays on screen until the first
  at-or-after-target frame arrives.
- **`skip_pending_` (already "a seek's decode-forward is in progress") now
  drives resolution:**
  - `animating()` returns true while it is set, so `App::run`'s poll gate keeps
    render ticks coming even when the clip is paused — the pending seek can
    actually finish.
  - `try_advance_pending()` tops the worker queue up to `SEEK_FEED_DEPTH` (32)
    instead of `PREFETCH_DEPTH` (2) while it is set, and its feed-on-timeout
    branch ignores `MAX_STEADY_IN_FLIGHT` while it is set — the same
    "one-time, GOP-bounded gap" reasoning the old blocking loop used.
  - `consume_result()` realigns the transport to the decoded frame's actual
    pts when it resolves a pending seek, preserving the old synchronous
    behavior for paused frame-stepping. An EOF result (seek at/past the last
    frame) leaves the position at the clamped target, as before.
- **The constructor's frame 0 keeps the blocking `decode_into_pending()`** —
  opening starts at pts 0, a keyframe, so there is no decode-forward gap.

Scrubbing supersedes naturally: each drag event's generation bump +
`begin_seek` queue-drop discards the previous target's work, and stale results
are already filtered by generation. No new threads, no new shared state, no
vault-format or index change.

## Tests

New regression test
(`video_playback_seek_does_not_block_render_thread_during_slow_decode`,
`tests/ui/test_video_playback.cpp`) drives a 300 ms/packet artificial decode
delay and asserts the async-seek contract: `seek()` returns in well under one
packet-decode time, `animating()` is true while the seek resolves on a paused
clip, and pumping ticks resolves it with the transport realigned near the
target. `video_playback_seek_moves_position_and_stays_paused` was updated to
pump until resolution instead of expecting the seek to finish inside a single
call.

1714 tests / 0 failed; ASAN clean.
