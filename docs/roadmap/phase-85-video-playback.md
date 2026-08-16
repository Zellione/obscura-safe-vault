# Phase 85 — MPEG-PS playback, A/V seek sync, auto-play

## Overview

Three independent fixes enabling raw `.mpg` (MPEG-PS) playback, eliminating audio/video desynchronization after seeking, and adding a persisted auto-play-videos setting (default ON).

---

## 85.1 — Raw MPEG-PS (.mpg) file support

### Root cause: Missing `mpegvideo` probe demuxer

FFmpeg's `mpegps` demuxer defers video codec identification to **codec probing** via a `request_probe` callback chain (vendor/ffmpeg/libavformat/mpeg.c:576-593). This probe needs the raw `mpegvideo` probe demuxer (`vendor/ffmpeg/libavformat/demux.c:123`, which maps a probe hit → `AV_CODEC_ID_MPEG2VIDEO`). Our decode-only build had `CONFIG_MPEGVIDEO_DEMUXER 0`, leaving the probe unavailable. The app correctly handled the probe failure by tagging the file as codec `Unknown` (a safe fallback), but users expecting to play a `.mpg` file saw a "Cannot decode" error instead.

MPEG-1 and MPEG-2 in Program Stream (PS) containers are legitimate media formats, historically common in consumer electronics and certain streaming contexts. The Phase 65 migration job (`src/ui/migration_job.cpp:92`) can re-probe `codec == Unknown` nodes if the capability generation advances.

### Solution: Enable the demuxer and bump capability generation

1. **Build scripts:** Both `scripts/build_codecs.sh:214` and `scripts/build_ffmpeg_windows.sh:70` now list `mpegvideo` in the `--enable-demuxer` arguments alongside the existing `mpegps`, `mpegts` demuxers.

2. **Capability generation bump:** `src/media/video_probe.h:26` sets `PROBE_CAPS_GEN = 2` (was 1). This token is consumed by `vault::migration_pending()` — stamped vaults now re-offer the Phase 65 unlock-time migration, which re-probes and heals existing Unknown-codec `.mpg` nodes without new code in this phase.

3. **Empirical probe refinement:** FFmpeg's raw ES probe for MPEG-1-in-PS refines to `MPEG2VIDEO` via stream-info parsing (both MPEG-1 and MPEG-2 decoders accept the same codec ID; demux.c's probe maps raw-es hits uniformly). Verified against synthetic fixtures.

### Test coverage

New `tests/media/test_video_probe.cpp` fixtures decode `.mpg` files (MPEG-1 and MPEG-2 in Program Stream format, synthetic 96×64 test patterns with audio, uuencoded under ~100 KB each) and confirm probing returns a real codec (`VideoContainer::MPEGPS`, `VideoCodec::MPEG2` or `MPEG1`), not Unknown.

---

## 85.2 — Audio/video synchronization on seek

### Root cause: Audio frames played pre-target; clock based on requested target, not actual audio

When seeking to a target time T, the video decode worker skips decoded frames below T (correct behaviour). However, the audio decoder has **no skip logic** — it plays every audio frame from the keyframe the demuxer landed on forward. If the demuxer's keyframe was before T, the user audibly hears audio content from before the target — a visible/audible desync.

Additionally, `do_seek()` set `audio_.seek_base = tt` (the **requested** target), but when the demuxer lands at an earlier keyframe, the clock's real basis is inconsistent with what the audio content actually contains.

### Solution: Drop pre-target audio frames and re-base the clock

1. **New `media::AudioSeekSkip` helper** (`src/media/av_sync.{h,cpp}`): A pure decision function that answers, for each decoded audio frame: drop it (ends at or before the seek target — nothing audible at/after the target), or start feeding and re-base the clock on this frame's actual `pts_seconds`. Degenerate input (no samples / bad sample rate) fails open to `Start` so audio can never be dropped forever.

2. **Skip logic in `VideoPlayback::pump_audio()`** (`src/ui/video_playback.cpp`): A temporary `skip_target >= 0.0` state field triggers the decision per frame during seek resolution. Frames marked `Drop` are **never fed** to SDL (no skipped-audio glitch) and never counted in `samples_fed`. When the first frame passes the decision (`Start`), `seek_base` is rewritten to that frame's **actual** `pts_seconds` (not the requested target), and `skip_target` is cleared for normal playback.

