#include "test_framework.h"

#ifdef OSV_VENDORED_ARCHIVE

#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/folder_dialog.h"
#include "platform/vault_registry.h"
#include "ui/archive_test_helpers.h"
#include "ui/gallery_grid.h"
#include "ui/import_queue_test_helpers.h"
#include "ui/zip_test_helpers.h"

#include <fstream>

namespace ui {
struct ArchiveSelectionTestPeer {
    static void pick(GalleryGrid& grid, const std::vector<std::string>& paths)
    {
        grid.handle_multiple_archives_enqueue(paths);
    }
    static bool confirming(const GalleryGrid& grid) { return grid.volume_dialog_.active(); }
    static bool naming(const GalleryGrid& grid) { return grid.naming_.active; }
    static std::string name(const GalleryGrid& grid) { return grid.naming_.buf.str(); }
    static bool password_prompt(const GalleryGrid& grid) { return grid.naming_.password.active; }
    static void password(GalleryGrid& grid) { grid.naming_.password.buf.set_text("password"); }
    static bool pending_import(const GalleryGrid& grid) { return grid.naming_.zip.active; }
};
} // namespace ui

namespace {
void press(ui::GalleryGrid& grid, SDL_Keycode key)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    grid.handle_event(event);
}
} // namespace

TEST(archive_selection_confirms_each_set_once_and_refuses_gaps)
{
    const auto dir = ziptest::fresh_dir("archive_selection_dialog");
    const auto original = ziptest::fake_jpeg(64);
    const auto whole = archivetest::make_archive(
        {{"photo.jpg", original}}, "7zip", dir / "photos.7z");
    const auto bytes = archivetest::read_file(whole);
    REQUIRE(bytes.size() > 2);
    const size_t middle = bytes.size() / 2;
    const auto first = dir / "photos.7z.001";
    const auto second = dir / "photos.7z.002";
    {
        std::ofstream(first, std::ios::binary).write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(middle));
        std::ofstream(second, std::ios::binary).write(
            reinterpret_cast<const char*>(bytes.data() + middle),
            static_cast<std::streamsize>(bytes.size() - middle));
    }
    std::ofstream(dir / "book.part1.rar").put('x');
    std::ofstream(dir / "book.part3.rar").put('x');
    {
        vault::Vault vault;
        REQUIRE(vaulttest::make_directory_vault(vault, dir / "vault.osv"));
        vault::CommitLane lane;
        lane.start(vault);
        vault.set_commit_router(&lane);
        ui::ImportQueue queue;
        queue.begin_session(vault, lane);
        platform::VaultRegistry registry(dir / "vaults.list");
        gfx::Window window;
        gfx::FontAtlas font;
        gfx::TextureCache cache(nullptr);
        platform::FileDialog files;
        platform::FolderDialog folders;
        ui::GallerySessionState session;
        ui::GalleryGrid grid({window, font, vault, cache}, {files, folders},
                             {registry, (dir / "vault.osv").string(), nullptr}, session, queue);
        grid.on_enter();
        ui::ArchiveSelectionTestPeer::pick(
            grid, {second.string(), first.string(), (dir / "book.part1.rar").string(),
                   (dir / "book.part3.rar").string()});
        CHECK(queue.snapshot().empty());
        REQUIRE(ui::ArchiveSelectionTestPeer::confirming(grid));
        press(grid, SDLK_RETURN);
        REQUIRE(ui::ArchiveSelectionTestPeer::naming(grid));
        press(grid, SDLK_RETURN);
        REQUIRE(ui::ArchiveSelectionTestPeer::confirming(grid));
        REQUIRE(queue.snapshot().size() == 1);
        press(grid, SDLK_RETURN); // Missing RAR volume: confirmation must stay blocked.
        CHECK(ui::ArchiveSelectionTestPeer::confirming(grid));
        CHECK_FALSE(ui::ArchiveSelectionTestPeer::naming(grid));
        press(grid, SDLK_ESCAPE);
        CHECK_FALSE(ui::ArchiveSelectionTestPeer::confirming(grid));
        CHECK_FALSE(ui::ArchiveSelectionTestPeer::pending_import(grid));
        REQUIRE(importtest::drain_until_idle(queue));
        const auto rows = queue.snapshot();
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].state == ui::ImportTaskState::Done);
        CHECK_EQ(rows[0].imported, 1);
        const auto* photo = vault.resolve_node("photos/photo.jpg");
        REQUIRE(photo != nullptr);
        crypto::SecureBytes restored;
        REQUIRE(vault.read_image(*photo, restored) == vault::VaultResult::Ok);
        CHECK_BYTES_EQ(restored.as_span(), original);
        grid.on_exit();
        queue.end_session();
    }
    ziptest::cleanup_dir(dir);
}

