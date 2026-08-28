#include "platform/clipboard_pref.h"

#include <fstream>
#include <string>

#include "platform/atomic_write.h"
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
    return platform::atomic_write_file(file_, std::string(mode_slug(m)) + "\n", "Platform");
}

}  // namespace platform