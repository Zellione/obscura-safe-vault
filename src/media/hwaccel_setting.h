#pragma once

namespace media {

// Phase 102: the two runtime user-toggled overrides that gate
// media::try_attach_hwaccel() (declared in src/media/hw_accel.h).
//
// `enable_hardware_decode`  default true; Ctrl+Shift+H toggles live.
// `force_software_decode`   default false; Ctrl+Shift+F toggles live
//                            and overrides enable_hardware_decode.
//
// Both are seeded from platform::HwAccelPref::default_location() at
// App::init and persisted on every toggle (live-save, no exit-save needed;
// the F2 settings overlay's value-cycling path is the only writer).
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
