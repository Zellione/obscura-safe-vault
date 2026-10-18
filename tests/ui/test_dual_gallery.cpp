#include "test_framework.h"

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/folder_dialog.h"
#include "platform/vault_registry.h"
#include "ui/dual_gallery.h"
#include "ui/dual_session_state.h"
#include "ui/gallery_session_state.h"
#include "ui/import_queue.h"
#include "ui/second_vault.h"
#include "ui/zip_test_helpers.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace {
void press(ui::Screen& screen, SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    event.key.mod = mod;
    screen.handle_event(event);
}

void password(ui::Screen& screen)
{
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "pw";
    screen.handle_event(event);
    press(screen, SDLK_RETURN);
}
}  // namespace

TEST(dual_gallery_cross_vault_copy_both_directions_same_root_and_teardown)
{
    const auto dir = ziptest::fresh_dir("dual_vault_copy");
    {
        vault::Vault primary, secondary;
        const std::vector<uint8_t> pw{'p', 'w'};
        REQUIRE(vault::Vault::create_directory((dir / "primary.osv").string(), pw, {},
                                               ziptest::kTestKdf,
                                               primary) == vault::VaultResult::Ok);
        REQUIRE(vault::Vault::create_directory((dir / "secondary.osv").string(), pw, {},
                                               ziptest::kTestKdf,
                                               secondary) == vault::VaultResult::Ok);
        REQUIRE(primary.create_gallery("Left") == vault::VaultResult::Ok);
        REQUIRE(primary.add_image("Left", ziptest::fake_jpeg(1), "left.jpg") ==
                vault::VaultResult::Ok);
        REQUIRE(secondary.create_gallery("Right") == vault::VaultResult::Ok);
        REQUIRE(secondary.add_image("Right", ziptest::fake_jpeg(2), "right.jpg") ==
                vault::VaultResult::Ok);
        REQUIRE(secondary.add_image("", ziptest::fake_jpeg(9), "root.jpg") ==
                vault::VaultResult::Ok);
        secondary.lock();
        platform::VaultRegistry registry(dir / "vaults.list");
        REQUIRE(registry.add(dir / "secondary.osv"));
        gfx::Window window;
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        REQUIRE(window.init());
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        gfx::FontAtlas font;
        REQUIRE(font.bake_from_file(OSV_DEFAULT_FONT, 24));
        gfx::TextureCache cache(window.sdl_renderer());
        gfx::Renderer renderer(window.sdl_renderer());
        platform::FileDialog files;
        platform::FolderDialog folders;
        ui::GallerySessionState session;
        ui::DualSessionState dual;
        ui::ImportQueue queue;
        {
            ui::DualGalleryScreen screen({window, font, primary, cache}, {files, folders},
                                         {registry, (dir / "primary.osv").string(), nullptr},
                                         session, queue, dual);
            screen.on_enter();
            screen.render(renderer);
            press(screen, SDLK_O, SDL_KMOD_CTRL);
            CHECK(SDL_TextInputActive(window.sdl_window()));
            screen.render(renderer);
            press(screen, SDLK_RETURN);
            press(screen, SDLK_RETURN);  // Empty password fails without changing panes.
            CHECK(screen.take_nav().kind == ui::NavKind::None);
            password(screen);
            screen.update(0.01);
            CHECK(!SDL_TextInputActive(window.sdl_window()));
            // Opening selects the right pane. Copy mixed media/gallery selection.
            press(screen, SDLK_A, SDL_KMOD_CTRL);
            press(screen, SDLK_M);
            press(screen, SDLK_RETURN);
            REQUIRE(screen.blocks_idle_lock());
            // Navigation must not escape while a worker owns the vaults.
            press(screen, SDLK_F3);
            CHECK(screen.take_nav().kind == ui::NavKind::None);
            for (int i = 0; i < 5000 && screen.blocks_idle_lock(); ++i) {
                screen.update(0.01);
                screen.render(renderer);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            REQUIRE(!screen.blocks_idle_lock());
            CHECK_EQ(primary.list("Right").size(), size_t{1});
            CHECK(primary.resolve_node("root.jpg") != nullptr);
            // Same-named gallery: suffix collision policy preserves both copies.
            press(screen, SDLK_HOME);
            press(screen, SDLK_M);
            press(screen, SDLK_RETURN);
            press(screen, SDLK_DOWN);
            press(screen, SDLK_RETURN);
            REQUIRE(screen.blocks_idle_lock());
            for (int i = 0; i < 5000 && screen.blocks_idle_lock(); ++i) {
                screen.update(0.01);
                screen.render(renderer);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            REQUIRE(!screen.blocks_idle_lock());
            CHECK_EQ(primary.list("Right_2").size(), size_t{1});
            // Copy Left to the right root, keeping the original.
            press(screen, SDLK_TAB);
            press(screen, SDLK_HOME);
            press(screen, SDLK_M);
            press(screen, SDLK_RETURN);
            REQUIRE(screen.blocks_idle_lock());
            for (int i = 0; i < 5000 && screen.blocks_idle_lock(); ++i) {
                screen.update(0.01);
                screen.render(renderer);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            REQUIRE(!screen.blocks_idle_lock());
            CHECK_EQ(primary.list("Left").size(), size_t{1});
            press(screen, SDLK_W, SDL_KMOD_CTRL);
            REQUIRE(ziptest::open_vault(dir / "secondary.osv", secondary));
            secondary.lock();  // Ctrl+W released the writer lease while screen stays alive.
            screen.on_exit();
        }
        REQUIRE(ziptest::open_vault(dir / "secondary.osv", secondary));
        CHECK_EQ(secondary.list("Left").size(), size_t{1});
        CHECK_EQ(secondary.list("Right").size(), size_t{1});
    }
    ziptest::cleanup_dir(dir);
}

TEST(gallery_pull_keeps_updates_off_worker_owned_tree)
{
    const auto dir = ziptest::fresh_dir("gallery_pull_updates");
    {
        vault::Vault primary, secondary;
        const std::vector<uint8_t> pw{'p', 'w'};
        REQUIRE(vault::Vault::create_directory((dir / "primary.osv").string(), pw, {},
                                               ziptest::kTestKdf,
                                               primary) == vault::VaultResult::Ok);
        ziptest::make_vault(secondary, dir / "secondary.osv");
        REQUIRE(primary.add_image("", ziptest::fake_jpeg(3), "existing.jpg") ==
                vault::VaultResult::Ok);
        constexpr int galleries = 40;
        for (int i = 0; i < galleries; ++i) {
            const auto name = "Gallery" + std::to_string(i);
            REQUIRE(secondary.create_gallery(name) == vault::VaultResult::Ok);
            REQUIRE(secondary.add_image(name, ziptest::fake_jpeg(1), "photo.jpg") ==
                    vault::VaultResult::Ok);
        }
        secondary.lock();
        platform::VaultRegistry registry(dir / "vaults.list");
        REQUIRE(registry.add(dir / "secondary.osv"));
        gfx::Window window;
        gfx::FontAtlas font;
        gfx::TextureCache cache(nullptr);
        platform::FileDialog files;
        platform::FolderDialog folders;
        ui::GallerySessionState session;
        session.detail_open = true;
        ui::ImportQueue queue;
        ui::GalleryGrid grid({window, font, primary, cache}, {files, folders},
                             {registry, (dir / "primary.osv").string(), nullptr}, session, queue);
        grid.on_enter();
        press(grid, SDLK_M);
        press(grid, SDLK_DOWN);  // Pull direction.
        press(grid, SDLK_RETURN);
        press(grid, SDLK_DOWN);  // Copy instead of Move.
        press(grid, SDLK_RETURN);
        press(grid, SDLK_RETURN);  // Source vault.
        password(grid);
        for (int i = 0; i < galleries; ++i) {
            press(grid, SDLK_SPACE);
            press(grid, SDLK_DOWN);
        }
        press(grid, SDLK_RETURN);
        REQUIRE(grid.blocks_idle_lock());
        for (int i = 0; i < 10000 && grid.blocks_idle_lock(); ++i) {
            grid.update(0.1);  // detail/hover must not inspect the reallocating tree
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(!grid.blocks_idle_lock());
        grid.update(0.1);
        CHECK_EQ(primary.list("").size(), size_t{galleries + 1});
        grid.on_exit();
    }
    ziptest::cleanup_dir(dir);
}

namespace ui {
struct TransferOwnershipTestPeer {
    static bool start(GalleryGrid& grid, vault::Vault& vault, std::atomic<bool>& entered,
                      std::atomic<bool>& resume)
    {
        grid.transfer_.active_ = true;
        grid.transfer_.stage_ = TransferDialog::Stage::Running;
        return grid.transfer_.run_.job.launch(FileOpKind::Transfer, [&] {
            // Reallocate the exact tree whose node pointers the grid cached.
            const bool ok = vault.create_gallery("Incoming") == vault::VaultResult::Ok;
            entered.store(true);
            for (int i = 0; i < 5000 && !resume.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            FileOpOutcome outcome;
            outcome.ok = ok;
            return outcome;
        });
    }
};
}  // namespace ui

TEST(gallery_transfer_paused_after_tree_reallocation_does_not_read_stale_nodes)
{
    const auto dir = ziptest::fresh_dir("transfer_paused_tree");
    {
        vault::Vault primary;
        ziptest::make_vault(primary, dir / "primary.osv");
        REQUIRE(primary.add_image("", ziptest::fake_jpeg(3), "existing.jpg") ==
                vault::VaultResult::Ok);
        platform::VaultRegistry registry(dir / "vaults.list");
        gfx::Window window;
        gfx::FontAtlas font;
        gfx::TextureCache cache(nullptr);
        platform::FileDialog files;
        platform::FolderDialog folders;
        ui::GallerySessionState session;
        session.detail_open = true;
        ui::ImportQueue queue;
        std::atomic<bool> entered{false}, resume{false};
        ui::GalleryGrid grid({window, font, primary, cache}, {files, folders},
                             {registry, (dir / "primary.osv").string(), nullptr}, session, queue);
        grid.on_enter();
        REQUIRE(ui::TransferOwnershipTestPeer::start(grid, primary, entered, resume));
        for (int i = 0; i < 2000 && !entered.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        CHECK(entered.load());
        grid.update(0.1);  // ASAN: old code dereferences the freed selected node here.
        CHECK(grid.blocks_idle_lock());
        resume.store(true);
        for (int i = 0; i < 5000 && grid.blocks_idle_lock(); ++i) {
            grid.update(0.1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(!grid.blocks_idle_lock());
        CHECK_EQ(primary.list("").size(), size_t{2});
        grid.update(0.1);
        grid.on_exit();
    }
    ziptest::cleanup_dir(dir);
}

TEST(dual_gallery_warm_vault_is_owned_until_cancel_or_shutdown)
{
    for (const bool cancel : {false, true}) {
        const auto dir = ziptest::fresh_dir("split_warm_teardown");
        {
            vault::Vault primary, source;
            const std::vector<uint8_t> pw{'p', 'w'};
            REQUIRE(vault::Vault::create_directory((dir / "primary.osv").string(), pw, {},
                                                   ziptest::kTestKdf,
                                                   primary) == vault::VaultResult::Ok);
            ziptest::make_vault(source, dir / "source.osv");
            REQUIRE(source.create_gallery("Photos") == vault::VaultResult::Ok);
            for (int i = 0; i < 50; ++i)
                REQUIRE(source.add_image("Photos", ziptest::fake_jpeg(4),
                                         std::to_string(i) + ".jpg") == vault::VaultResult::Ok);
            ui::SecondVaultSession warm;
            warm.adopt(std::move(source), (dir / "source.osv").string(),
                       platform::SecondVaultMode::KeepTimed);
            platform::VaultRegistry registry(dir / "vaults.list");
            REQUIRE(registry.add(dir / "source.osv"));
            gfx::Window window;
            gfx::FontAtlas font;
            gfx::TextureCache cache(nullptr);
            platform::FileDialog files;
            platform::FolderDialog folders;
            ui::GallerySessionState session;
            ui::DualSessionState dual;
            ui::ImportQueue queue;
            {
                ui::DualGalleryScreen screen({window, font, primary, cache}, {files, folders},
                                             {registry, (dir / "primary.osv").string(), &warm},
                                             session, queue, dual);
                screen.on_enter();
                press(screen, SDLK_O, SDL_KMOD_CTRL);
                press(screen, SDLK_RETURN);  // Warm slot: no password round-trip.
                CHECK(!warm.occupied());
                (void)warm.tick(10000, false);  // cannot expire a pane-owned vault
                press(screen, SDLK_M);
                press(screen, SDLK_RETURN);
                REQUIRE(screen.blocks_idle_lock());
                if (cancel) {
                    press(screen, SDLK_ESCAPE);
                    for (int i = 0; i < 5000 && screen.blocks_idle_lock(); ++i) {
                        screen.update(0.01);
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    CHECK(!screen.blocks_idle_lock());
                }
                screen.on_exit();  // also joins a still-running transfer on shutdown
            }
            REQUIRE(ziptest::open_vault(dir / "source.osv", source));
            CHECK_EQ(source.list("Photos").size(), size_t{50});
            CHECK(primary.list("Photos").size() <= size_t{50});
        }
        ziptest::cleanup_dir(dir);
    }
}

TEST(dual_gallery_same_vault_move_blocks_navigation_and_refreshes_both_panes)
{
    const auto dir = ziptest::fresh_dir("split_same_vault");
    {
        vault::Vault vault;
        ziptest::make_vault(vault, dir / "vault.osv");
        REQUIRE(vault.create_gallery("A") == vault::VaultResult::Ok);
        REQUIRE(vault.create_gallery("B") == vault::VaultResult::Ok);
        REQUIRE(vault.add_image("A", ziptest::fake_jpeg(1), "photo.jpg") == vault::VaultResult::Ok);
        platform::VaultRegistry registry(dir / "vaults.list");
        gfx::Window window;
        gfx::FontAtlas font;
        gfx::TextureCache cache(nullptr);
        platform::FileDialog files;
        platform::FolderDialog folders;
        ui::GallerySessionState session;
        ui::DualSessionState dual;
        dual.pane[0].path = "A";
        dual.pane[1].path = "B";
        ui::ImportQueue queue;
        ui::DualGalleryScreen screen({window, font, vault, cache}, {files, folders},
                                     {registry, (dir / "vault.osv").string(), nullptr}, session,
                                     queue, dual);
        screen.on_enter();
        press(screen, SDLK_N);  // Naming modal owns F3 and Tab.
        press(screen, SDLK_F3);
        press(screen, SDLK_TAB);
        CHECK(screen.take_nav().kind == ui::NavKind::None);
        press(screen, SDLK_ESCAPE);
        press(screen, SDLK_M);
        press(screen, SDLK_RETURN);
        REQUIRE(screen.blocks_idle_lock());
        CHECK(screen.animating());
        press(screen, SDLK_TAB);
        press(screen, SDLK_F3);
        CHECK(screen.take_nav().kind == ui::NavKind::None);
        for (int i = 0; i < 5000 && screen.blocks_idle_lock(); ++i) {
            screen.update(0.01);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(!screen.blocks_idle_lock());
        CHECK(vault.list("A").empty());
        CHECK_EQ(vault.list("B").size(), size_t{1});
        press(screen, SDLK_TAB);
        press(screen, SDLK_RETURN);
        const auto nav = screen.take_nav();
        CHECK(nav.kind == ui::NavKind::ToViewer);
        CHECK(nav.path == "B");
        screen.on_exit();
    }
    ziptest::cleanup_dir(dir);
}
