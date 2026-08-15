#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "platform/gallery_view_pref.h"

namespace fs = std::filesystem;

namespace {
// RAII unique temp file path, removed on destruction.
struct TempFile {
    fs::path path;
    explicit TempFile(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_gallery_view_" + std::string(tag) + "_" + std::to_string(ctr++) + ".conf");
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

TEST(gallery_view_pref_round_trip)
{
    TempFile tf("roundtrip");
    const platform::GalleryViewPref pref(tf.path);
    CHECK_EQ(pref.load(), ui::GalleryView::GridM);          // missing -> default
    REQUIRE(pref.save(ui::GalleryView::List));
    CHECK_EQ(pref.load(), ui::GalleryView::List);
    REQUIRE(pref.save(ui::GalleryView::GridXL));
    CHECK_EQ(pref.load(), ui::GalleryView::GridXL);
}

TEST(gallery_view_pref_garbage_loads_default)
{
    TempFile tf("garbage");
    { std::ofstream(tf.path, std::ios::binary) << "wat\n"; }
    const platform::GalleryViewPref pref(tf.path);
    CHECK_EQ(pref.load(), ui::GalleryView::GridM);
}

TEST(gallery_view_pref_stores_only_the_slug)
{
    TempFile tf("slugonly");
    const platform::GalleryViewPref pref(tf.path);
    REQUIRE(pref.save(ui::GalleryView::GridXL));
    // The raw file is exactly the stable slug + newline — nothing else.
    CHECK_EQ(read_text(tf.path), std::string("grid-xl\n"));
}

TEST(gallery_view_pref_empty_path_instance_is_safe_noop)
{
    platform::GalleryViewPref pref;   // default ctor: no file
    CHECK_EQ(pref.load(), ui::GalleryView::GridM);
    CHECK_FALSE(pref.save(ui::GalleryView::List));
}
