#include "test_framework.h"

#include <SDL3/SDL.h>

#include <cstdlib>
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


struct ScopedDataHome {
    fs::path dir;
    std::string previous;
    bool had_previous = false;
    bool ready = false;

    ScopedDataHome()
    {
        dir = fs::temp_directory_path() / "osv_autoplay_config_home";
        std::error_code ec;
        fs::remove_all(dir, ec);
        if (!fs::create_directories(dir, ec) && ec) return;

#if defined(_WIN32)
        constexpr const char* name = "APPDATA";
#else
        constexpr const char* name = "XDG_DATA_HOME";
#endif
        if (const char* old = std::getenv(name)) {
            previous = old;
            had_previous = true;
        }
        ready = SDL_setenv_unsafe(name, dir.string().c_str(), 1) == 0;
    }

    ~ScopedDataHome()
    {
#if defined(_WIN32)
        constexpr const char* name = "APPDATA";
#else
        constexpr const char* name = "XDG_DATA_HOME";
#endif
        if (had_previous) (void)SDL_setenv_unsafe(name, previous.c_str(), 1);
        else (void)SDL_unsetenv_unsafe(name);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
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
    ScopedDataHome home;
    REQUIRE(home.ready);
    const auto pref = platform::AutoplayPref::default_location();
    CHECK(pref.file().filename() == "autoplay.conf");
    REQUIRE(pref.save(false));
    CHECK(pref.load() == false);
}
