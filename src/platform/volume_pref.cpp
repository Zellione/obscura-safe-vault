#include "platform/volume_pref.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <print>
#include <string>

#include "platform/paths.h"

namespace platform {

namespace {
constexpr float kDefaultVolume = 1.0f;
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
}  // namespace

VolumePref::VolumePref(std::filesystem::path file) : file_(std::move(file)) {}

VolumePref VolumePref::default_location()
{
    auto dir = config_dir();
    if (dir.empty()) return VolumePref{};               // no config dir → inert pref
    return VolumePref{dir / "volume.conf"};
}

float VolumePref::load() const
{
    if (file_.empty()) return kDefaultVolume;

    std::ifstream in(file_, std::ios::binary);
    if (!in) return kDefaultVolume;                     // missing file → default

    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();   // tolerate CRLF

    float v = kDefaultVolume;
    const char* begin = line.data();
    const char* end   = line.data() + line.size();
    if (auto [ptr, ec] = std::from_chars(begin, end, v); ec != std::errc{})
        return kDefaultVolume;                          // unparseable → default
    return clamp01(v);
}

bool VolumePref::save(float volume) const
{
    if (file_.empty()) return false;

    // Atomic replace: write a sibling temp file, then rename over the target so a
    // crash mid-write never leaves a torn value.
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::println(stderr, "[VolumePref] cannot write {}", tmp.string());
            return false;
        }
        out << clamp01(volume) << '\n';
        out.flush();
        if (!out) {
            std::println(stderr, "[VolumePref] write error on {}", tmp.string());
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        std::println(stderr, "[VolumePref] rename failed: {}", ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace platform
