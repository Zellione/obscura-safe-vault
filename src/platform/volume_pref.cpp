#include "platform/volume_pref.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "platform/atomic_write.h"
#include "platform/path_utf8.h"
#include "platform/paths.h"
#include "platform/safe_print.h"

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
    char* endp = nullptr;
    errno = 0;
    const float parsed = std::strtof(line.c_str(), &endp);
    if (endp == line.c_str() || errno == ERANGE) return kDefaultVolume;
    v = parsed;
    return clamp01(v);
}

bool VolumePref::save(float volume) const
{
    if (file_.empty()) return false;
    // Format through the same stream default the ofstream path used, so the
    // on-disk bytes (e.g. "0.5", not "0.500000") are unchanged.
    std::ostringstream oss;
    oss << clamp01(volume) << '\n';
    return platform::atomic_write_file(file_, oss.str(), "VolumePref");
}

} // namespace platform
