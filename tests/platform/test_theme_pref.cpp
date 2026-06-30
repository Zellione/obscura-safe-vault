#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "platform/theme_pref.h"

namespace fs = std::filesystem;

namespace {
// RAII unique temp file path, removed on destruction.
struct TempFile {
    fs::path path;
    explicit TempFile(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_theme_" + std::string(tag) + "_" + std::to_string(ctr++) + ".conf");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

std::string read_text(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}
} // namespace

TEST(theme_pref_missing_file_is_default)
{
    TempFile tf("missing");
    platform::ThemePref pref(tf.path);
    CHECK_TRUE(pref.load() == gfx::ThemeId::RefinedSlate);
}

TEST(theme_pref_save_then_load_round_trips)
{
    TempFile tf("roundtrip");
    platform::ThemePref pref(tf.path);
    for (int i = 0; i < gfx::THEME_COUNT; ++i) {
        const auto id = static_cast<gfx::ThemeId>(i);
        REQUIRE(pref.save(id));
        CHECK_TRUE(pref.load() == id);
    }
}

TEST(theme_pref_persists_across_instances)
{
    TempFile tf("persist");
    { platform::ThemePref pref(tf.path); REQUIRE(pref.save(gfx::ThemeId::Midnight)); }
    platform::ThemePref pref2(tf.path);
    CHECK_TRUE(pref2.load() == gfx::ThemeId::Midnight);
}

TEST(theme_pref_unknown_slug_loads_default)
{
    TempFile tf("unknown");
    { std::ofstream(tf.path, std::ios::binary) << "no-such-theme\n"; }
    platform::ThemePref pref(tf.path);
    CHECK_TRUE(pref.load() == gfx::ThemeId::RefinedSlate);
}

TEST(theme_pref_tolerates_crlf)
{
    TempFile tf("crlf");
    { std::ofstream(tf.path, std::ios::binary) << "midnight\r\n"; }
    platform::ThemePref pref(tf.path);
    CHECK_TRUE(pref.load() == gfx::ThemeId::Midnight);
}

TEST(theme_pref_stores_only_the_slug)
{
    TempFile tf("slugonly");
    platform::ThemePref pref(tf.path);
    REQUIRE(pref.save(gfx::ThemeId::HighContrast));
    // The raw file is exactly the stable slug + newline — nothing else.
    CHECK_EQ(read_text(tf.path), std::string("high-contrast\n"));
}

TEST(theme_pref_empty_path_instance_is_safe_noop)
{
    platform::ThemePref pref;   // default ctor: no file
    CHECK_TRUE(pref.load() == gfx::ThemeId::RefinedSlate);
    CHECK_FALSE(pref.save(gfx::ThemeId::Light));
}
