#include <chrono>
#include <thread>
#include "gfx/window.h"
#include "test_framework.h"
#include "ui/bulk_rename.h"
#include "ui/import_queue.h"
#include "vault/fixtures.h"

namespace {
bool finish_rename(ui::BulkRename& modal)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (modal.busy() && std::chrono::steady_clock::now() < deadline) {
        modal.update();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return !modal.busy();
}
void key(ui::BulkRename& modal, SDL_Keycode code)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    (void)modal.handle_event(event);
}
void text(ui::BulkRename& modal, const char* value)
{
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = value;
    (void)modal.handle_event(event);
}
}  // namespace
TEST(bulk_rename_worker_preview_apply_edit_invalidation_and_close_join)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-bulk-rename-modal.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == vault::VaultResult::Ok);
        const auto id = v.resolve_node("one")->node_id;
        gfx::Window window;
        ui::ImportQueue queue;
        ui::BulkRename modal(v, window, queue);
        modal.open({id});
        text(modal, "one");
        key(modal, SDLK_TAB);
        text(modal, "two");
        key(modal, SDLK_RETURN);
        REQUIRE(modal.busy());
        REQUIRE(finish_rename(modal));
        CHECK(v.resolve_node("one") != nullptr);  // preview never commits
        text(modal, "2");                         // editing invalidates the previous confirmation
        key(modal, SDLK_RETURN);
        REQUIRE(finish_rename(modal));
        CHECK(v.resolve_node("one") != nullptr);
        key(modal, SDLK_RETURN);
        REQUIRE(finish_rename(modal));
        CHECK(modal.consume_completed());
        CHECK(!modal.active());
        CHECK(v.resolve_node("two2") != nullptr);
        modal.open({id});
        text(modal, "two");
        key(modal, SDLK_RETURN);
        modal.close();  // cancellation joins before vault destruction
        CHECK(!modal.busy());
        CHECK(!modal.active());
        v.lock();
    }
    std::filesystem::remove_all(path);
}
