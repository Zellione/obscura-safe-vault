#pragma once

// Atomic config-file write shared by every platform pref (Phase 92 cleanup).
// ThemePref, VolumePref, GalleryViewPref, AutoplayPref, SecondVaultPref,
// ClipboardPref and VaultRegistry each used to hand-roll this identical
// temp+rename sequence; a new pref (clipboard) copying it again tipped
// SonarCloud's new-code duplication gate over its 3 % threshold, so the one
// implementation lives here and the call sites are thin. Header-only inline so
// no premake registration is needed, mirroring path_utf8.h / safe_print.h.
//
// c:S2083 is suppressed below: the rule taints every free-function path
// parameter as attacker-controlled, but `file` is always an app-owned
// config-dir / registry path the caller computed — never user input. Same
// accepted class as paths.cpp's keyfile write.

#include <filesystem>
#include <fstream>
#include <ios>
#include <string_view>

#include "platform/path_utf8.h"
#include "platform/safe_print.h"

namespace platform {

// Atomically replace `file` with a sibling "<file>.tmp" holding `content`, then
// rename the temp over the target, so a crash mid-write can never leave a torn,
// half-written value for the next load() to read. `content` is the file's FULL
// contents (callers append their trailing newline). On failure the temp file is
// removed and a single `[<tag>]` diagnostic is logged — the tag keeps the old
// per-file prefix stable so existing logs are unchanged.
[[nodiscard]] inline bool atomic_write_file(const std::filesystem::path& file,
                                            std::string_view content, const char* tag)
{
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            platform::safe_println(stderr, "[{}] cannot write {}", tag, path_to_utf8(tmp));
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            platform::safe_println(stderr, "[{}] write error on {}", tag, path_to_utf8(tmp));
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);  // NOSONAR cpp:S2083
    if (ec) {
        platform::safe_println(stderr, "[{}] rename failed: {}", tag, ec.message());
        std::filesystem::remove(tmp, ec);  // NOSONAR cpp:S2083
        return false;
    }
    return true;
}

} // namespace platform