TEST(archive_selection_encrypted_single_and_split_prompt_then_import)
{
    const auto dir = ziptest::fresh_dir("archive_selection_encrypted");
    const auto single = dir / "Single.7z";
    archivetest::uudecode(std::filesystem::path(OSV_UI_FIXTURE_DIR) /
                         "encrypted-headers-photo.7z.uu", single);
    const auto bytes = archivetest::read_file(single);
    const auto first = dir / "Split.7z.001";
    const auto second = dir / "Split.7z.002";
    const size_t middle = bytes.size() / 2;
    std::ofstream(first, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                static_cast<std::streamsize>(middle));
    std::ofstream(second, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data() + middle),
                                                 static_cast<std::streamsize>(bytes.size() - middle));
    {
        vault::Vault vault;
        REQUIRE(vaulttest::make_directory_vault(vault, dir / "vault.osv"));
        vault::CommitLane lane;
        lane.start(vault);
        vault.set_commit_router(&lane);
        ui::ImportQueue queue;
        queue.begin_session(vault, lane);
        platform::VaultRegistry registry(dir / "vaults.list");
        gfx::Window window;
        gfx::FontAtlas font;
        gfx::TextureCache cache(nullptr);
        platform::FileDialog files;
        platform::FolderDialog folders;
        ui::GallerySessionState session;
        ui::GalleryGrid grid({window, font, vault, cache}, {files, folders},
                             {registry, (dir / "vault.osv").string(), nullptr}, session, queue);
        grid.on_enter();
        ui::ArchiveSelectionTestPeer::pick(grid, {single.string(), second.string(), first.string()});
        REQUIRE(ui::ArchiveSelectionTestPeer::password_prompt(grid));
        CHECK(queue.snapshot().empty());
        ui::ArchiveSelectionTestPeer::password(grid);
        press(grid, SDLK_RETURN);
        REQUIRE(ui::ArchiveSelectionTestPeer::confirming(grid));
        press(grid, SDLK_RETURN);
        REQUIRE(ui::ArchiveSelectionTestPeer::naming(grid));
        CHECK_EQ(ui::ArchiveSelectionTestPeer::name(grid), std::string("split"));
        press(grid, SDLK_RETURN);
        REQUIRE(ui::ArchiveSelectionTestPeer::password_prompt(grid));
        ui::ArchiveSelectionTestPeer::password(grid);
        press(grid, SDLK_RETURN);
        REQUIRE(importtest::drain_until_idle(queue));
        const auto rows = queue.snapshot();
        REQUIRE(rows.size() == 2);
        for (const auto& row : rows) {
            CHECK(row.state == ui::ImportTaskState::Done);
            CHECK_EQ(row.imported, 1);
        }
        CHECK(vault.resolve_node("Single/photo.jpg") != nullptr);
        CHECK(vault.resolve_node("split/photo.jpg") != nullptr);
        grid.on_exit();
        queue.end_session();
    }
    ziptest::cleanup_dir(dir);
}

#endif
