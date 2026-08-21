#include "platform/autoplay_pref.h"

#include <cerrno>
#include <fstream>
#include <string>

#include "platform/path_utf8.h"
#include "platform/paths.h"
#include "platform/safe_print.h"

namespace platform {

namespace {
constexpr bool kDefaultAutoplay = true;
}  // namespace

AutoplayPref::AutoplayPref(std::filesystem::path file) : file_(std::move(file)) {}

AutoplayPref AutoplayPref::default_location()
{
    auto dir = config_dir();
    if (dir.empty()) return AutoplayPref{};               // no config dir → inert pref
    return AutoplayPref{dir / "autoplay.conf"};
}

bool AutoplayPref::load() const
{
    if (file_.empty()) return kDefaultAutoplay;

    std::ifstream in(file_, std::ios::binary);
    if (!in) return kDefaultAutoplay;                     // missing file → default

    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();   // tolerate CRLF

    if (line == "off") return false;
    if (line == "on") return true;
    return kDefaultAutoplay;                              // invalid → default (true)
}

bool AutoplayPref::save(bool enabled) const
{
    if (file_.empty()) return false;

    // Atomic replace: write a sibling temp file, then rename over the target so a
    // crash mid-write never leaves a torn value.
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            safe_println(stderr, "[AutoplayPref] cannot write {}", path_to_utf8(tmp));
            return false;
        }
        out << (enabled ? "on" : "off") << '\n';
        out.flush();
        if (!out) {
            safe_println(stderr, "[AutoplayPref] write error on {}", path_to_utf8(tmp));
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        safe_println(stderr, "[AutoplayPref] rename failed: {}", ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace platform
