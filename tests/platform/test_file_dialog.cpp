#include "test_framework.h"

#include "platform/file_dialog.h"

#include <filesystem>
#include <string>
#include <vector>

// Test seam: FileDialog's Open/Done transition is normally driven by SDL (the
// open_* methods show a real dialog; on_files is the async SDL callback). The
// peer reaches the same private state machine without showing a dialog, so the
// result-routing logic can be exercised headlessly.
namespace platform {
struct FileDialogTestPeer {
    static void arm(FileDialog& d, FileDialog::Purpose p) { d.begin_open(p); }

    static void complete(FileDialog& d, const std::vector<std::string>& paths)
    {
        std::vector<const char*> argv;
        argv.reserve(paths.size() + 1);
        for (const auto& s : paths) argv.push_back(s.c_str());
        argv.push_back(nullptr);
        FileDialog::on_files(&d, argv.data(), 0);
    }
};
}  // namespace platform

using platform::FileDialog;
using Peer = platform::FileDialogTestPeer;
using Purpose = platform::FileDialog::Purpose;

// Regression (Phase 17): a zip pick must NOT be consumable as an image import.
// GalleryGrid polls one shared dialog from two handlers (pump_import then
// pump_zip_import); before this fix the image handler drained the zip result
// first and stored the .zip verbatim as a broken image item.
TEST(file_dialog_zip_result_not_taken_by_images_purpose)
{
    FileDialog d;
    Peer::arm(d, Purpose::Zip);
    Peer::complete(d, {"/tmp/archive.zip"});

    // The image-import poller runs first but must skip a zip-tagged result.
    CHECK_FALSE(d.take_result(Purpose::Images).has_value());

    // The zip poller still finds it (the result was not consumed above).
    auto zip = d.take_result(Purpose::Zip);
    REQUIRE(zip.has_value());
    REQUIRE(zip->size() == 1);
    // Compared as a path, not a string: on_files now runs every dialog result
    // through platform::normalize_user_path, whose lexically_normal() renders
    // with the platform's preferred separator ("\tmp\archive.zip" on Windows).
    // fs::path treats both separators as separators; the raw text does not.
    CHECK(std::filesystem::path(zip->front()) ==
          std::filesystem::path("/tmp/archive.zip"));
}

TEST(file_dialog_images_result_not_taken_by_zip_purpose)
{
    FileDialog d;
    Peer::arm(d, Purpose::Images);
    Peer::complete(d, {"/tmp/a.png", "/tmp/b.jpg"});

    CHECK_FALSE(d.take_result(Purpose::Zip).has_value());

    auto imgs = d.take_result(Purpose::Images);
    REQUIRE(imgs.has_value());
    CHECK(imgs->size() == 2);
}

// A matching take_result consumes the result: a second poll sees nothing.
TEST(file_dialog_matching_take_consumes_result)
{
    FileDialog d;
    Peer::arm(d, Purpose::Zip);
    Peer::complete(d, {"/tmp/archive.zip"});

    CHECK(d.take_result(Purpose::Zip).has_value());
    CHECK_FALSE(d.take_result(Purpose::Zip).has_value());
}

// A cancelled dialog (no files) still resolves for the matching purpose, so the
// caller can repaint/clear state; the wrong purpose still ignores it.
TEST(file_dialog_cancel_routes_to_matching_purpose)
{
    FileDialog d;
    Peer::arm(d, Purpose::Zip);
    Peer::complete(d, {});   // user cancelled

    CHECK_FALSE(d.take_result(Purpose::Images).has_value());

    auto zip = d.take_result(Purpose::Zip);
    REQUIRE(zip.has_value());
    CHECK(zip->empty());     // present-but-empty == cancelled
}

// Regression (Phase 21): a tag-list (.txt) pick must NOT be consumable by the
// image-import or zip-import pollers that share the same FileDialog, and the
// tag-list poller must skip an image/zip result tagged for another purpose.
TEST(file_dialog_taglist_result_isolated_from_images_and_zip)
{
    FileDialog d;
    Peer::arm(d, Purpose::TagList);
    Peer::complete(d, {"/tmp/tags.txt"});

    CHECK_FALSE(d.take_result(Purpose::Images).has_value());
    CHECK_FALSE(d.take_result(Purpose::Zip).has_value());

    auto txt = d.take_result(Purpose::TagList);
    REQUIRE(txt.has_value());
    REQUIRE(txt->size() == 1);
    CHECK(std::filesystem::path(txt->front()) ==
          std::filesystem::path("/tmp/tags.txt"));
}

TEST(file_dialog_images_result_not_taken_by_taglist_purpose)
{
    FileDialog d;
    Peer::arm(d, Purpose::Images);
    Peer::complete(d, {"/tmp/a.png"});

    CHECK_FALSE(d.take_result(Purpose::TagList).has_value());
    CHECK(d.take_result(Purpose::Images).has_value());
}

// The no-arg take_result() keeps its purpose-agnostic behaviour for the
// single-consumer screens (unlock, vault manager, transfer keyfile).
TEST(file_dialog_no_arg_take_is_purpose_agnostic)
{
    FileDialog d;
    Peer::arm(d, Purpose::Vault);
    Peer::complete(d, {"/tmp/my.osv"});

    auto res = d.take_result();
    REQUIRE(res.has_value());
    CHECK(res->size() == 1);
}
