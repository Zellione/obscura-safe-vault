#include "test_framework.h"

#include "platform/file_dialog.h"
#include "platform/vault_registry.h"
#include "gfx/window.h"
#include "gfx/text.h"
#include "ui/vault_manager.h"

#include <filesystem>
#include <string>
#include <vector>

// Test seam: FileDialog's Open/Done transition is normally driven by SDL (the
// open_* methods show a real dialog; on_files is the async SDL callback). The
// peer reaches the same private state machine without showing a dialog, so the
// result-routing logic can be exercised headlessly.
namespace platform {
struct FileDialogTestPeer {
    static inline int parent_picker_calls = 0;
    static inline bool parent_allow_many = true;
    static void SDLCALL pick_parent(SDL_DialogFileCallback callback, void* userdata,
                                   SDL_Window*, const char*, bool allow_many)
    {
        ++parent_picker_calls;
        parent_allow_many = allow_many;
        const char* paths[] = {"/tmp", nullptr};
        callback(userdata, paths, -1);
    }
    static void use_parent_picker(FileDialog& d) { d.vault_parent_picker_ = pick_parent; }
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

TEST(file_dialog_new_vault_selects_parent_folder)
{
    FileDialog d;
    Peer::use_parent_picker(d);
    Peer::parent_picker_calls = 0;
    d.open_vault_parent(nullptr);
    CHECK(Peer::parent_picker_calls == 1);
    CHECK_FALSE(Peer::parent_allow_many);
    CHECK_FALSE(d.take_result(Purpose::VaultDirectory).has_value());
    CHECK_FALSE(d.take_result(Purpose::SaveVault).has_value());
    const auto result = d.take_result(Purpose::VaultParent);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(result->front() == "/tmp");
    Peer::arm(d, Purpose::Images);
    d.open_vault_parent(nullptr);
    CHECK(Peer::parent_picker_calls == 1);
}

TEST(file_dialog_new_vault_manager_names_child_before_navigation)
{
    FileDialog d;
    Peer::use_parent_picker(d);
    gfx::Window window;
    gfx::FontAtlas font;
    platform::VaultRegistry registry;
    ui::VaultManager manager(window, font, registry, d, {});
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_N;
    manager.handle_event(event);
    manager.update(0);
    CHECK(manager.take_nav().kind == ui::NavKind::None);
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "../outside";
    manager.handle_event(event);
    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_RETURN;
    manager.handle_event(event);
    CHECK(manager.take_nav().kind == ui::NavKind::None);
    event.key.key = SDLK_A;
    event.key.mod = SDL_KMOD_CTRL;
    manager.handle_event(event);
    event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "osv-picker-regression-été";
    manager.handle_event(event);
    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_RETURN;
    manager.handle_event(event);
    const auto nav = manager.take_nav();
    CHECK(nav.kind == ui::NavKind::ToUnlock);
    CHECK(nav.path == "/tmp/osv-picker-regression-été.osv");
    CHECK_FALSE(std::filesystem::exists(nav.path));
}

TEST(file_dialog_new_vault_manager_cancel_stays_in_manager)
{
    FileDialog d;
    Peer::use_parent_picker(d);
    gfx::Window window;
    gfx::FontAtlas font;
    platform::VaultRegistry registry;
    ui::VaultManager manager(window, font, registry, d, {});
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_N;
    manager.handle_event(event);
    Peer::complete(d, {});
    manager.update(0);
    CHECK(manager.take_nav().kind == ui::NavKind::None);
    manager.handle_event(event);
    manager.update(0);
    event.key.key = SDLK_ESCAPE;
    manager.handle_event(event);
    CHECK(manager.take_nav().kind == ui::NavKind::None);
    event.key.key = SDLK_RETURN;
    manager.handle_event(event);
    CHECK(manager.take_nav().kind == ui::NavKind::None);
}

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

// Phase 55: a tag-dictionary (.json) pick is its own Purpose so it cannot be
// drained by the tag-list (.txt) poller or any other handler sharing the dialog.
TEST(file_dialog_tagjson_result_isolated_from_other_purposes)
{
    FileDialog d;
    Peer::arm(d, Purpose::TagJson);
    Peer::complete(d, {"/tmp/dict.json"});

    CHECK_FALSE(d.take_result(Purpose::TagList).has_value());
    CHECK_FALSE(d.take_result(Purpose::Images).has_value());
    CHECK_FALSE(d.take_result(Purpose::Zip).has_value());

    auto json = d.take_result(Purpose::TagJson);
    REQUIRE(json.has_value());
    REQUIRE(json->size() == 1);
    CHECK(std::filesystem::path(json->front()) == std::filesystem::path("/tmp/dict.json"));
}

TEST(file_dialog_taglist_result_not_taken_by_tagjson_purpose)
{
    FileDialog d;
    Peer::arm(d, Purpose::TagList);
    Peer::complete(d, {"/tmp/tags.txt"});

    CHECK_FALSE(d.take_result(Purpose::TagJson).has_value());
    CHECK(d.take_result(Purpose::TagList).has_value());
}

TEST(file_dialog_cancelled_tagjson_pick_still_resolves)
{
    FileDialog d;
    Peer::arm(d, Purpose::TagJson);
    Peer::complete(d, {});   // user cancelled

    auto json = d.take_result(Purpose::TagJson);
    REQUIRE(json.has_value());
    CHECK(json->empty());
}

// Phase 51 Task 14: Multi-select archives. The zip dialog now accepts
// allow_many=true, so multiple archives can be picked at once.
TEST(file_dialog_zip_multi_select)
{
    FileDialog d;
    Peer::arm(d, Purpose::Zip);
    Peer::complete(d, {"/tmp/a.zip", "/tmp/b.cbz", "/tmp/c.7z"});

    auto zip = d.take_result(Purpose::Zip);
    REQUIRE(zip.has_value());
    CHECK(zip->size() == 3);
    CHECK(std::filesystem::path(zip->at(0)) == std::filesystem::path("/tmp/a.zip"));
    CHECK(std::filesystem::path(zip->at(1)) == std::filesystem::path("/tmp/b.cbz"));
    CHECK(std::filesystem::path(zip->at(2)) == std::filesystem::path("/tmp/c.7z"));
}
