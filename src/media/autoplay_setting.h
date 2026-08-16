#pragma once

namespace media {

// Process-global auto-play-videos toggle (Phase 85). Seeded from
// platform::AutoplayPref at App::init and written back by the F2 settings
// toggle (saved live — no exit-save needed; settings is the only writer).
// UI-thread only (like the active-theme global); no synchronisation needed.
// NOT gated on OSV_VENDORED_AV — the setting exists on every build.
[[nodiscard]] bool saved_autoplay_enabled() noexcept;   // default true until seeded
void set_saved_autoplay_enabled(bool enabled) noexcept;

} // namespace media
