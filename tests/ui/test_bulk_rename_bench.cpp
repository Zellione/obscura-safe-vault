#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <print>
#include <string_view>
#include <thread>
#include "crypto/random.h"
#include "gfx/window.h"
#include "test_framework.h"
#include "ui/bulk_rename.h"
#include "ui/import_queue.h"
#include "vault/fixtures.h"
#include "vault/rename_batch.h"
#include "vault/staging.h"
namespace {
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}
}  // namespace
TEST(bulk_rename_selection_benchmark)
{
    const char* enabled = std::getenv("OSV_RENAME_SELECTION_BENCH");
    if (!enabled || std::string_view(enabled) != "1") return;
    for (int count : {1000, 10000, 50000}) {
        const auto path = std::filesystem::temp_directory_path() / "osv-122c-benchmark.osv";
        std::filesystem::remove_all(path);
        {
            vault::Vault v;
            REQUIRE(vaulttest::make_directory_vault(v, path));
            auto* root = v.resolve_node("");
            REQUIRE(root);
            std::vector<vault::RenameId> ids;
            root->children.reserve(static_cast<size_t>(count));
            ids.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                vault::IndexNode n;
                n.name = "old-" + std::to_string(i);
                REQUIRE(crypto::fill_random(n.node_id));
                ids.push_back(n.node_id);
                root->children.push_back(std::move(n));
            }
            REQUIRE(vault::commit_staged(v) == vault::VaultResult::Ok);
            gfx::Window win;
            ui::ImportQueue queue;
            ui::BulkRename modal(v, win, queue);
            auto start = Clock::now();
            modal.open(std::move(ids));
            const auto opened = Clock::now();
            SDL_Event e{};
            e.type = SDL_EVENT_TEXT_INPUT;
            e.text.text = "^old-(.*)$";
            (void)modal.handle_event(e);
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = SDLK_TAB;
            (void)modal.handle_event(e);
            e.type = SDL_EVENT_TEXT_INPUT;
            e.text.text = "new-$1";
            (void)modal.handle_event(e);
            auto run = [&](const char* phase) {
                e.type = SDL_EVENT_KEY_DOWN;
                e.key.key = SDLK_RETURN;
                const auto begin = Clock::now();
                (void)modal.handle_event(e);
                const auto returned = Clock::now();
                double max_poll = 0, max_gap = 0;
                auto previous = returned;
                const auto deadline = begin + std::chrono::seconds{120};
                while (modal.busy() && Clock::now() < deadline) {
                    const auto a = Clock::now();
                    max_gap = std::max(max_gap, ms(previous, a));
                    modal.update();
                    const auto b = Clock::now();
                    max_poll = std::max(max_poll, ms(a, b));
                    previous = a;
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
                std::println("BENCH count={} phase={} call={:.3f}ms total={:.3f}ms "
                             "max-poll={:.3f}ms max-gap={:.3f}ms",
                             count, phase, ms(begin, returned), ms(begin, Clock::now()), max_poll,
                             max_gap);
                return !modal.busy();
            };
            std::println("BENCH count={} open-call={:.3f}ms", count, ms(start, opened));
            REQUIRE(run("preview"));
            REQUIRE(modal.active());
            REQUIRE(run("apply"));
            CHECK(modal.consume_completed());
            CHECK(v.resolve_node("new-0") != nullptr);
            CHECK(v.resolve_node("new-" + std::to_string(count - 1)) != nullptr);
        }
        std::filesystem::remove_all(path);
    }
}
