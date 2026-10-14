#include "test_framework.h"

#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/vault_registry.h"
#include "ui/import_queue.h"
#include "ui/second_vault.h"
#include "ui/transfer_dialog.h"
#include "ui/zip_test_helpers.h"
#include "vault/commit_lane.h"

#include <chrono>
#include <thread>
#include <utility>

namespace {
void press(ui::TransferDialog& dialog, SDL_Keycode key, int& _failures)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    CHECK(dialog.handle_event(event));
}

void pull_into_empty_gallery(const std::string& target, int& _failures)
{
    const auto dir = ziptest::fresh_dir(target.empty() ? "pull_empty_root" : "pull_empty_nested");
    {
        vault::Vault source, destination;
        ziptest::make_vault(source, dir / "source.osv");
        ziptest::make_vault(destination, dir / "destination.osv");
        CHECK(source.create_gallery("Photos") == vault::VaultResult::Ok);
        CHECK(source.add_image("Photos", ziptest::fake_jpeg(7), "photo.jpg") == vault::VaultResult::Ok);
        source.lock();
        if (!target.empty()) CHECK(destination.create_gallery(target) == vault::VaultResult::Ok);
        CHECK(destination.list(target).empty());

        platform::VaultRegistry registry(dir / "vaults.list");
        CHECK(registry.add(dir / "source.osv"));
        CHECK(registry.add(dir / "destination.osv"));
        platform::FileDialog files;
        gfx::Window window;
        ui::TransferDialog dialog(destination, (dir / "destination.osv").string(),
                                  registry, files, window, nullptr);
        ui::open_pull_transfer(dialog, target);
        CHECK(dialog.active());
        press(dialog, SDLK_RETURN, _failures); // Copy (default).
        press(dialog, SDLK_RETURN, _failures); // Source vault; destination must be excluded.
        SDL_Event password{};
        password.type = SDL_EVENT_TEXT_INPUT;
        password.text.text = "pw";
        CHECK(dialog.handle_event(password));
        press(dialog, SDLK_RETURN, _failures); // Unlock source.
        press(dialog, SDLK_SPACE, _failures);  // Select Photos.
        press(dialog, SDLK_RETURN, _failures); // Start copying.
        CHECK(dialog.job_active());
        for (int i = 0; i < 5000 && dialog.active(); ++i) {
            dialog.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(!dialog.active());
        ui::TransferCompletion completion;
        CHECK(dialog.consume_completed(completion));
        CHECK_EQ(completion.failed_total, 0);
        const auto copied = target.empty() ? "Photos" : target + "/Photos";
        CHECK_EQ(destination.list(copied).size(), size_t{1});
        CHECK(ziptest::open_vault(dir / "source.osv", source));
        CHECK_EQ(source.list("Photos").size(), size_t{1}); // Copy keeps the original.
    }
    ziptest::cleanup_dir(dir);
}
} // namespace

TEST(transfer_dialog_pull_into_empty_vault)
{
    pull_into_empty_gallery("", _failures);
}

TEST(transfer_dialog_pull_into_empty_nested_gallery)
{
    pull_into_empty_gallery("Destination", _failures);
}

TEST(transfer_dialog_empty_pull_can_cancel_and_reopen)
{
    vault::Vault vault;
    platform::VaultRegistry registry;
    platform::FileDialog files;
    gfx::Window window;
    ui::TransferDialog dialog(vault, "", registry, files, window, nullptr);
    ui::open_pull_transfer(dialog, "");
    press(dialog, SDLK_ESCAPE, _failures);
    CHECK(!dialog.active());
    CHECK(!dialog.job_active());
    ui::open_pull_transfer(dialog, "Destination");
    CHECK(dialog.active());
    press(dialog, SDLK_ESCAPE, _failures);
    CHECK(!dialog.active());
}

TEST(transfer_dialog_directory_copy_and_move_with_warm_vault_close_cleanly)
{
    for (const auto& [pull, move] : {std::pair{false, false}, std::pair{false, true},
                                     std::pair{true, false}, std::pair{true, true}}) {
        const auto dir = ziptest::fresh_dir(move ? "v3_move_close" : "v3_copy_close");
        {
            vault::Vault source, active;
            const std::vector<uint8_t> password{'p', 'w'};
            REQUIRE(vault::Vault::create_directory((dir / "source.osv").string(), password, {},
                                                   ziptest::kTestKdf,
                                                   source) == vault::VaultResult::Ok);
            REQUIRE(vault::Vault::create_directory((dir / "active.osv").string(), password, {},
                                                   ziptest::kTestKdf,
                                                   active) == vault::VaultResult::Ok);
            auto& origin = pull ? source : active;
            REQUIRE(origin.create_gallery("Photos") == vault::VaultResult::Ok);
            REQUIRE(origin.add_image("Photos", ziptest::fake_jpeg(7), "photo.jpg") ==
                    vault::VaultResult::Ok);
            source.lock();

            auto lane = std::make_unique<vault::CommitLane>();
            lane->start(active);
            active.set_commit_router(lane.get());
            ui::ImportQueue queue;
            queue.begin_session(active, *lane);
            queue.set_exclusive(true);
            ui::SecondVaultSession second;
            second.set_default_mode(platform::SecondVaultMode::KeepSession);
            platform::VaultRegistry registry(dir / "vaults.list");
            REQUIRE(registry.add(dir / "source.osv"));
            platform::FileDialog files;
            gfx::Window window;
            {
                ui::TransferDialog dialog(active, (dir / "active.osv").string(), registry, files,
                                          window, &second);
                if (pull)
                    ui::open_pull_transfer(dialog, "");
                else
                    dialog.open_gallery("Photos");
                // Pull defaults to Copy, push defaults to Move.
                if (move == pull) press(dialog, SDLK_DOWN, _failures);
                press(dialog, SDLK_RETURN, _failures);
                if (!pull) press(dialog, SDLK_DOWN, _failures);  // Skip "This vault".
                press(dialog, SDLK_RETURN, _failures);
                SDL_Event text{};
                text.type = SDL_EVENT_TEXT_INPUT;
                text.text.text = "pw";
                REQUIRE(dialog.handle_event(text));
                press(dialog, SDLK_RETURN, _failures);
                if (pull) press(dialog, SDLK_SPACE, _failures);
                press(dialog, SDLK_RETURN, _failures);
                REQUIRE(dialog.job_active());
                for (int i = 0; i < 5000 && dialog.active(); ++i) {
                    dialog.update();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                REQUIRE(!dialog.active());
                ui::TransferCompletion completion;
                REQUIRE(dialog.consume_completed(completion));
                CHECK_EQ(completion.failed_total, 0);
                CHECK(second.occupied());
                CHECK(second.vault().is_unlocked());
                CHECK(active.is_unlocked());
            }
            // App shutdown order: screens, import queue, warm vault, active vault,
            // then lane. Both destination durability and source move semantics survive.
            queue.end_session();
            second.wipe();
            active.lock();
            lane.reset();
            REQUIRE(ziptest::open_vault(dir / "active.osv", active));
            CHECK_EQ(active.list("Photos").size(), !pull && move ? size_t{0} : size_t{1});
            REQUIRE(ziptest::open_vault(dir / "source.osv", source));
            CHECK_EQ(source.list("Photos").size(), pull && move ? size_t{0} : size_t{1});
        }
        ziptest::cleanup_dir(dir);
    }
}
