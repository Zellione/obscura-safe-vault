#include <chrono>
#include <cstdlib>
#include <new>
#include <print>
#include <stdexcept>
#include <system_error>
#include <thread>
#include "crypto/random.h"
#include "gfx/window.h"
#include "test_framework.h"
#include "ui/import_queue.h"
#include "ui/tag_editor.h"
#include "vault/fixtures.h"
#include "vault/tag_bundles.h"

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

namespace ui {
struct TagEditorTestAccess {
    static void launch(TagEditor& editor, std::function<void()> work)
    {
        editor.launch(std::move(work));
    }
    static bool has_error(const TagEditor& editor) { return !editor.error_.empty(); }
    static void preview_bundle(TagEditor& editor, std::string_view members)
    {
        editor.bundle_.mode = true;
        editor.open_bundle_form(false);
        editor.bundle_.members.set_text(members);
        editor.preview_bundle_change();
    }
    static std::string_view bundle_preview(const TagEditor& editor)
    {
        return editor.bundle_.preview;
    }
};
}

TEST(tag_editor_worker_resource_failures_join_and_allow_retry)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-tag-worker-failure.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == vault::VaultResult::Ok);
        gfx::Window window;
        ui::ImportQueue queue;
        ui::TagEditor editor(v, window, queue);
        for (int failure = 0; failure < 3; ++failure) {
            editor.open("one");
            REQUIRE(finish(editor));
            ui::TagEditorTestAccess::launch(editor, [failure] {
                if (failure == 0) throw std::bad_alloc{};
                if (failure == 1) throw std::length_error("synthetic size failure");
                throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
            });
            CHECK(editor.busy());
            REQUIRE(finish(editor));
            CHECK(ui::TagEditorTestAccess::has_error(editor));
            editor.close();
            CHECK(!editor.busy());
        }
        // Teardown joins and handles a failed worker even without update().
        editor.open("one");
        REQUIRE(finish(editor));
        ui::TagEditorTestAccess::launch(editor, [] { throw std::bad_alloc{}; });
        editor.close();
        CHECK(!editor.busy());
        editor.open("one");
        REQUIRE(finish(editor));
        CHECK(!ui::TagEditorTestAccess::has_error(editor));
        editor.close();
        v.lock();
    }
    std::filesystem::remove_all(path);
}

TEST(tag_editor_bundle_admin_preview_apply_remove_and_delete)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-bundle-editor-job.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("one", "manual") == vault::VaultResult::Ok);
        gfx::Window window;
        ui::ImportQueue queue;
        ui::TagEditor editor(v, window, queue);
        editor.open("one");
        REQUIRE(finish(editor));
        const auto press = [&](SDL_Keycode code) {
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.key = code;
            return editor.handle_event(key);
        };
        const auto type = [&](const char* value) {
            SDL_Event text{};
            text.type = SDL_EVENT_TEXT_INPUT;
            text.text.text = value;
            return editor.handle_event(text);
        };
        CHECK(press(SDLK_F3));
        CHECK(press(SDLK_F8));
        CHECK(type("bundle"));
        CHECK(press(SDLK_TAB));
        CHECK(type("manual;bundled"));
        CHECK(press(SDLK_F5));
        REQUIRE(finish(editor));
        CHECK(press(SDLK_RETURN));
        REQUIRE(finish(editor));
        REQUIRE(vault::vault_settings(v).tag_bundles.size() == 1);
        CHECK(press(SDLK_RETURN));
        REQUIRE(finish(editor));
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{2});
        CHECK(press(SDLK_DELETE));
        REQUIRE(finish(editor));
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{1});
        CHECK(v.resolve_node("one")->tags[0] == "manual");
        CHECK(press(SDLK_F6));
        REQUIRE(finish(editor));
        CHECK(press(SDLK_RETURN));
        REQUIRE(finish(editor));
        CHECK(vault::vault_settings(v).tag_bundles.empty());
        editor.close();
    }
    std::filesystem::remove_all(path);
}

TEST(tag_editor_bundle_normalization_preview_matches_committed_effects)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-bundle-preview-normalize.osv";
    std::filesystem::remove_all(path);
    using enum vault::VaultResult;
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == Ok);
        REQUIRE(vault::save_tag_bundle(v, "bundle", {"shared", "gone"}) == Ok);
        REQUIRE(vault::apply_tag_bundle(v, std::vector<std::string>{"one"}, "bundle", false) == Ok);
        gfx::Window window;
        ui::ImportQueue queue;
        ui::TagEditor editor(v, window, queue);
        editor.open("one");
        REQUIRE(finish(editor));
        ui::TagEditorTestAccess::preview_bundle(editor,
                                                " shared ; SHARED; ; new\\;tag; new\\;TAG; ");
        REQUIRE(finish(editor));
        CHECK(ui::TagEditorTestAccess::bundle_preview(editor) ==
              "Save: 1 applications, 1 tags added, 1 removed.");
        CHECK(v.resolve_node("one")->tags[1] == "gone");
        SDL_Event enter{};
        enter.type = SDL_EVENT_KEY_DOWN;
        enter.key.key = SDLK_RETURN;
        CHECK(editor.handle_event(enter));
        REQUIRE(finish(editor));
        REQUIRE(vault::vault_settings(v).tag_bundles[0].tags.size() == 2);
        CHECK(vault::vault_settings(v).tag_bundles[0].tags[0] == "shared");
        CHECK(vault::vault_settings(v).tag_bundles[0].tags[1] == "new;tag");
        CHECK(v.resolve_node("one")->tags == vault::vault_settings(v).tag_bundles[0].tags);
        ui::TagEditorTestAccess::preview_bundle(editor, " ;  ; ");
        REQUIRE(finish(editor));
        CHECK(ui::TagEditorTestAccess::bundle_preview(editor) ==
              "Save: 1 applications, 0 tags added, 2 removed.");
        CHECK(editor.handle_event(enter));
        REQUIRE(finish(editor));
        CHECK(v.resolve_node("one")->tags.empty());
        CHECK(vault::vault_settings(v).tag_bundles[0].tags.empty());
        editor.close();
    }
    std::filesystem::remove_all(path);
}
