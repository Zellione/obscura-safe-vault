#include "test_framework.h"
#include "ui/zip_test_helpers.h"
#include "vault/staging.h"
#include "vault/vault.h"

using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_vault;

TEST(uncommitted_staged_chunks_are_clean_orphans)
{
    const auto dir  = fresh_dir("iosplit");
    const auto path = dir / "a.osv";
    uint64_t   wasted_before = 0;
    {
        vault::Vault v;
        make_vault(v, path);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", fake_jpeg(1), "committed.jpg") == vault::VaultResult::Ok);
        wasted_before = v.wasted_bytes();
        auto s1 = vault::stage_image(v, fake_jpeg(2), "lost1.jpg");
        auto s2 = vault::stage_image(v, fake_jpeg(3), "lost2.jpg");
        REQUIRE(s1.status == vault::VaultResult::Ok);
        REQUIRE(s2.status == vault::VaultResult::Ok);
        // NO attach, NO commit — Vault dtor closes the file like a crash would.
    }
    vault::Vault v2;
    REQUIRE(ziptest::open_vault(path, v2));   // helper: open+unlock with test creds
    CHECK_EQ(static_cast<int>(v2.list("g").size()), 1);          // only the committed one
    CHECK(v2.wasted_bytes() > wasted_before);                    // orphans visible
    CHECK(v2.compact() == vault::VaultResult::Ok);               // and reclaimable
    CHECK_EQ(static_cast<int>(v2.list("g").size()), 1);
    cleanup_dir(dir);
}
