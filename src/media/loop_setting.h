#pragma once

namespace media {

// Session-wide remembered video loop toggle (Phase 40 Part 1).
//
// Held as a process global so it carries across videos with no disk round-trip:
// every VideoPlayback seeds its flag from saved_loop_enabled() on open and writes
// back via set_saved_loop_enabled() whenever the user toggles it (the R key).
// Process-lifetime only — unlike media::volume_setting, nothing seeds this from a
// persisted preference at startup and nothing writes it back on exit.
//
// UI-thread only (like the active-theme global); no synchronisation needed.
[[nodiscard]] bool saved_loop_enabled() noexcept;   // default false until toggled
void set_saved_loop_enabled(bool enabled) noexcept;

} // namespace media
