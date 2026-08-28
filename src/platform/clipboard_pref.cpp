#include "platform/clipboard_pref.h"

#include <fstream>
#include <string>

#include "platform/path_utf8.h"
#include "platform/paths.h"
#include "platform/safe_print.h"

namespace platform {

ClipboardPref::ClipboardPref(std::filesystem::path file) : file_(std::move(file)) {}

ClipboardPref ClipboardPref::default_location()
{
    auto dir = config_dir();
    if (dir.empty()) return ClipboardPref{};  // no config dir → inert pref
    return ClipboardPref{dir / "clipboard.conf"};
}

namespace {
[[nodiscard]] std::string_view mode_slug(ClipboardMode m)
{
    using enum ClipboardMode;
    switch (m) {
    case Allow:
        return "allow";
    case Warn:
        return "warn";
    case Disable:
        return "off";
    }
    // unreachable; present for compiler completeness
    return "allow";
}

[[nodiscard]] ClipboardMode mode_from_slug(std::string_view slug)
{
    using enum ClipboardMode;
    if (slug == "warn") return Warn;
    if (slug == "off") return Disable;
    return Allow;  // unknown/missing → safe default
}
}  // namespace

ClipboardMode ClipboardPref::load() const
{
    if (file_.empty()) return ClipboardMode::Allow;

    std::ifstream in(file_, std::ios::binary);
    if (!in) return ClipboardMode::Allow;  // missing file → default

    std::string slug;
    std::getline(in, slug);
    if (!slug.empty() && slug.back() == '\r') slug.pop_back();  // tolerate CRLF
    return mode_from_slug(slug);                                // unknown slug → default
}

bool ClipboardPref::save(ClipboardMode m) const
{
    if (file_.empty()) return false;

    // Atomic replace: write a sibling temp file, then rename over the target so a
    // crash mid-write never leaves a torn value.
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            platform::safe_println(stderr, "[Platform] cannot write {}", path_to_utf8(tmp));
            return false;
        }
        out << mode_slug(m) << '\n';
        out.flush();
        if (!out) {
            platform::safe_println(stderr, "[Platform] write error on {}", path_to_utf8(tmp));
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        platform::safe_println(stderr, "[Platform] rename failed: {}", ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace platform