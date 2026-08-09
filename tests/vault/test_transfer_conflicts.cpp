#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/transfer.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// Internal linkage: several test files each define their own `TempVault`
// with a DIFFERENT layout. At namespace scope those are one-definition-rule
// violations — the member functions are implicitly inline, so the linker keeps
// a single copy and silently discards the rest.
namespace {

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_cf_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

}  // namespace

static std::vector<uint8_t> blob(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 23 + seed);
    return v;
}

static const vault::IndexNode* find_child(const vault::Vault& v, std::string_view gallery,
                                          std::string_view name)
{
    for (const auto* c : v.list(gallery)) if (c->name == name) return c;
    return nullptr;
}

// Suffix policy: the colliding subtree lands under the first free name_2.
TEST(transfer_gallery_suffix_renames_on_collision)
{
    using enum vault::VaultResult;
    TempVault sa("s1"), da("d1");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("Trip") == Ok);
    REQUIRE(src.add_image("Trip", blob(2000, 1), "a.jpg") == Ok);
    REQUIRE(dst.create_gallery("Trip") == Ok);   // the collision

    REQUIRE(vault::transfer_gallery(src, "Trip", dst, "", vault::TransferMode::Move,
                                    {.policy = vault::CollisionPolicy::Suffix}) == Ok);
    REQUIRE(find_child(dst, "", "Trip_2") != nullptr);
    CHECK(find_child(dst, "Trip_2", "a.jpg") != nullptr);
    CHECK(find_child(src, "", "Trip") == nullptr);   // Move pruned the source
}

// Taken suffixes are probed upward: Trip and Trip_2 exist -> Trip_3.
TEST(transfer_gallery_suffix_probes_past_taken_names)
{
    using enum vault::VaultResult;
    TempVault sa("s2"), da("d2");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("Trip") == Ok);
    REQUIRE(dst.create_gallery("Trip") == Ok);
    REQUIRE(dst.create_gallery("Trip_2") == Ok);

    REQUIRE(vault::transfer_gallery(src, "Trip", dst, "", vault::TransferMode::Move,
                                    {.policy = vault::CollisionPolicy::Suffix}) == Ok);
    CHECK(find_child(dst, "", "Trip_3") != nullptr);
}

// A max-length multi-byte name is trimmed on a codepoint boundary so
// name + "_2" still fits MAX_NODE_NAME_BYTES and stays a safe node name.
TEST(transfer_gallery_suffix_trims_long_name_on_codepoint_boundary)
{
    using enum vault::VaultResult;
    TempVault sa("s3"), da("d3");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    std::string longname;                       // 85 x "日" = 255 bytes exactly
    for (int i = 0; i < 85; ++i) longname += "\xE6\x97\xA5";
    REQUIRE(src.create_gallery(longname) == Ok);
    REQUIRE(dst.create_gallery(longname) == Ok);

    REQUIRE(vault::transfer_gallery(src, longname, dst, "", vault::TransferMode::Move,
                                    {.policy = vault::CollisionPolicy::Suffix}) == Ok);
    // 255 - len("_2") = 253 -> trimmed to 84 codepoints (252 bytes) + "_2".
    std::string expect;
    for (int i = 0; i < 84; ++i) expect += "\xE6\x97\xA5";
    expect += "_2";
    CHECK(find_child(dst, "", expect) != nullptr);
}

// Default policy stays Fail: unchanged AlreadyExists.
TEST(transfer_gallery_default_policy_still_fails)
{
    using enum vault::VaultResult;
    TempVault sa("s4"), da("d4");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("Dupe") == Ok);
    REQUIRE(dst.create_gallery("Dupe") == Ok);
    CHECK(vault::transfer_gallery(src, "Dupe", dst, "", vault::TransferMode::Move) == AlreadyExists);
}
