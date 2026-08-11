#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/staging.h"
#include "vault/transfer.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

namespace {

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_xfertags_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

}  // namespace

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 31 + seed);
    return v;
}

TEST(effective_tags_unions_own_and_ancestors) {
    using enum vault::VaultResult;
    TempVault tv("efftags");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("a") == Ok);
    REQUIRE(v.create_gallery("a/b") == Ok);
    REQUIRE(v.add_tag("", "global") == Ok);      // root tag cascades
    REQUIRE(v.add_tag("a", "series") == Ok);
    REQUIRE(v.add_tag("a/b", "chapter") == Ok);
    auto img = pattern(64, 1);
    vault::StagedThumb no_thumb;   // precomputed empty thumb: no decode needed
    REQUIRE(vault::add_image_prestaged(v, "a/b", img, "x.jpg", no_thumb, 0)
            == Ok);
    REQUIRE(v.add_tag("a/b/x.jpg", "OWN") == Ok);
    REQUIRE(v.add_tag("a/b/x.jpg", "Series") == Ok);  // ci-dupe of ancestor

    auto eff = vault::effective_tags(v, "a/b/x.jpg");
    // own first (own casing wins), then inherited minus ci-duplicates
    REQUIRE(eff.size() == 4);
    CHECK(eff[0] == "OWN");
    CHECK(eff[1] == "Series");     // node's casing kept; ancestor "series" deduped
    // remaining two are "global" and "chapter" in root->parent order
    CHECK(std::ranges::find(eff, "global")  != eff.end());
    CHECK(std::ranges::find(eff, "chapter") != eff.end());
}

TEST(effective_tags_locked_or_missing_is_empty) {
    using enum vault::VaultResult;
    TempVault tv("locked");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("a") == Ok);
    REQUIRE(v.add_tag("a", "test") == Ok);

    // Locked vault -> empty
    vault::Vault locked;
    REQUIRE(vault::Vault::open(tv.str(), locked) == Ok);
    auto eff = vault::effective_tags(locked, "a");
    CHECK(eff.empty());

    // Unlocked but missing path -> empty
    REQUIRE(locked.unlock(bytes("pw"), {}) == Ok);
    auto eff2 = vault::effective_tags(locked, "nonexistent");
    CHECK(eff2.empty());
}
