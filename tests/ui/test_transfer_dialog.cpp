#include "test_framework.h"

#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/vault_registry.h"
#include "ui/transfer_dialog.h"
#include "ui/zip_test_helpers.h"

#include <chrono>
#include <thread>

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
