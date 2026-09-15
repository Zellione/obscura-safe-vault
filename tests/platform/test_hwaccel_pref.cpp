#include "test_framework.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "platform/hwaccel_pref.h"

namespace fs = std::filesystem;

namespace {
// RAII unique temp file path, removed on destruction.
struct TempFile {
    fs::path path;
    explicit TempFile(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_hwaccel_" + std::string(tag) + "_" + std::to_string(ctr++) + ".conf");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

// Redirects SDL_GetPrefPath() (used by default_location()) into a per-test
// temp dir so the test never touches the real config. Mirrors the same
// ScopedDataHome trick used by test_autoplay_pref.
struct ScopedDataHome {
    fs::path dir;
    std::string previous;
    bool had_previous = false;
    bool ready = false;

    ScopedDataHome()
    {
        dir = fs::temp_directory_path() / "osv_hwaccel_config_home";
        std::error_code ec;
        fs::remove_all(dir, ec);
        if (!fs::create_directories(dir, ec) && ec) return;

        constexpr const char* name = "XDG_DATA_HOME";
        if (const char* old = std::getenv(name)) {
            previous = old;
            had_previous = true;
        }
        ready = SDL_setenv_unsafe(name, dir.string().c_str(), 1) == 0;
    }

    ~ScopedDataHome()
    {
        constexpr const char* name = "XDG_DATA_HOME";
        if (had_previous) (void)SDL_setenv_unsafe(name, previous.c_str(), 1);
        else (void)SDL_unsetenv_unsafe(name);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};
}  // namespace

TEST(hwaccel_pref_missing_file_loads_defaults)
{
    TempFile tf("missing");
    platform::HwAccelPref p(tf.path);
    const auto s = p.load();
    CHECK(s.enable_hardware == true);     // default
    CHECK(s.force_software  == false);   // default
}

TEST(hwaccel_pref_round_trips_both_overrides)
{
    TempFile tf("roundtrip");
    platform::HwAccelPref p(tf.path);
    platform::HwAccelPref::State out{false, true};   // hw off, sw forced on
    REQUIRE(p.save(out));
    const auto loaded = p.load();
    CHECK(loaded.enable_hardware == false);
    CHECK(loaded.force_software  == true);

    platform::HwAccelPref::State back{true, false};   // restore defaults
    REQUIRE(p.save(back));
    const auto loaded2 = p.load();
    CHECK(loaded2.enable_hardware == true);
    CHECK(loaded2.force_software  == false);
}

TEST(hwaccel_pref_partial_file_loads_only_what_parses)
{
    // An older osv.conf might only contain one of the two keys; the
    // other should fall back to its default rather than be left
    // uninitialised or garbage.
    TempFile tf("partial");
    {
        std::ofstream out(tf.path, std::ios::binary);
        out << "hw=off\n";   // only the hardware line; force_software untouched
    }
    platform::HwAccelPref p(tf.path);
    const auto s = p.load();
    CHECK(s.enable_hardware == false);
    CHECK(s.force_software  == false);   // default
}

TEST(hwaccel_pref_garbage_lines_are_ignored)
{
    TempFile tf("garbage");
    {
        std::ofstream out(tf.path, std::ios::binary);
        out << "banana\n";
        out << "hw=off\n";
        out << "# this is a comment, hopefully\n";
        out << "swforce=on\n";
    }
    platform::HwAccelPref p(tf.path);
    const auto s = p.load();
    CHECK(s.enable_hardware == false);
    CHECK(s.force_software  == true);
}

TEST(hwaccel_pref_default_location_is_config_dir_hwaccel_conf)
{
    ScopedDataHome home;
    REQUIRE(home.ready);
    const auto pref = platform::HwAccelPref::default_location();
    CHECK(pref.file().filename() == "hwaccel.conf");
    platform::HwAccelPref::State out{false, true};
    REQUIRE(pref.save(out));
    const auto loaded = pref.load();
    CHECK(loaded.enable_hardware == false);
    CHECK(loaded.force_software  == true);
}
