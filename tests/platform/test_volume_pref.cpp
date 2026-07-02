#include "test_framework.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "platform/volume_pref.h"

namespace fs = std::filesystem;

namespace {
// RAII unique temp file path, removed on destruction.
struct TempFile {
    fs::path path;
    explicit TempFile(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_vol_" + std::string(tag) + "_" + std::to_string(ctr++) + ".conf");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

bool near(float a, float b) { return std::abs(a - b) < 1e-4f; }

void write_text(const fs::path& p, const std::string& s)
{
    std::ofstream(p, std::ios::binary) << s;
}
} // namespace

TEST(volume_pref_missing_file_is_full)
{
    TempFile tf("missing");
    platform::VolumePref pref(tf.path);
    CHECK(near(pref.load(), 1.0f));
}

TEST(volume_pref_round_trips)
{
    TempFile tf("round");
    platform::VolumePref pref(tf.path);
    CHECK(pref.save(0.35f));
    CHECK(near(pref.load(), 0.35f));
}

TEST(volume_pref_clamps_out_of_range_on_save_and_load)
{
    TempFile tf("clamp");
    platform::VolumePref pref(tf.path);

    CHECK(pref.save(2.5f));               // over-range save is clamped to 1.0
    CHECK(near(pref.load(), 1.0f));
    CHECK(pref.save(-1.0f));              // under-range save is clamped to 0.0
    CHECK(near(pref.load(), 0.0f));

    write_text(tf.path, "5.0\n");         // a hand-edited over-range value clamps on load
    CHECK(near(pref.load(), 1.0f));
}

TEST(volume_pref_garbage_is_full)
{
    TempFile tf("garbage");
    platform::VolumePref pref(tf.path);
    write_text(tf.path, "not-a-number\n");
    CHECK(near(pref.load(), 1.0f));
}

TEST(volume_pref_empty_backing_is_inert)
{
    platform::VolumePref pref;             // no file
    CHECK(near(pref.load(), 1.0f));        // default
    CHECK_FALSE(pref.save(0.5f));          // nothing to write to
}
