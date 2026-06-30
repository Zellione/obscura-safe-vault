#pragma once

#include <filesystem>

#include "gfx/theme.h"

namespace platform {

// A config-dir record of the chosen UI theme (Phase 23). Stores NO secrets —
// only the theme's stable slug (one short token), written atomically (temp file
// + rename) just like vault_registry. An unknown or absent value loads as the
// default theme, so a corrupt or hand-edited file never breaks startup.
//
// Like VaultRegistry, the only state is the backing-file path; load()/save()
// read/write that file rather than any in-memory selection, so save() is const.
class ThemePref {
public:
    ThemePref() = default;                                 // empty: no backing file
    explicit ThemePref(std::filesystem::path file);

    [[nodiscard]] static ThemePref default_location();     // config_dir()/"theme.conf"

    [[nodiscard]] gfx::ThemeId load() const;               // missing/unknown → default
    bool save(gfx::ThemeId id) const;                      // persist; false on I/O failure

    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

private:
    std::filesystem::path file_;
};

} // namespace platform
