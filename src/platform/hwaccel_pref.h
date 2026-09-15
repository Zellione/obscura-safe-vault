#pragma once

#include <filesystem>

namespace platform {

// Phase 102: a config-dir record of the two hwaccel runtime overrides
// (src/media/hwaccel_setting.h): enable_hardware_decode +
// force_software_decode. Stored together in one file ("hwaccel.conf") so
// the pref lives at one path and is written atomically (temp file + rename,
// like ThemePref / VolumePref / AutoplayPref). Missing / empty /
// unparseable → both defaults (hardware on, software-forced off); a
// corrupt file never breaks startup.
//
// File format (one per line, ASCII):
//   hw=on            or  hw=off
//   swforce=on       or  swforce=off
// Unknown keys / values are ignored; the lines that parse still apply.
class HwAccelPref {
public:
    HwAccelPref() = default;                                  // empty: no backing file
    explicit HwAccelPref(std::filesystem::path file);

    [[nodiscard]] static HwAccelPref default_location();      // config_dir()/"hwaccel.conf"

    struct State {
        bool enable_hardware = true;     // default true
        bool force_software  = false;    // default false
    };

    [[nodiscard]] State load() const;                        // missing/invalid → defaults
    bool save(State s) const;                                // persist; false on I/O failure

    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

private:
    std::filesystem::path file_;
};

} // namespace platform
