#pragma once

#include <cstdint>
#include <filesystem>

namespace platform {

// What happens to a cross-vault transfer's unlocked DESTINATION vault when the
// transfer finishes (Phase 66). Lives in platform:: (not ui::) because the
// persisted preference is a platform concern and ui already depends on
// platform — mirrors gfx::ThemeId living beside ThemePref's include.
enum class SecondVaultMode : uint8_t {
    LockNow,      // today's behavior: wipe the key when the dialog closes
    KeepTimed,    // keep unlocked for a sliding 5-minute window
    KeepSession,  // keep unlocked until an explicit lock / app exit
};

// A config-dir record of the DEFAULT SecondVaultMode offered in the transfer
// dialog (Phase 66). Stores NO secrets — one short slug, written atomically
// (temp file + rename) exactly like ThemePref. Missing/unknown → LockNow, so a
// corrupt file can never weaken security or break startup.
class SecondVaultPref {
public:
    SecondVaultPref() = default;                            // empty: no backing file
    explicit SecondVaultPref(std::filesystem::path file);

    [[nodiscard]] static SecondVaultPref default_location(); // config_dir()/"second_vault.conf"

    [[nodiscard]] SecondVaultMode load() const;             // missing/unknown → LockNow
    bool save(SecondVaultMode m) const;                     // persist; false on I/O failure

private:
    std::filesystem::path file_;
};

} // namespace platform
