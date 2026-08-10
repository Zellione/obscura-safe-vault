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

// Combine policy on Move: files skip, same-named sub-galleries recurse,
// source shell is deleted once empty (combine semantics verbatim).
TEST(transfer_gallery_combine_merges_into_existing)
{
    using enum vault::VaultResult;
    TempVault sa("s5"), da("d5");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("Trip/Sub") == Ok);
    REQUIRE(src.add_image("Trip", blob(2000, 1), "a.jpg") == Ok);
    REQUIRE(src.add_image("Trip", blob(2000, 2), "b.jpg") == Ok);
    REQUIRE(src.add_image("Trip/Sub", blob(2000, 3), "c.jpg") == Ok);
    REQUIRE(dst.create_gallery("Trip/Sub") == Ok);
    REQUIRE(dst.add_image("Trip", blob(10, 4), "b.jpg") == Ok);   // file collision

    vault::TransferTally t;
    REQUIRE(vault::transfer_gallery(src, "Trip", dst, "", vault::TransferMode::Move,
                                    {.tally = &t,
                                     .policy = vault::CollisionPolicy::Combine}) == Ok);
    CHECK(find_child(dst, "Trip", "a.jpg") != nullptr);
    CHECK(find_child(dst, "Trip/Sub", "c.jpg") != nullptr);   // recursed merge
    CHECK_EQ(t.done, 2);       // a.jpg + c.jpg
    CHECK_EQ(t.skipped, 1);    // b.jpg collision
    // Skipped b.jpg keeps the source gallery alive (partial-merge contract).
    CHECK(find_child(src, "Trip", "b.jpg") != nullptr);
}

// Combine policy when the same-named destination child is a FILE, not a
// gallery: nothing to merge into -> AlreadyExists (documented fallback).
TEST(transfer_gallery_combine_into_media_child_fails)
{
    using enum vault::VaultResult;
    TempVault sa("s6"), da("d6");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("X") == Ok);
    REQUIRE(dst.add_image("", blob(10, 1), "X") == Ok);   // media named "X"
    CHECK(vault::transfer_gallery(src, "X", dst, "", vault::TransferMode::Move,
                                  {.policy = vault::CollisionPolicy::Combine}) == AlreadyExists);
}

TEST(colliding_galleries_lists_only_clashes)
{
    using enum vault::VaultResult;
    TempVault da("d7");
    vault::Vault dst;
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(dst.create_gallery("Trip") == Ok);
    REQUIRE(dst.add_image("", blob(10, 1), "Pics") == Ok);   // media child counts too

    const std::vector<std::string> paths = {"old/Trip", "old/Fresh", "Pics"};
    const auto hits = vault::colliding_galleries(dst, "", paths);
    REQUIRE(hits.size() == 2u);
    CHECK_EQ(hits[0], std::string("Trip"));
    CHECK_EQ(hits[1], std::string("Pics"));
    CHECK(vault::colliding_galleries(dst, "Trip", paths).empty());
}

// transfer_galleries forwards the policy to every subtree: with Suffix, the
// colliding one renames and the clean one keeps its name — both transfer.
TEST(transfer_galleries_forwards_policy)
{
    using enum vault::VaultResult;
    TempVault sa("s8"), da("d8");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("A") == Ok);
    REQUIRE(src.create_gallery("B") == Ok);
    REQUIRE(dst.create_gallery("A") == Ok);   // only A collides

    const auto t = vault::transfer_galleries(src, {"A", "B"}, dst, "",
                                             vault::TransferMode::Move, nullptr,
                                             vault::CollisionPolicy::Suffix);
    CHECK_EQ(t.done, 2);
    CHECK(find_child(dst, "", "A_2") != nullptr);
    CHECK(find_child(dst, "", "B") != nullptr);
}
