#include "platform/theme_pref.h"

#include <fstream>
#include <print>
#include <string>

#include "platform/paths.h"

namespace platform {

ThemePref::ThemePref(std::filesystem::path file) : file_(std::move(file)) {}

ThemePref ThemePref::default_location()
{
    auto dir = config_dir();
    if (dir.empty()) return ThemePref{};                // no config dir → inert pref
    return ThemePref{dir / "theme.conf"};
}

gfx::ThemeId ThemePref::load() const
{
    if (file_.empty()) return gfx::ThemeId::RefinedSlate;

    std::ifstream in(file_, std::ios::binary);
    if (!in) return gfx::ThemeId::RefinedSlate;         // missing file → default

    std::string slug;
    std::getline(in, slug);
    if (!slug.empty() && slug.back() == '\r') slug.pop_back();   // tolerate CRLF
    return gfx::theme_from_slug(slug);                  // unknown slug → default
}

bool ThemePref::save(gfx::ThemeId id) const
{
    if (file_.empty()) return false;

    // Atomic replace: write a sibling temp file, then rename over the target so a
    // crash mid-write never leaves a torn value.
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::println(stderr, "[ThemePref] cannot write {}", tmp.string());
            return false;
        }
        out << gfx::theme_slug(id) << '\n';
        out.flush();
        if (!out) {
            std::println(stderr, "[ThemePref] write error on {}", tmp.string());
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        std::println(stderr, "[ThemePref] rename failed: {}", ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace platform
