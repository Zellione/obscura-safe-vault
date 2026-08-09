#include "test_framework.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
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
               ("osv_tol_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 31 + seed);
    return v;
}

// Find a media (image or video) node by name in a gallery; nullptr if absent.
static const vault::IndexNode* find_image(const vault::Vault& v, std::string_view gallery,
                                          std::string_view name)
{
    for (const auto* c : v.list(gallery))
        if (c->is_media() && c->name == name) return c;
    return nullptr;
}

// Flip one ciphertext byte of `node`'s data chunk on disk, so the next read
// fails Poly1305 (AuthFailed). Chunk layout: nonce[24] | cipher | tag[16]
// — +30 lands inside the ciphertext for any chunk > 6 plaintext bytes.
static void tamper_data_chunk(const fs::path& vault_path, uint64_t data_offset)
{
    std::fstream f(vault_path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekg(static_cast<std::streamoff>(data_offset) + 30);
    char b = 0;
    f.read(&b, 1);
    f.seekp(static_cast<std::streamoff>(data_offset) + 30);
    b = static_cast<char>(b ^ 0x01);
    f.write(&b, 1);
}

}  // namespace

// record_failure caps storage at MAX_TRANSFER_FAILURES but keeps counting.
TEST(record_failure_caps_entries)
{
    vault::TransferTally t;
    for (int i = 0; i < 150; ++i)
        vault::record_failure(t, std::to_string(i), vault::VaultResult::IoError,
                              vault::TransferFailure::Stage::Write);
    CHECK_EQ(t.failed, 150);
    CHECK_EQ(t.failures.size(), vault::MAX_TRANSFER_FAILURES);
    CHECK_EQ(t.failures.front().path, std::string("0"));
}

// One corrupt file and one destination collision must not stop the others; each
// failure carries the source path, code, and stage.
TEST(transfer_images_records_failures_and_continues)
{
    using enum vault::VaultResult;
    TempVault sa("s1"), da("d1");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("g") == Ok);
    for (const char* n : {"a.jpg", "b.jpg", "c.jpg", "d.jpg"})
        REQUIRE(src.add_image("g", pattern(4000, 1), n) == Ok);

    // b.jpg: corrupt at source. c.jpg: already present at destination.
    tamper_data_chunk(sa.path, find_image(src, "g", "b.jpg")->meta.data_offset);
    REQUIRE(dst.add_image("", pattern(10, 2), "c.jpg") == Ok);

    const auto tally = vault::transfer_images(
        src, "g", {"a.jpg", "b.jpg", "c.jpg", "d.jpg"}, dst, "",
        vault::TransferMode::Move);

    CHECK_EQ(tally.done, 2);
    CHECK_EQ(tally.failed, 1);      // only the corrupt b.jpg is a FAILURE now
    CHECK_EQ(tally.skipped, 1);     // the c.jpg collision is a SKIP, not a failure
    REQUIRE(tally.failures.size() == 1u);
    CHECK_EQ(tally.failures[0].path, std::string("g/b.jpg"));
    CHECK(tally.failures[0].code == AuthFailed);
    CHECK(tally.failures[0].stage == vault::TransferFailure::Stage::Read);
    // Failed AND skipped files stay in the source; moved ones are gone.
    CHECK(find_image(src, "g", "b.jpg") != nullptr);
    CHECK(find_image(src, "g", "c.jpg") != nullptr);
    CHECK(find_image(src, "g", "a.jpg") == nullptr);
    CHECK(find_image(dst, "", "d.jpg") != nullptr);
}

// stage under a safe name, rename the UNATTACHED node, then attach:
// attach_staged checks only sibling collision, not name safety — exactly the
// trust gap a foreign vault exploits.
static bool forge_bad_named_image(vault::Vault& v, const std::string& gallery,
                                  const std::string& bad_name)
{
    auto staged = vault::stage_image(v, pattern(500, 42), "tmp_safe.jpg");
    if (staged.status != vault::VaultResult::Ok) return false;
    staged.node.name = bad_name;
    return vault::attach_staged(v, gallery, std::move(staged.node))
           == vault::VaultResult::Ok;
}

// A corrupt file inside a moved gallery no longer aborts the transfer: the rest
// moves, the bad file + its ancestors survive at the source, and the failure is
// recorded. Also proves per-file Move + empty-gallery pruning.
TEST(transfer_gallery_tolerates_corrupt_file)
{
    using enum vault::VaultResult;
    TempVault sa("s2"), da("d2");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("album/sub") == Ok);
    REQUIRE(src.add_image("album", pattern(3000, 1), "ok1.jpg") == Ok);
    REQUIRE(src.add_image("album/sub", pattern(3000, 2), "bad.jpg") == Ok);
    REQUIRE(src.add_image("album/sub", pattern(3000, 3), "ok2.jpg") == Ok);
    tamper_data_chunk(sa.path, find_image(src, "album/sub", "bad.jpg")->meta.data_offset);

    vault::TransferTally tally;
    REQUIRE(vault::transfer_gallery(src, "album", dst, "", vault::TransferMode::Move,
                                    {.tally = &tally}) == Ok);

    CHECK_EQ(tally.done, 2);
    CHECK_EQ(tally.failed, 1);
    REQUIRE(tally.failures.size() == 1u);
    CHECK_EQ(tally.failures[0].path, std::string("album/sub/bad.jpg"));
    CHECK(tally.failures[0].code == AuthFailed);
    // Destination holds the two good files.
    CHECK(find_image(dst, "album", "ok1.jpg") != nullptr);
    CHECK(find_image(dst, "album/sub", "ok2.jpg") != nullptr);
    // Source residue: bad.jpg + its ancestor galleries survive, nothing else.
    CHECK(find_image(src, "album/sub", "bad.jpg") != nullptr);
    CHECK(find_image(src, "album", "ok1.jpg") == nullptr);
    CHECK(find_image(src, "album/sub", "ok2.jpg") == nullptr);
}

