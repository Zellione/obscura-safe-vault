#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "platform/autoplay_pref.h"

namespace fs = std::filesystem;

namespace {
// RAII unique temp file path, removed on destruction.
struct TempFile {
    fs::path path;
    explicit TempFile(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_autoplay_" + std::string(tag) + "_" + std::to_string(ctr++) + ".conf");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};
}  // namespace

TEST(autoplay_pref_missing_file_loads_on)
{
    TempFile tf("missing");
    platform::AutoplayPref p(tf.path);
    CHECK(p.load() == true);
}

TEST(autoplay_pref_round_trips_off_and_on)
{
    TempFile tf("roundtrip");
    platform::AutoplayPref p(tf.path);
    REQUIRE(p.save(false));
    CHECK(p.load() == false);
    REQUIRE(p.save(true));
    CHECK(p.load() == true);
}

TEST(autoplay_pref_garbage_content_loads_on)
{
    TempFile tf("garbage");
    {
        std::ofstream out(tf.path, std::ios::binary);
        out << "banana\n";
    }
    platform::AutoplayPref p(tf.path);
    CHECK(p.load() == true);
}

TEST(autoplay_pref_default_location_is_config_dir_autoplay_conf)
{
    CHECK(platform::AutoplayPref::default_location().file().filename() == "autoplay.conf");
}
