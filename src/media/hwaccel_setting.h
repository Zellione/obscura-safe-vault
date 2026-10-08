#pragma once

namespace media {

// Runtime hardware gate plus a retained internal/test software override.
// The UI exposes one Hardware (with fallback) / Software choice through F2
// and Ctrl+Shift+H. App startup maps legacy HwAccelPref bits to that choice;
// every user selection clears force_software_decode. Applies on next clip.
//
// UI-thread only (like the active-theme global in gfx/theme.cpp and the
// volume / loop / autoplay settings in this directory); no
// synchronisation needed.
//
// NOT gated on OSV_VENDORED_AV / OSV_HWACCEL_VAAPI — the runtime values
// exist on every build, so a saved pref from one build can load on
// another. try_attach_hwaccel() returns false unconditionally on a build
// without hwaccel compiled in, so the toggle has no observable effect
// there.
[[nodiscard]] bool enable_hardware_decode() noexcept;
void set_enable_hardware_decode(bool enabled) noexcept;

[[nodiscard]] bool force_software_decode() noexcept;
void set_force_software_decode(bool enabled) noexcept;

} // namespace media
