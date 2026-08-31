#include "test_framework.h"

#include <filesystem>
#include <string>
#include <vector>

#include "ui/dup_model.h"
#include "ui/dup_scan.h"
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
               ("osv_dupref_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

// A review member that names a real node but carries deliberately WRONG
// span refs — refresh must overwrite them from the index.
ui::DupMember stale_member(std::string node_path)
{
    ui::DupMember m;
    m.node_path     = std::move(node_path);
    m.name          = "x";
    m.bytes         = 1;
    m.thumb.offset  = 42;
    m.thumb.length  = 42;
    m.thumb.record.fill(0xEE);
    return m;
}
} // namespace

TEST(dup_refresh_review_rereads_spans_from_index)
{
    TempVault tv("spans");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(1000, 1), "a.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(1000, 1), "b.png") == vault::VaultResult::Ok);

    ui::DupGroup g;
    g.members = {stale_member("a.png"), stale_member("b.png")};
    ui::DupReview review(std::vector<ui::DupGroup>{g});

    CHECK_EQ(ui::refresh_review_members(v, review), size_t{0});
    REQUIRE(review.groups().size() == 1);

    const vault::Vault& cv = v;
    const auto* a = cv.resolve_node("a.png");
    REQUIRE(a != nullptr);
    const auto& m0 = review.groups()[0].members[0];   // sorted order == insertion
    CHECK_EQ(m0.bytes, a->meta.orig_size);
    REQUIRE(m0.data.size() == 1);
    CHECK_EQ(m0.data[0].offset,  a->meta.data_offset);
    CHECK_EQ(m0.data[0].length,  a->meta.data_length);
    CHECK_EQ(m0.thumb.offset,    a->meta.thumb_offset);
    CHECK_EQ(m0.thumb.length,    a->meta.thumb_length);
}

TEST(dup_refresh_review_drops_vanished_member)
{
    TempVault tv("gone");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(500, 2), "a.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(500, 2), "b.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(500, 2), "c.png") == vault::VaultResult::Ok);

    ui::DupGroup g;
    g.members = {stale_member("a.png"), stale_member("b.png"), stale_member("c.png")};
    ui::DupReview review(std::vector<ui::DupGroup>{g});

    const std::vector<std::string> doomed{"c.png"};
    REQUIRE(vault::remove_media_batch(v, doomed, nullptr) == vault::VaultResult::Ok);

    CHECK_EQ(ui::refresh_review_members(v, review), size_t{1});
    REQUIRE(review.groups().size() == 1);
    CHECK_EQ(review.groups()[0].members.size(), size_t{2});
}

TEST(dup_refresh_review_drops_group_below_two)
{
    TempVault tv("shrink");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(300, 3), "a.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(300, 3), "b.png") == vault::VaultResult::Ok);

    ui::DupGroup g;
    g.members = {stale_member("a.png"), stale_member("b.png")};
    ui::DupReview review(std::vector<ui::DupGroup>{g});

    const std::vector<std::string> doomed{"b.png"};
    REQUIRE(vault::remove_media_batch(v, doomed, nullptr) == vault::VaultResult::Ok);

    CHECK_EQ(ui::refresh_review_members(v, review), size_t{1});
    CHECK(review.groups().empty());
}