3. **Ordering guarantee:** The resolution happens within the viewer's playback build frame, before any update tick, so no pre-seek audio or video is ever presented to the user. Combined with the existing `apply_video_resume` seek (which preserves play/pause state), auto-play (Task 6) starts transport after the seek is resolved, yielding the correct composition: seek + skip-resolve + auto-start, all synchronous, visible as smooth playback from the target.

### Hardening: `dup_video_sig` sws_scale buffer overrun (Phase 80 class)

An incidental hardening fix surfaced during ASAN-instrumented codec rebuild: `src/ui/dup_video_sig.cpp` passed 1-element plane arrays to `sws_scale` (which internally reads all 4 planes) and a tight-stride destination buffer. This is the Phase 80 overrun pattern — libswscale stores whole SIMD vectors and can write up to ~56 bytes past a non-padded buffer. Fixed with 4-element plane arrays + a padded destination stride + a row copy into the tight result, consistent with Phase 80's `decode_poster_rgb` fix.

---

## 85.3 — Auto-play videos on open (persisted setting, default ON)

### Feature design

The gallery now plays videos automatically when opened (a behavior change from "paused by default"), governed by a persisted per-machine setting.

**Persistence layer:**
- New `platform::AutoplayPref` (src/platform/autoplay_pref.{h,cpp}) — mirrors the `ThemePref` / `VolumePref` pattern; stores `"on"` / `"off"` atomically (temp file + rename); missing/invalid file → `true` (default ON).
- File: `autoplay.conf` in the app's config directory.

**Process-global setting:**
- New `media::autoplay_setting` (src/media/autoplay_setting.{h,cpp}) — `saved_autoplay_enabled()` / `set_saved_autoplay_enabled(bool)`, default `true`.
- UI-thread only (like the active theme global); no synchronisation needed.
- Seeded from `platform::AutoplayPref` at App startup (line ~119 in src/app/app.cpp, next to the VolumePref seed).
- Saved live on F2 settings toggle; no separate exit-save needed.

**UI signals:**
- **F2 settings** gain a **Playback** section (SettingsSection now has 6 rows: Appearance, Playback, Browsing, TagColours, VaultOps, Security). The Playback section has one row: `"Auto-play videos"` toggle showing `"On"` / `"Off"`. Left/Right cycles the value; the setting is saved immediately.
- `VideoPlayback::set_paused(bool)` — a new public method that routes through the same pause/resume path as the Space key (no key synthesis). Used at video open to start transport if auto-play is enabled.
- The viewer's playback-build path (`image_viewer.cpp`, ~line 277 region) calls `set_paused(false)` after construction if `media::saved_autoplay_enabled()` is true.

**Behavior change:** Videos now start playing immediately on open by default. Users can disable this in F2 Playback settings, and the choice persists across restarts.

---

## Spec deltas (rulings from investigation)

1. **MPEG-PS probe:** FFmpeg's `mpegps` defers codec ID to probe; the `mpegvideo` demuxer was genuinely missing from the build, not an app-side probe bug.

2. **Healing existing `.mpg` nodes:** The spec's mention of a new `ui/video_repair.*` module is stale — the healer is the existing Phase 65 migration job, re-triggered by the `PROBE_CAPS_GEN` bump.

3. **Audio skip + clock re-base:** The spec's mention of a pure clock re-base was incomplete. The full fix requires skipping (dropping frames audibly ending pre-target, never feeding them to SDL) **plus** re-basing the clock to the first **kept** frame's actual pts, not the video frame's pts or the requested target.

---

## Summary

| item | what changed |
|---|---|
| codec support | raw MPEG-PS (.mpg) playable via FFmpeg `mpegvideo` probe demuxer + `PROBE_CAPS_GEN` bump |
| healing | Phase 65 migration re-offered at unlock; existing Unknown-codec `.mpg` nodes re-probed and healed |
| seek fix | audio frames dropped until target reached; clock re-based to first kept frame's actual pts |
| hardening | `dup_video_sig` sws_scale overrun fixed (4-element planes + padded stride + row copy) |
| auto-play | persisted per-machine toggle (default ON); videos start playing on open; F2 Playback section with one row |
| settings | `SETTINGS_SECTION_COUNT` 5 → 6 (Playback inserted after Appearance) |
| tests | 2098 tests / 0 failed; ASAN clean |

No `.osv` format change; `INDEX_VERSION` stays 12.
