#include "test_framework.h"

#include <filesystem>
#include <string>
#include <vector>

#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kFastKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

namespace {
struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_rmb_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    [[nodiscard]] std::string str() const { return path.string(); }
};
} // namespace

TEST(remove_batch_deletes_only_named_media)
{
    TempVault tv("basic");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("a") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("a", pattern(1000, 1), "one.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("a", pattern(1000, 1), "two.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("",  pattern(2000, 2), "root.png") == vault::VaultResult::Ok);

    const std::vector<std::string> doomed{"a/two.png", "root.png"};
    vault::RemoveBatchStats stats;
    REQUIRE(vault::remove_media_batch(v, doomed, &stats) == vault::VaultResult::Ok);
    CHECK_EQ(stats.removed, size_t{2});
    CHECK_EQ(stats.missing, size_t{0});
    CHECK_EQ(v.list("a").size(), size_t{1});
    CHECK_EQ(v.list("a")[0]->name, std::string("one.png"));
    CHECK_EQ(v.list("").size(), size_t{1});  // only gallery "a" remains at root
}

TEST(remove_batch_counts_missing_and_non_media)
{
    TempVault tv("missing");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", pattern(500, 3), "keep.png") == vault::VaultResult::Ok);

    const std::vector<std::string> doomed{"g/nope.png", "g"};  // absent + a gallery
    vault::RemoveBatchStats stats;
    REQUIRE(vault::remove_media_batch(v, doomed, &stats) == vault::VaultResult::Ok);
    CHECK_EQ(stats.removed, size_t{0});
    CHECK_EQ(stats.missing, size_t{2});
    CHECK_EQ(v.list("g").size(), size_t{1});  // untouched
}

TEST(remove_batch_survives_reopen)
{
    TempVault tv("reopen");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(100, 4), "a.png") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(100, 4), "b.png") == vault::VaultResult::Ok);
        const std::vector<std::string> doomed{"b.png"};
        REQUIRE(vault::remove_media_batch(v, doomed, nullptr) == vault::VaultResult::Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    REQUIRE(v.list("").size() == 1);
    CHECK_EQ(v.list("")[0]->name, std::string("a.png"));
}

TEST(remove_batch_locked_vault_refused)
{
    TempVault tv("locked");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
                == vault::VaultResult::Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);  // still locked
    const std::vector<std::string> doomed{"a.png"};
    vault::RemoveBatchStats stats{.removed = 99, .missing = 99};
    CHECK(vault::remove_media_batch(v, doomed, &stats) == vault::VaultResult::Locked);
    CHECK_EQ(stats.removed, size_t{0});  // stats zeroed on failure (prune_tags contract)
    CHECK_EQ(stats.missing, size_t{0});
}

TEST(remove_nodes_batch_mixed_media_and_gallery)
{
    TempVault tv("nodes_mixed");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("g/sub") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g/sub", pattern(500, 1), "deep.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", pattern(500, 2), "in_g.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(500, 3), "keep.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(500, 4), "doomed.png") == vault::VaultResult::Ok);

    // One batch: a media node AND a whole gallery subtree.
    const std::vector<std::string> doomed{"doomed.png", "g"};
    vault::RemoveBatchStats stats;
    REQUIRE(vault::remove_nodes_batch(v, doomed, &stats) == vault::VaultResult::Ok);
    CHECK_EQ(stats.removed, size_t{2});
    CHECK_EQ(stats.missing, size_t{0});
    CHECK_EQ(v.list("").size(), size_t{1});           // only keep.png left
    CHECK_EQ(v.list("")[0]->name, std::string("keep.png"));
    CHECK(v.resolve_node("g") == nullptr);            // subtree gone
    CHECK(v.resolve_node("g/sub/deep.png") == nullptr);
}

TEST(remove_nodes_batch_counts_missing)
{
    TempVault tv("nodes_missing");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(100, 1), "a.png") == vault::VaultResult::Ok);

    const std::vector<std::string> doomed{"a.png", "ghost.png", "no/such/gallery"};
    vault::RemoveBatchStats stats;
    REQUIRE(vault::remove_nodes_batch(v, doomed, &stats) == vault::VaultResult::Ok);
    CHECK_EQ(stats.removed, size_t{1});
    CHECK_EQ(stats.missing, size_t{2});
    CHECK_EQ(v.list("").size(), size_t{0});
}

TEST(remove_nodes_batch_survives_reopen)
{
    TempVault tv("nodes_reopen");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", pattern(100, 1), "x.png") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(100, 2), "keep.png") == vault::VaultResult::Ok);
        const std::vector<std::string> doomed{"g"};
        REQUIRE(vault::remove_nodes_batch(v, doomed, nullptr) == vault::VaultResult::Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    REQUIRE(v.list("").size() == 1);
    CHECK_EQ(v.list("")[0]->name, std::string("keep.png"));
}

TEST(remove_nodes_batch_locked_refused)
{
    TempVault tv("nodes_locked");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
                == vault::VaultResult::Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);  // still locked
    const std::vector<std::string> doomed{"a.png"};
    vault::RemoveBatchStats stats{.removed = 99, .missing = 99};
    CHECK(vault::remove_nodes_batch(v, doomed, &stats) == vault::VaultResult::Locked);
    CHECK_EQ(stats.removed, size_t{0});   // stats zeroed on failure
    CHECK_EQ(stats.missing, size_t{0});
}

TEST(remove_nodes_batch_root_path_is_missing_not_erased)
{
    TempVault tv("nodes_root");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(100, 1), "a.png") == vault::VaultResult::Ok);
    const std::vector<std::string> doomed{""};   // the root is not deletable
    vault::RemoveBatchStats stats;
    REQUIRE(vault::remove_nodes_batch(v, doomed, &stats) == vault::VaultResult::Ok);
    CHECK_EQ(stats.removed, size_t{0});
    CHECK_EQ(stats.missing, size_t{1});
    CHECK_EQ(v.list("").size(), size_t{1});
}
