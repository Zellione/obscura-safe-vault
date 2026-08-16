#pragma once

#include <filesystem>

namespace platform {

// A config-dir record of the auto-play-videos toggle (Phase 85). Stores NO
// secrets — one "on"/"off" token, written atomically (temp file + rename)
// exactly like ThemePref/VolumePref. Missing, empty, or unparseable → true
// (auto-play ON is the shipped default), so a corrupt file never breaks startup.
//
// Mirrors VolumePref: the only state is the backing-file path; load()/save()
// read/write that file rather than any in-memory value, so save() is const.
class AutoplayPref {
public:
    AutoplayPref() = default;                                 // empty: no backing file
    explicit AutoplayPref(std::filesystem::path file);

    [[nodiscard]] static AutoplayPref default_location();     // config_dir()/"autoplay.conf"

    [[nodiscard]] bool load() const;                          // missing/invalid → true
    bool save(bool enabled) const;                            // persist; false on I/O failure

    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

private:
    std::filesystem::path file_;
};

} // namespace platform
