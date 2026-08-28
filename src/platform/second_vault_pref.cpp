#include "platform/second_vault_pref.h"

#include <fstream>
#include <string>

#include "platform/atomic_write.h"
#include "platform/path_utf8.h"
#include "platform/paths.h"
#include "platform/safe_print.h"

namespace platform {

SecondVaultPref::SecondVaultPref(std::filesystem::path file) : file_(std::move(file)) {}

SecondVaultPref SecondVaultPref::default_location()
{
    auto dir = config_dir();
    if (dir.empty()) return SecondVaultPref{};              // no config dir → inert pref
    return SecondVaultPref{dir / "second_vault.conf"};
}

namespace {
[[nodiscard]] std::string_view mode_slug(SecondVaultMode m)
{
    using enum SecondVaultMode;
    switch (m) {
        case LockNow:
            return "lock";
        case KeepTimed:
            return "5min";
        case KeepSession:
            return "session";
    }
    // unreachable; present for compiler completeness
    return "lock";
}

[[nodiscard]] SecondVaultMode mode_from_slug(std::string_view slug)
{
    using enum SecondVaultMode;
    if (slug == "5min") return KeepTimed;
    if (slug == "session") return KeepSession;
    return LockNow;                       // unknown/missing → safe default
}
} // namespace

SecondVaultMode SecondVaultPref::load() const
{
    if (file_.empty()) return SecondVaultMode::LockNow;

    std::ifstream in(file_, std::ios::binary);
    if (!in) return SecondVaultMode::LockNow;              // missing file → default

    std::string slug;
    std::getline(in, slug);
    if (!slug.empty() && slug.back() == '\r') slug.pop_back();   // tolerate CRLF
    return mode_from_slug(slug);                           // unknown slug → default
}

bool SecondVaultPref::save(SecondVaultMode m) const
{
    if (file_.empty()) return false;
    return platform::atomic_write_file(file_, std::string(mode_slug(m)) + "\n", "Platform");
}

} // namespace platform
