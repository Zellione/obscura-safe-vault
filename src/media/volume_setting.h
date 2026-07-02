#pragma once

namespace media {

// Session-wide remembered media playback volume in [0,1] (Phase 25 follow-up).
//
// Held as a process global so it carries across videos with no disk round-trip:
// every VideoPlayback seeds its level from saved_volume() on open and writes back
// via set_saved_volume() whenever the user changes it (keys or the drag bar). App
// seeds this from platform::VolumePref at startup and persists it on exit — so the
// configured volume is remembered both across clips and across app restarts.
//
// UI-thread only (like the active-theme global); no synchronisation needed.
[[nodiscard]] float saved_volume() noexcept;    // default 1.0 until seeded
void set_saved_volume(float v) noexcept;        // clamped to [0,1]

} // namespace media
