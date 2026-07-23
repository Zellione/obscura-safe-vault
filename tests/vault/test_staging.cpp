#include "test_framework.h"
#include "ui/zip_test_helpers.h"
#include "vault/staging.h"
#include "vault/vault.h"

#include <thread>

using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_vault;

TEST(stage_then_attach_equals_add_image)
{
    const auto dir = fresh_dir("staging1");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);

    auto staged = vault::stage_image(v, fake_jpeg(7), "x.jpg");
    REQUIRE(staged.status == vault::VaultResult::Ok);
    CHECK(staged.node.meta.data_length > 0);
    REQUIRE(vault::attach_staged(v, "g", std::move(staged.node)) == vault::VaultResult::Ok);

    const auto kids = v.list("g");
    REQUIRE(kids.size() == 1);
    crypto::SecureBytes out;
    CHECK(v.read_image(*kids[0], out) == vault::VaultResult::Ok);   // readable pre-commit
    cleanup_dir(dir);
}

TEST(attach_staged_rejects_collision_and_orphans_chunks)
{
    const auto dir = fresh_dir("staging2");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", fake_jpeg(1), "x.jpg") == vault::VaultResult::Ok);

    auto staged = vault::stage_image(v, fake_jpeg(2), "x.jpg");   // staging can't see the tree
    REQUIRE(staged.status == vault::VaultResult::Ok);
    CHECK(vault::attach_staged(v, "g", std::move(staged.node))
          == vault::VaultResult::AlreadyExists);
    CHECK_EQ(static_cast<int>(v.list("g").size()), 1);
    cleanup_dir(dir);
}

TEST(ensure_gallery_path_creates_without_commit)
{
    const auto dir = fresh_dir("staging3");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(vault::ensure_gallery_path(v, "a/b/c") == vault::VaultResult::Ok);
    REQUIRE(vault::ensure_gallery_path(v, "a/b/c") == vault::VaultResult::Ok);  // idempotent
    CHECK_EQ(static_cast<int>(v.list("a/b").size()), 1);
    cleanup_dir(dir);
}

// The concurrency contract: many stage_image calls from a worker thread while
// the main thread reads an existing image. Tree untouched by the worker; the
// write mutex serialises appends. This test is the TSAN witness for Phase 50's
// core claim, and it retargets Task 1's bridge test.
TEST(stage_image_is_safe_beside_main_thread_reads)
{
    const auto dir = fresh_dir("staging4");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", fake_jpeg(1), "1.jpg") == vault::VaultResult::Ok);
    const auto* node = v.list("g").at(0);

    std::vector<vault::StagedNode> staged;
    std::thread worker([&] {
        for (int i = 0; i < 40; ++i)
            staged.push_back(vault::stage_image(v, fake_jpeg(static_cast<uint8_t>(i)),
                                                "s" + std::to_string(i) + ".jpg"));
    });
    for (int i = 0; i < 200; ++i) {
        crypto::SecureBytes out;
        CHECK(v.read_image(*node, out) == vault::VaultResult::Ok);
    }
    worker.join();
    for (auto& s : staged) {
        REQUIRE(s.status == vault::VaultResult::Ok);
        REQUIRE(vault::attach_staged(v, "g", std::move(s.node)) == vault::VaultResult::Ok);
    }
    CHECK_EQ(static_cast<int>(v.list("g").size()), 41);
    cleanup_dir(dir);
}
