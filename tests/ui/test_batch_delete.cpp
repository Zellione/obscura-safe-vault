#include "test_framework.h"

#include <filesystem>
#include <string>
#include <vector>

#include "ui/batch_delete.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kFastKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> blob(size_t n)
{
    return std::vector<uint8_t>(n, 0xAB);
}

namespace {
struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_bdel_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

TEST(prune_drops_descendants_of_selected_gallery)
{
    const std::vector<std::string> in{"g", "g/a.png", "g/sub/b.png", "other.png"};
    const auto out = ui::prune_descendant_paths(in);
    REQUIRE(out.size() == 2);
    CHECK_EQ(out[0], std::string("g"));
    CHECK_EQ(out[1], std::string("other.png"));
}

TEST(prune_respects_path_component_boundary)
{
    // "g2" must NOT be treated as a descendant of "g" or of "g2x".
    const std::vector<std::string> in{"g", "g2", "g2x/y.png"};
    const auto out = ui::prune_descendant_paths(in);
    REQUIRE(out.size() == 3);   // nothing dropped
}

TEST(prune_keeps_order_and_handles_empty)
{
    CHECK(ui::prune_descendant_paths({}).empty());
    const std::vector<std::string> in{"b.png", "a.png"};
    const auto out = ui::prune_descendant_paths(in);
    REQUIRE(out.size() == 2);
    CHECK_EQ(out[0], std::string("b.png"));   // input order preserved
    CHECK_EQ(out[1], std::string("a.png"));
}

TEST(summarize_mixed_selection)
{
    TempVault tv("sum");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("g/sub") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", blob(1000), "one.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g/sub", blob(2000), "two.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", blob(4000), "solo.png") == vault::VaultResult::Ok);

    const std::vector<std::string> paths{"g", "solo.png", "ghost.png"};
    const auto s = ui::summarize_batch_delete(v, paths);
    CHECK_EQ(s.top_level, 2);            // ghost.png does not resolve
    CHECK_EQ(s.galleries, 2);            // g + g/sub
    CHECK_EQ(s.images, 3);               // one.png, two.png, solo.png
    CHECK_EQ(s.videos, 0);
    CHECK_EQ(s.bytes, uint64_t{7000});
    // item_total: gallery subtree (2 images + 1 sub-gallery + g itself = 4) + solo.png
    CHECK_EQ(s.item_total, 5);
}

TEST(counts_line_omits_zero_categories)
{
    ui::BatchDeleteSummary s{.top_level = 3, .galleries = 2, .images = 7,
                             .videos = 0, .bytes = 1000, .item_total = 10};
    const std::string line = ui::batch_delete_counts_line(s);
    CHECK(line.find("2 galleries") != std::string::npos);
    CHECK(line.find("7 images") != std::string::npos);
    CHECK(line.find("video") == std::string::npos);
    CHECK(line.find("B") != std::string::npos);   // format_size(1000) = "1000 B"

    ui::BatchDeleteSummary one{.top_level = 1, .galleries = 1, .images = 1,
                               .videos = 1, .bytes = 0, .item_total = 3};
    const std::string l1 = ui::batch_delete_counts_line(one);
    CHECK(l1.find("1 gallery") != std::string::npos);   // singular
    CHECK(l1.find("1 image") != std::string::npos);
    CHECK(l1.find("1 video") != std::string::npos);
}
