#include "test_framework.h"

#include "ui/zip_test_helpers.h"   // make_vault, fake_jpeg, fresh_dir, cleanup_dir
#include "vault/op_progress.h"
#include "vault/transfer.h"
#include "vault/vault.h"

using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_vault;

namespace {
// Add `n` fake images named 1.jpg..n.jpg into a fresh gallery `g`. Returns true
// iff every create/add succeeded (the caller REQUIREs it).
[[nodiscard]] bool seed_images(vault::Vault& v, const char* g, int n)
{
    if (v.create_gallery(g) != vault::VaultResult::Ok) return false;
    for (int i = 1; i <= n; ++i)
        if (v.add_image(g, fake_jpeg(static_cast<uint8_t>(i)),
                        std::to_string(i) + ".jpg") != vault::VaultResult::Ok)
            return false;
    return true;
}
} // namespace

// transfer_images reports total up front and bumps done per file; the tally
// matches the moved count and the destination really holds them.
TEST(transfer_images_progress_counts)
{
    auto dir = fresh_dir("osv_xferprog");
    {
        vault::Vault src, dst;
        make_vault(src, dir / "s.osv");
        make_vault(dst, dir / "d.osv");
        REQUIRE(seed_images(src, "a", 3));
        REQUIRE(dst.create_gallery("b") == vault::VaultResult::Ok);

        vault::OpProgress prog;
        const auto tally = vault::transfer_images(src, "a", {"1.jpg", "2.jpg", "3.jpg"},
                                                  dst, "b", vault::TransferMode::Move, &prog);
        CHECK_EQ(tally.done, 3);
        CHECK_EQ(tally.failed, 0);
        CHECK_EQ(prog.total.load(), 3);
        CHECK_EQ(prog.done.load(), 3);
        CHECK_EQ(dst.list("b").size(), static_cast<size_t>(3));
        CHECK_EQ(src.list("a").size(), static_cast<size_t>(0));   // Move emptied the source
    }
    cleanup_dir(dir);
}

// A cancel set before the loop starts stops it immediately: nothing is moved and
// the source is left fully intact (a clean no-op partial).
TEST(transfer_images_cancel_before_start_is_noop)
{
    auto dir = fresh_dir("osv_xfercancel");
    {
        vault::Vault src, dst;
        make_vault(src, dir / "s.osv");
        make_vault(dst, dir / "d.osv");
        REQUIRE(seed_images(src, "a", 3));
        REQUIRE(dst.create_gallery("b") == vault::VaultResult::Ok);

        vault::OpProgress prog;
        prog.cancel.store(true);
        const auto tally = vault::transfer_images(src, "a", {"1.jpg", "2.jpg", "3.jpg"},
                                                  dst, "b", vault::TransferMode::Move, &prog);
        CHECK_EQ(tally.done, 0);
        CHECK_EQ(prog.done.load(), 0);
        CHECK_EQ(src.list("a").size(), static_cast<size_t>(3));   // source untouched
        CHECK_EQ(dst.list("b").size(), static_cast<size_t>(0));
    }
    cleanup_dir(dir);
}

// transfer_gallery sets total to the subtree's media count and copies them all.
TEST(transfer_gallery_progress_counts_media)
{
    auto dir = fresh_dir("osv_gxferprog");
    {
        vault::Vault src, dst;
        make_vault(src, dir / "s.osv");
        make_vault(dst, dir / "d.osv");
        REQUIRE(seed_images(src, "album", 4));

        vault::OpProgress prog;
        const auto r = vault::transfer_gallery(src, "album", dst, "",
                                               vault::TransferMode::Move, &prog);
        CHECK(r == vault::VaultResult::Ok);
        CHECK_EQ(prog.total.load(), 4);
        CHECK_EQ(prog.done.load(), 4);
        CHECK_EQ(dst.list("album").size(), static_cast<size_t>(4));
        CHECK_EQ(src.list("").size(), static_cast<size_t>(0));   // moved out of source
    }
    cleanup_dir(dir);
}

// A cancelled gallery Move leaves the SOURCE intact (recoverable duplicate, never
// a loss) even though some files may already be in the destination.
TEST(transfer_gallery_cancel_leaves_source_intact)
{
    auto dir = fresh_dir("osv_gxfercancel");
    {
        vault::Vault src, dst;
        make_vault(src, dir / "s.osv");
        make_vault(dst, dir / "d.osv");
        REQUIRE(seed_images(src, "album", 4));

        vault::OpProgress prog;
        prog.cancel.store(true);   // cancel before any file copies
        const auto r = vault::transfer_gallery(src, "album", dst, "",
                                               vault::TransferMode::Move, &prog);
        CHECK(r == vault::VaultResult::Ok);
        CHECK_EQ(src.list("album").size(), static_cast<size_t>(4));   // NOT removed on cancel
    }
    cleanup_dir(dir);
}