// A fully successful Move still removes the whole source subtree (pruning).
TEST(transfer_gallery_full_move_prunes_source)
{
    using enum vault::VaultResult;
    TempVault sa("s3"), da("d3");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("a/b/c") == Ok);      // includes an EMPTY leaf
    REQUIRE(src.add_image("a/b", pattern(2000, 1), "x.jpg") == Ok);

    vault::TransferTally tally;
    REQUIRE(vault::transfer_gallery(src, "a", dst, "", vault::TransferMode::Move,
                                    {.tally = &tally}) == Ok);
    CHECK_EQ(tally.failed, 0);
    CHECK(src.resolve_node("a") == nullptr);          // fully pruned
    CHECK(dst.resolve_node("a/b/c") != nullptr);      // empty leaf recreated
    CHECK(find_image(dst, "a/b", "x.jpg") != nullptr);
}

// An unsafe-NAMED sub-gallery (foreign vault): ONE failure entry for the
// gallery, its media counted failed but not individually recorded, siblings
// still transfer, and the skipped branch fully survives at the source.
TEST(transfer_gallery_skips_bad_named_subbranch)
{
    using enum vault::VaultResult;
    TempVault sa("s4"), da("d4");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("top") == Ok);
    REQUIRE(src.add_image("top", pattern(1000, 1), "keep.jpg") == Ok);
    // Forge gallery "bad<g>" under top, holding two images.
    REQUIRE(vault::attach_staged(src, "top", vault::IndexNode::gallery("bad<g>")) == Ok);
    for (const char* n : {"in1.jpg", "in2.jpg"}) {
        auto st = vault::stage_image(src, pattern(1000, 7), n);
        REQUIRE(st.status == Ok);
        REQUIRE(vault::attach_staged(src, "top/bad<g>", std::move(st.node)) == Ok);
    }

    vault::TransferTally tally;
    REQUIRE(vault::transfer_gallery(src, "top", dst, "", vault::TransferMode::Move,
                                    {.tally = &tally}) == Ok);
    CHECK_EQ(tally.done, 1);                       // keep.jpg
    CHECK_EQ(tally.failed, 3);                     // the gallery + its 2 images
    REQUIRE(tally.failures.size() == 1u);          // ONE entry: the gallery itself
    CHECK_EQ(tally.failures[0].path, std::string("top/bad<g>"));
    CHECK(tally.failures[0].code == InvalidArg);
    // Skipped branch intact at source (including its media), moved file gone.
    REQUIRE(src.resolve_node("top/bad<g>") != nullptr);
    CHECK_EQ(src.list("top/bad<g>").size(), static_cast<size_t>(2));
    CHECK(find_image(src, "top", "keep.jpg") == nullptr);
}

// A forged bad-named MEDIA file fails with InvalidArg/Write and the rest moves.
TEST(transfer_gallery_records_bad_named_media)
{
    using enum vault::VaultResult;
    TempVault sa("s5"), da("d5");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("g") == Ok);
    REQUIRE(src.add_image("g", pattern(1000, 1), "fine.jpg") == Ok);
    REQUIRE(forge_bad_named_image(src, "g", "trailing dot."));

    vault::TransferTally tally;
    REQUIRE(vault::transfer_gallery(src, "g", dst, "", vault::TransferMode::Copy,
                                    {.tally = &tally}) == Ok);
    CHECK_EQ(tally.done, 1);
    CHECK_EQ(tally.failed, 1);
    REQUIRE(tally.failures.size() == 1u);
    CHECK_EQ(tally.failures[0].path, std::string("g/trailing dot."));
    CHECK(tally.failures[0].code == InvalidArg);
    CHECK(tally.failures[0].stage == vault::TransferFailure::Stage::Write);
}

// transfer_galleries records CORRECT error codes (NotFound) for structurally-failed
// subtrees; the actual error code matters (not hardcoded Ok), and continues with others
// (one done, one failed with actual NotFound code, not Ok).
TEST(transfer_galleries_records_structural_error_codes)
{
    using enum vault::VaultResult;
    TempVault sa("s6"), da("d6");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    // Setup src: one good gallery, one nonexistent (will fail with NotFound).
    REQUIRE(src.create_gallery("good") == Ok);
    REQUIRE(src.add_image("good", pattern(1000, 1), "x.jpg") == Ok);

    const auto tally = vault::transfer_galleries(
        src, {"nonexistent", "good"}, dst, "",
        vault::TransferMode::Copy);

    // One succeeded (good), one failed (nonexistent NotFound).
    CHECK_EQ(tally.done, 1);
    CHECK_EQ(tally.failed, 1);
    REQUIRE(tally.failures.size() == 1u);
    // Verify error code is correct NotFound, NOT hardcoded Ok (the bug this test catches).
    CHECK_EQ(tally.failures[0].path, std::string("nonexistent"));
    CHECK(tally.failures[0].code == NotFound);
}
