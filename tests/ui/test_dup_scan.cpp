#include "test_framework.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

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
               ("osv_dsc_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

TEST(dup_scan_items_cover_whole_tree)
{
    TempVault tv("collect");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("a/b") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("",    pattern(100, 1), "root.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("a",   pattern(200, 2), "mid.png")  == vault::VaultResult::Ok);
    REQUIRE(v.add_image("a/b", pattern(300, 3), "leaf.png") == vault::VaultResult::Ok);

    auto items = ui::collect_scan_items(v);
    REQUIRE(items.size() == 3);
    auto find = [&](const std::string& p) {
        return std::ranges::find_if(items, [&](const ui::DupScanItem& it) {
            return it.node_path == p;
        });
    };
    REQUIRE(find("root.png") != items.end());
    REQUIRE(find("a/mid.png") != items.end());
    REQUIRE(find("a/b/leaf.png") != items.end());

    const auto& leaf = *find("a/b/leaf.png");
    CHECK_EQ(leaf.name, std::string("leaf.png"));
    CHECK_EQ(leaf.parent_path, std::string("a/b"));
    CHECK(!leaf.is_video);
    CHECK_EQ(leaf.bytes, uint64_t{300});
    REQUIRE(leaf.data_spans.size() == 1);
    CHECK(leaf.data_spans[0].second > 0);   // on-disk chunk length recorded
}
