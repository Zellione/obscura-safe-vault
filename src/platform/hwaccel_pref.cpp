#include "platform/hwaccel_pref.h"

#include <fstream>
#include <string>

#include "platform/atomic_write.h"
#include "platform/paths.h"

namespace platform {

namespace {
constexpr const char* kEnableHardwareOn  = "hw=on";
constexpr const char* kEnableHardwareOff = "hw=off";
constexpr const char* kForceSoftwareOn   = "swforce=on";
constexpr const char* kForceSoftwareOff  = "swforce=off";

// Trim trailing \r for CRLF tolerance, like AutoplayPref.
std::string rstrip_cr(std::string line)
{
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}
}  // namespace

HwAccelPref::HwAccelPref(std::filesystem::path file) : file_(std::move(file)) {}

HwAccelPref HwAccelPref::default_location()
{
    auto dir = config_dir();
    if (dir.empty()) return HwAccelPref{};               // no config dir → inert pref
    return HwAccelPref{dir / "hwaccel.conf"};
}

HwAccelPref::State HwAccelPref::load() const
{
    State s;   // defaults: enable_hardware=true, force_software=false
    if (file_.empty()) return s;

    std::ifstream in(file_, std::ios::binary);
    if (!in) return s;                                    // missing file → defaults

    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = rstrip_cr(line);
        if (trimmed == kEnableHardwareOn)  s.enable_hardware = true;
        else if (trimmed == kEnableHardwareOff) s.enable_hardware = false;
        else if (trimmed == kForceSoftwareOn)  s.force_software = true;
        else if (trimmed == kForceSoftwareOff) s.force_software = false;
        // Unknown lines (comments, future keys) are silently ignored.
    }
    return s;
}

bool HwAccelPref::save(State s) const
{
    if (file_.empty()) return false;
    const std::string content =
        std::string(s.enable_hardware ? kEnableHardwareOn : kEnableHardwareOff) + "\n" +
        std::string(s.force_software  ? kForceSoftwareOn  : kForceSoftwareOff) + "\n";
    return platform::atomic_write_file(file_, content, "HwAccelPref");
}

} // namespace platform
