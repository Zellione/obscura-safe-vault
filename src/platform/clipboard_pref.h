#pragma once

#include <cstdint>
#include <filesystem>

namespace platform {

// What the app may do with the OS clipboard (Phase 92). Lives in platform::
// because the persisted preference is a platform concern and ui already depends
// on platform — mirrors SecondVaultMode living beside SecondVaultPref's include.
//
// The OS clipboard is a cross-process, persistent plaintext sink outside the
// app's mlock/wipe control, so the thumbnails/decoded-pixels hardening (Phases
// 88–91) cannot reach it. This gate decides whether the app writes plaintext
// (passwords, node names, tags, search text) to it at all.
enum class ClipboardMode : uint8_t {
    Allow,    // write immediately (the pre-phase-92 behaviour)
    Warn,     // ask first (default-cancel confirm), then write
    Disable,  // refuse: copies are no-ops
};

// A config-dir record of the clipboard gate (Phase 92). Stores NO secrets — one
// short slug, written atomically (temp file + rename) exactly like
// SecondVaultPref. Missing/unknown → Allow, so a corrupt file can never lock
// out a legitimate copy or break startup.
class ClipboardPref {
public:
    ClipboardPref() = default;  // empty: no backing file
    explicit ClipboardPref(std::filesystem::path file);

    [[nodiscard]] static ClipboardPref default_location();  // config_dir()/"clipboard.conf"

    [[nodiscard]] ClipboardMode load() const;  // missing/unknown → Allow
    bool save(ClipboardMode m) const;          // persist; false on I/O failure

    [[nodiscard]] const std::filesystem::path& file() const noexcept
    {
        return file_;
    }

private:
    std::filesystem::path file_;
};

}  // namespace platform