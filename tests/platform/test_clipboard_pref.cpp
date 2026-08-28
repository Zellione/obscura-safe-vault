#include "test_framework.h"

#include <filesystem>

#include "platform/clipboard_pref.h"

namespace {
std::filesystem::path temp_pref_file(const char* name)
{
    auto p = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(p);
    return p;
}
}  // namespace

TEST(clipboard_pref_missing_file_loads_allow)
{
    const platform::ClipboardPref pref(temp_pref_file("osv_cbp_missing.conf"));
    CHECK(pref.load() == platform::ClipboardMode::Allow);
}

TEST(clipboard_pref_round_trips_each_mode)
{
    const auto file = temp_pref_file("osv_cbp_rt.conf");
    const platform::ClipboardPref pref(file);
    for (auto m : {platform::ClipboardMode::Allow, platform::ClipboardMode::Warn,
                   platform::ClipboardMode::Disable}) {
        CHECK(pref.save(m));
        CHECK(pref.load() == m);
    }
    std::filesystem::remove(file);
}

TEST(clipboard_pref_garbage_content_loads_allow)
{
    const auto file = temp_pref_file("osv_cbp_bad.conf");
    {
        auto* fp = std::fopen(file.string().c_str(), "wb");
        std::fputs("definitely-not-a-mode\n", fp);
        std::fclose(fp);
    }
    const platform::ClipboardPref pref(file);
    CHECK(pref.load() == platform::ClipboardMode::Allow);
    std::filesystem::remove(file);
}

TEST(clipboard_pref_empty_pref_saves_nothing)
{
    const platform::ClipboardPref pref;  // default-constructed: no backing file
    CHECK_FALSE(pref.save(platform::ClipboardMode::Warn));
}