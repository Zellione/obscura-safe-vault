#pragma once

#include <filesystem>

namespace platform {

// A config-dir record of the user's chosen media playback volume (Phase 25
// follow-up). Stores NO secrets — just one float in [0,1], written atomically
// (temp file + rename) exactly like ThemePref/VaultRegistry. A missing, empty, or
// unparseable value loads as 1.0 (full), so a corrupt or hand-edited file never
// breaks startup.
//
// Mirrors ThemePref: the only state is the backing-file path; load()/save()
// read/write that file rather than any in-memory value, so save() is const.
class VolumePref {
public:
    VolumePref() = default;                                 // empty: no backing file
    explicit VolumePref(std::filesystem::path file);

    [[nodiscard]] static VolumePref default_location();     // config_dir()/"volume.conf"

    [[nodiscard]] float load() const;                       // missing/invalid → 1.0; clamped [0,1]
    bool save(float volume) const;                          // persist (clamped); false on I/O failure

    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

private:
    std::filesystem::path file_;
};

} // namespace platform
