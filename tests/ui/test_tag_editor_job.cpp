#include <chrono>
#include <cstdlib>
#include <print>
#include <thread>
#include "crypto/random.h"
#include "gfx/window.h"
#include "test_framework.h"
#include "ui/import_queue.h"
#include "ui/tag_editor.h"
#include "vault/fixtures.h"

namespace {
bool finish(ui::TagEditor& editor, std::chrono::seconds timeout = std::chrono::seconds{10})
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (editor.busy() && std::chrono::steady_clock::now() < deadline) {
        editor.update();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return !editor.busy();
}

TEST(tag_editor_selection_benchmark)
{
    const char* requested = std::getenv("OSV_TAG_SELECTION_BENCH");
    if (!requested) return;
    const int count = std::atoi(requested);
    REQUIRE(count > 0 && count <= 50000);
    const auto path = std::filesystem::temp_directory_path() / "osv-tag-editor-benchmark.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        auto* root = v.resolve_node("");
        REQUIRE(root);
        std::vector<std::string> paths;
        for (int i = 0; i < count; ++i) {
            vault::IndexNode node;
            node.type = vault::IndexNode::Type::Gallery;
            node.name = "item-" + std::to_string(i);
            REQUIRE(crypto::fill_random(node.node_id));
            paths.emplace_back(node.name.view());
            root->children.push_back(std::move(node));
        }
        REQUIRE(vault::set_vault_settings(v, vault::vault_settings(v)) == vault::VaultResult::Ok);
        gfx::Window window;
        ui::ImportQueue queue;
        ui::TagEditor editor(v, window, queue);
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        editor.open_multi(paths);
        const auto returned = Clock::now();
        REQUIRE(finish(editor, std::chrono::seconds{120}));
        const auto opened = Clock::now();
        SDL_Event text{};
        text.type = SDL_EVENT_TEXT_INPUT;
        text.text.text = "shared";
        CHECK(editor.handle_event(text));
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.key = SDLK_RETURN;
        const auto add_start = Clock::now();
        CHECK(editor.handle_event(key));
        const auto add_returned = Clock::now();
        REQUIRE(finish(editor, std::chrono::seconds{120}));
        const auto added = Clock::now();
        key.key.key = SDLK_DELETE;
        CHECK(editor.handle_event(key));
        REQUIRE(finish(editor, std::chrono::seconds{120}));
        const auto removed = Clock::now();
        const auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        std::println("[TAG BENCH] nodes={} open-call={:.2f}ms open-total={:.2f}ms "
                     "add-call={:.2f}ms add-total={:.2f}ms remove-total={:.2f}ms",
                     count, ms(start, returned), ms(start, opened), ms(add_start, add_returned),
                     ms(add_start, added), ms(added, removed));
        CHECK(ms(start, returned) < 100);
        CHECK(ms(add_start, add_returned) < 100);
        editor.close();
    }
    std::filesystem::remove_all(path);
}
}  // namespace

TEST(tag_editor_worker_add_remove_and_close_join_before_vault_teardown)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-tag-editor-job.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("two") == vault::VaultResult::Ok);
        gfx::Window window;
        ui::ImportQueue queue;
        ui::TagEditor editor(v, window, queue);
        editor.open_multi({"one", "two"});
        REQUIRE(editor.busy());
        REQUIRE(finish(editor));
        SDL_Event text{};
        text.type = SDL_EVENT_TEXT_INPUT;
        text.text.text = "shared";
        CHECK(editor.handle_event(text));
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.key = SDLK_RETURN;
        CHECK(editor.handle_event(key));
        REQUIRE(editor.busy());
        REQUIRE(finish(editor));
        REQUIRE(v.resolve_node("one")->tags.size() == 1);
        CHECK(v.resolve_node("one")->tags[0] == "shared");
        CHECK_EQ(v.resolve_node("two")->tags.size(), size_t{1});
        key.key.key = SDLK_DELETE;
        CHECK(editor.handle_event(key));
        REQUIRE(finish(editor));
        CHECK(v.resolve_node("one")->tags.empty());
        editor.close();
        editor.open_multi({"one", "two"});
        REQUIRE(editor.busy());
        editor.close();
        CHECK(!editor.busy());
        CHECK(!editor.active());
        v.lock();
    }
    std::filesystem::remove_all(path);
}
