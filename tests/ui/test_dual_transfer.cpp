#include "test_framework.h"

#include "ui/dual_transfer.h"
#include "ui/parent_group.h"
#include "ui/file_op_job.h"
#include "ui/zip_test_helpers.h"
#include "vault/transfer.h"
#include "vault/vault.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>
#include <vector>

using ui::DualTransferPrompt;
using P = DualTransferPrompt;
using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_vault;

namespace {
namespace fs = std::filesystem;

// Poll take_outcome() with a generous timeout so the test never hangs CI.
std::optional<ui::FileOpOutcome> await_outcome(ui::FileOpJob& job)
{
    for (int i = 0; i < 5000; ++i) {
        if (auto oc = job.take_outcome()) return oc;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

[[nodiscard]] bool seed_images(vault::Vault& v, const char* g, int n)
{
    if (v.create_gallery(g) != vault::VaultResult::Ok) return false;
    for (int i = 1; i <= n; ++i)
        if (v.add_image(g, ziptest::fake_jpeg(static_cast<uint8_t>(i)),
                        std::to_string(i) + ".jpg") != vault::VaultResult::Ok)
            return false;
    return true;
}
} // namespace

// --- Unit tests for DualTransferPrompt state machine ---

TEST(dual_prompt_opens_on_mode_stage_move_selected)
{
    P p;
    p.open("Holiday", {});
    CHECK(p.stage() == P::Stage::Mode);
    CHECK_EQ(p.selected(), 0);
}

TEST(dual_prompt_enter_move_no_conflicts_fires_with_fail_policy)
{
    P p;
    p.open("Holiday", {});
    const auto l = p.key(P::Key::Enter);
    CHECK(l.fire);
    CHECK(l.mode == vault::TransferMode::Move);
    CHECK(l.policy == vault::CollisionPolicy::Fail);
    CHECK(p.stage() == P::Stage::Closed);
}

TEST(dual_prompt_copy_row_fires_copy)
{
    P p;
    p.open("Holiday", {});
    (void)p.key(P::Key::Down);
    const auto l = p.key(P::Key::Enter);
    CHECK(l.fire);
    CHECK(l.mode == vault::TransferMode::Copy);
}

TEST(dual_prompt_cancel_row_and_esc_do_not_fire)
{
    P p;
    p.open("Holiday", {});
    (void)p.key(P::Key::Down);
    (void)p.key(P::Key::Down);
    CHECK(!p.key(P::Key::Enter).fire);
    CHECK(p.stage() == P::Stage::Closed);
    p.open("Holiday", {});
    CHECK(!p.key(P::Key::Esc).fire);
    CHECK(p.stage() == P::Stage::Closed);
}

TEST(dual_prompt_conflicts_route_through_conflict_stage)
{
    P p;
    p.open("Holiday", {"a", "b"});
    (void)p.key(P::Key::Enter);                    // Move ->
    CHECK(p.stage() == P::Stage::Conflict);
    const auto l = p.key(P::Key::Enter);           // Combine row (default)
    CHECK(l.fire);
    CHECK(l.policy == vault::CollisionPolicy::Combine);
}

TEST(dual_prompt_conflict_rename_row_maps_to_suffix)
{
    P p;
    p.open("Holiday", {"a"});
    (void)p.key(P::Key::Enter);
    (void)p.key(P::Key::Down);
    const auto l = p.key(P::Key::Enter);
    CHECK(l.fire);
    CHECK(l.policy == vault::CollisionPolicy::Suffix);
}

TEST(dual_prompt_conflict_esc_returns_to_mode)
{
    P p;
    p.open("Holiday", {"a"});
    (void)p.key(P::Key::Enter);
    (void)p.key(P::Key::Esc);
    CHECK(p.stage() == P::Stage::Mode);
}

TEST(dual_prompt_up_down_clamp)
{
    P p;
    p.open("Holiday", {});
    (void)p.key(P::Key::Up);
    CHECK_EQ(p.selected(), 0);
    (void)p.key(P::Key::Down); (void)p.key(P::Key::Down); (void)p.key(P::Key::Down);
    CHECK_EQ(p.selected(), 2);
}

// --- Integration tests: FileOpJob transfer through same vault ---

TEST(file_op_job_same_vault_move_transfers_items)
{
    auto dir = fresh_dir("osv_dual_move");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "galA", 2));
        REQUIRE(v.create_gallery("galA/sub") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("galB") == vault::VaultResult::Ok);

        // Build spec: media from galA + gallery galA/sub
        std::vector<std::string> media_paths = {"galA/1.jpg"};
        std::vector<ui::ParentGroup> groups = ui::group_by_parent(media_paths);
        std::vector<std::string> gallery_paths = {"galA/sub"};
        ui::CollectionTransferSpec spec = {groups, gallery_paths};

        ui::FileOpJob job;
        CHECK(job.start_transfer_collection(v, spec, v, "galB",
                                            vault::TransferMode::Move,
                                            vault::CollisionPolicy::Fail, "Move test"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK(oc->done >= 1);  // at least 1.jpg + sub

        // Verify galB now holds the items
        bool has_img = false, has_sub = false;
        for (const auto* n : v.list("galB")) {
            if (n->name == "1.jpg") has_img = true;
            if (n->name == "sub") has_sub = true;
        }
        CHECK(has_img);
        CHECK(has_sub);

        // Verify galA no longer holds them
        bool galA_has_1 = false;
        for (const auto* n : v.list("galA")) {
            if (n->name == "1.jpg") galA_has_1 = true;
        }
        CHECK(!galA_has_1);
    }
    cleanup_dir(dir);
}

TEST(file_op_job_same_vault_copy_leaves_source)
{
    auto dir = fresh_dir("osv_dual_copy");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "galA", 1));
        REQUIRE(v.create_gallery("galB") == vault::VaultResult::Ok);

        std::vector<std::string> media_paths = {"galA/1.jpg"};
        std::vector<ui::ParentGroup> groups = ui::group_by_parent(media_paths);
        ui::CollectionTransferSpec spec = {groups, {}};

        ui::FileOpJob job;
        CHECK(job.start_transfer_collection(v, spec, v, "galB",
                                            vault::TransferMode::Copy,
                                            vault::CollisionPolicy::Fail, "Copy test"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);

        // Both sides hold the item
        bool galA_has = false, galB_has = false;
        for (const auto* n : v.list("galA")) {
            if (n->name == "1.jpg") galA_has = true;
        }
        for (const auto* n : v.list("galB")) {
            if (n->name == "1.jpg") galB_has = true;
        }
        CHECK(galA_has);
        CHECK(galB_has);
    }
    cleanup_dir(dir);
}

TEST(file_op_job_same_vault_transfer_with_suffix_collision_policy)
{
    auto dir = fresh_dir("osv_dual_suffix");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "galA", 1));
        REQUIRE(v.create_gallery("galA/sub") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("galB") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("galB/sub") == vault::VaultResult::Ok);  // collision exists

        std::vector<std::string> gallery_paths = {"galA/sub"};
        ui::CollectionTransferSpec spec = {{}, gallery_paths};

        ui::FileOpJob job;
        CHECK(job.start_transfer_collection(v, spec, v, "galB",
                                            vault::TransferMode::Move,
                                            vault::CollisionPolicy::Suffix, "Suffix test"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);

        // Verify galB/sub_2 exists
        bool has_sub_2 = false;
        for (const auto* n : v.list("galB")) {
            if (n->name == "sub_2") has_sub_2 = true;
        }
        CHECK(has_sub_2);
    }
    cleanup_dir(dir);
}
