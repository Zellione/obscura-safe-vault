#include "platform/second_vault_pref.h"

#include <fstream>
#include <print>
#include <string>

#include "platform/paths.h"

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
    switch (m) {
        case SecondVaultMode::LockNow:
            return "lock";
        case SecondVaultMode::KeepTimed:
            return "5min";
        case SecondVaultMode::KeepSession:
            return "session";
    }
    // unreachable; present for compiler completeness
    return "lock";
}

[[nodiscard]] SecondVaultMode mode_from_slug(std::string_view slug)
{
    if (slug == "5min") return SecondVaultMode::KeepTimed;
    if (slug == "session") return SecondVaultMode::KeepSession;
    return SecondVaultMode::LockNow;                       // unknown/missing → safe default
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

    // Atomic replace: write a sibling temp file, then rename over the target so a
    // crash mid-write never leaves a torn value.
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::println(stderr, "[Platform] cannot write {}", tmp.string());
            return false;
        }
        out << mode_slug(m) << '\n';
        out.flush();
        if (!out) {
            std::println(stderr, "[Platform] write error on {}", tmp.string());
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        std::println(stderr, "[Platform] rename failed: {}", ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace platform
