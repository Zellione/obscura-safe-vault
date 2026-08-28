#include "platform/autoplay_pref.h"

#include <cerrno>
#include <fstream>
#include <string>

#include "platform/atomic_write.h"
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
    return platform::atomic_write_file(file_, enabled ? "on\n" : "off\n", "AutoplayPref");
}

} // namespace platform
