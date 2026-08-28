#include "platform/theme_pref.h"

#include <fstream>
#include <string>

#include "platform/atomic_write.h"
#include "platform/path_utf8.h"
#include "platform/paths.h"
#include "platform/safe_print.h"

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
    return platform::atomic_write_file(file_, std::string(gfx::theme_slug(id)) + "\n", "ThemePref");
}

} // namespace platform
