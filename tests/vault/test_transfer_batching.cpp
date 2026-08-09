#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/combine.h"
#include "vault/file_util.h"
#include "vault/staging.h"
#include "vault/transfer.h"
#include "vault/vault.h"

// Phase 69: transfer batching. A bulk transfer must NOT pay a durable index
// commit per file (the destination commits in batches, like the import queue)
// and a Move must NOT remove source files one-by-one (one remove_media_batch
// after the destination is durable). These tests pin both the observable
// batching (fileutil::sync_call_count) and the unchanged end-state semantics.

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
               ("osv_xferbatch_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

static const vault::IndexNode* find_media(const vault::Vault& v, std::string_view gallery,
                                          std::string_view name)
{
    for (const auto* c : v.list(gallery))
        if (c->is_media() && c->name == name) return c;
    return nullptr;
}

// attach_image_prestaged stages + attaches but must NOT commit: the node is
// visible in this session and gone after a reopen; an explicit commit_index
// makes it durable.
TEST(attach_image_prestaged_defers_commit)
{
    using enum vault::VaultResult;
    TempVault tv("attach");
    const auto img_a = pattern(3000, 1);
    const auto img_b = pattern(3000, 2);
    const auto img_c = pattern(3000, 3);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
        REQUIRE(v.add_image("", img_a, "a.jpg") == Ok);   // committed baseline

        vault::StagedThumb thumb;   // no thumbnail
        REQUIRE(vault::attach_image_prestaged(v, "", img_b, "b.jpg", thumb, 0) == Ok);
        CHECK(find_media(v, "", "b.jpg") != nullptr);     // visible in-memory
    }
    {
        // No commit was issued for b.jpg: it must not survive the reopen.
        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == Ok);
        REQUIRE(v.unlock(bytes("pw"), {}) == Ok);
        CHECK(find_media(v, "", "a.jpg") != nullptr);
        CHECK(find_media(v, "", "b.jpg") == nullptr);

        // Attach + explicit commit persists.
        vault::StagedThumb thumb;
        REQUIRE(vault::attach_image_prestaged(v, "", img_c, "c.jpg", thumb, 0) == Ok);
        REQUIRE(vault::commit_staged(v) == Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == Ok);
    CHECK(find_media(v, "", "c.jpg") != nullptr);
}

// A bulk Copy of N files performs O(1) durable commits, not O(N): with one
// batch (N < 32) the whole transfer is a single 3-sync commit, so the sync
// count stays far under the old 4-syncs-per-file behavior.
TEST(transfer_images_copy_commits_in_batches)
{
    using enum vault::VaultResult;
    TempVault sa("cpsrc"), da("cpdst");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("ps"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("pd"), {}, kKdf, dst) == Ok);

    std::vector<std::string> names;
    for (int i = 0; i < 10; ++i) {
        names.push_back("img" + std::to_string(i) + ".jpg");
        REQUIRE(src.add_image("", pattern(3000, static_cast<uint8_t>(i)), names.back()) == Ok);
    }

    vault::fileutil::sync_call_count().store(0);
    auto tally = vault::transfer_images(src, "", names, dst, "",
                                        vault::TransferMode::Copy);
    const uint64_t syncs = vault::fileutil::sync_call_count().load();

    CHECK(tally.done == 10);
    CHECK(tally.failed == 0);
    CHECK(syncs <= 9);   // one batched commit (3) + slack; the old path needed 40

    // End state unchanged: everything present in dst, and durable.
    vault::Vault dst2;
    REQUIRE(vault::Vault::open(da.str(), dst2) == Ok);
    REQUIRE(dst2.unlock(bytes("pd"), {}) == Ok);
    for (const auto& n : names) CHECK(find_media(dst2, "", n) != nullptr);
}

// A bulk Move batches BOTH sides: one destination commit for the copies plus
// one source remove_media_batch commit — not a per-file commit + per-file
// remove_image (each with its own commit and auto-reclaim scan).
TEST(transfer_images_move_batches_commits_and_removals)
{
    using enum vault::VaultResult;
    TempVault sa("mvsrc"), da("mvdst");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("ps"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("pd"), {}, kKdf, dst) == Ok);

    std::vector<std::string> names;
    for (int i = 0; i < 10; ++i) {
        names.push_back("img" + std::to_string(i) + ".jpg");
        REQUIRE(src.add_image("", pattern(3000, static_cast<uint8_t>(i)), names.back()) == Ok);
    }

    vault::fileutil::sync_call_count().store(0);
    auto tally = vault::transfer_images(src, "", names, dst, "",
                                        vault::TransferMode::Move);
    const uint64_t syncs = vault::fileutil::sync_call_count().load();

    CHECK(tally.done == 10);
    CHECK(tally.failed == 0);
    CHECK(syncs <= 12);   // dst batch commit (3) + src batch removal (3) + slack; old ≈ 70

    // Moved out of source, into destination — durably on both sides.
    vault::Vault src2, dst2;
    REQUIRE(vault::Vault::open(sa.str(), src2) == Ok);
    REQUIRE(src2.unlock(bytes("ps"), {}) == Ok);
    REQUIRE(vault::Vault::open(da.str(), dst2) == Ok);
    REQUIRE(dst2.unlock(bytes("pd"), {}) == Ok);
    for (const auto& n : names) {
        CHECK(find_media(src2, "", n) == nullptr);
        CHECK(find_media(dst2, "", n) != nullptr);
    }
}

// Deferred removal must be per-file precise: a file whose destination add
// failed (collision) stays in the source; the others are still removed.
TEST(transfer_images_move_collision_keeps_only_failed_source)
{
    using enum vault::VaultResult;
    TempVault sa("colsrc"), da("coldst");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("ps"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("pd"), {}, kKdf, dst) == Ok);

    std::vector<std::string> names;
    for (int i = 0; i < 4; ++i) {
        names.push_back("f" + std::to_string(i) + ".jpg");
        REQUIRE(src.add_image("", pattern(2000, static_cast<uint8_t>(i)), names.back()) == Ok);
    }
    // Pre-seed the collision at the destination for f2.jpg.
    REQUIRE(dst.add_image("", pattern(500, 99), "f2.jpg") == Ok);

    auto tally = vault::transfer_images(src, "", names, dst, "",
                                        vault::TransferMode::Move);
    CHECK(tally.done == 3);
    CHECK(tally.failed == 0);           // collision is a skip, not a failure
    CHECK(tally.skipped == 1);          // f2.jpg collision
    REQUIRE(tally.failures.size() == 0u);

    CHECK(find_media(src, "", "f2.jpg") != nullptr);   // skipped (collision) file stays
    CHECK(find_media(src, "", "f0.jpg") == nullptr);   // moved files removed
    CHECK(find_media(src, "", "f1.jpg") == nullptr);
    CHECK(find_media(src, "", "f3.jpg") == nullptr);
}

// A destination commit failure fails every file of that batch (none of them is
// durable) and leaves the source untouched — no file may be reported done when
// its batch never committed, and none may be removed from the source.
TEST(transfer_images_commit_failure_fails_whole_batch)
{
    using enum vault::VaultResult;
    TempVault sa("iosrc"), da("iodst");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("ps"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("pd"), {}, kKdf, dst) == Ok);

    std::vector<std::string> names;
    for (int i = 0; i < 3; ++i) {
        names.push_back("g" + std::to_string(i) + ".jpg");
        REQUIRE(src.add_image("", pattern(2000, static_cast<uint8_t>(i)), names.back()) == Ok);
    }

    // The next fileutil::sync is the batched destination commit — fail it.
    vault::fileutil::inject_sync_failure(0);
    auto tally = vault::transfer_images(src, "", names, dst, "",
                                        vault::TransferMode::Move);
    vault::fileutil::clear_sync_failure();

    CHECK(tally.done == 0);
    CHECK(tally.failed == 3);

    // Source untouched (nothing was durably moved).
    for (const auto& n : names) CHECK(find_media(src, "", n) != nullptr);

    // Destination reopen: the failed batch is not present.
    dst.lock();
    vault::Vault dst2;
    REQUIRE(vault::Vault::open(da.str(), dst2) == Ok);
    REQUIRE(dst2.unlock(bytes("pd"), {}) == Ok);
    for (const auto& n : names) CHECK(find_media(dst2, "", n) == nullptr);
}

// A gallery Move keeps its end-state semantics under batching: subtree copied,
// source media removed, emptied source galleries pruned — with batched commits
// (bounded syncs) instead of per-file ones.
TEST(transfer_gallery_move_batches_source_removal)
{
    using enum vault::VaultResult;
    TempVault sa("gsrc"), da("gdst");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("ps"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("pd"), {}, kKdf, dst) == Ok);

    REQUIRE(src.create_gallery("G/S") == Ok);
    REQUIRE(src.add_image("G", pattern(2500, 1), "g1.jpg") == Ok);
    REQUIRE(src.add_image("G", pattern(2500, 2), "g2.jpg") == Ok);
    REQUIRE(src.add_image("G/S", pattern(2500, 3), "s1.jpg") == Ok);

    vault::fileutil::sync_call_count().store(0);
    vault::TransferTally tally;
    REQUIRE(vault::transfer_gallery(src, "G", dst, "", vault::TransferMode::Move,
                                    {.tally = &tally}) == Ok);
    const uint64_t syncs = vault::fileutil::sync_call_count().load();

    CHECK(tally.done == 3);
    CHECK(tally.failed == 0);
    // dst batch commit (3) + src batch removal (3) + prune commits (2 galleries,
    // 3 each) + slack; the old path was ~4 syncs per file plus 3 per remove.
    CHECK(syncs <= 15);

    // Subtree fully at destination; source subtree pruned away.
    vault::Vault src2, dst2;
    REQUIRE(vault::Vault::open(sa.str(), src2) == Ok);
    REQUIRE(src2.unlock(bytes("ps"), {}) == Ok);
    REQUIRE(vault::Vault::open(da.str(), dst2) == Ok);
    REQUIRE(dst2.unlock(bytes("pd"), {}) == Ok);
    CHECK(find_media(dst2, "G", "g1.jpg") != nullptr);
    CHECK(find_media(dst2, "G", "g2.jpg") != nullptr);
    CHECK(find_media(dst2, "G/S", "s1.jpg") != nullptr);
    CHECK(src2.resolve_node("G") == nullptr);   // emptied source gallery pruned
}

// A gallery combine (merge into an existing same-named destination gallery)
// moves its media through the batched bulk path, not one committed
// transfer_image per file.
TEST(combine_galleries_batches_media_moves)
{
    using enum vault::VaultResult;
    TempVault sa("cbsrc"), da("cbdst");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("ps"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("pd"), {}, kKdf, dst) == Ok);

    REQUIRE(src.create_gallery("G") == Ok);
    REQUIRE(dst.create_gallery("G") == Ok);   // same name on both sides → merge
    std::vector<std::string> names;
    for (int i = 0; i < 10; ++i) {
        names.push_back("m" + std::to_string(i) + ".jpg");
        REQUIRE(src.add_image("G", pattern(2500, static_cast<uint8_t>(i)), names.back()) == Ok);
    }

    vault::fileutil::sync_call_count().store(0);
    vault::CombineTally tally;
    REQUIRE(vault::combine_galleries(src, "G", dst, "G", tally) == Ok);
    const uint64_t syncs = vault::fileutil::sync_call_count().load();

    CHECK(tally.media_moved == 10);
    CHECK(tally.media_skipped == 0);
    // dst batch commit + src batch removal + emptied-source remove_gallery
    // commit + slack; the old per-file transfer_image path needed ~70.
    CHECK(syncs <= 14);

    vault::Vault dst2;
    REQUIRE(vault::Vault::open(da.str(), dst2) == Ok);
    REQUIRE(dst2.unlock(bytes("pd"), {}) == Ok);
    for (const auto& n : names) CHECK(find_media(dst2, "G", n) != nullptr);
    CHECK(src.resolve_node("G") == nullptr);   // emptied source gallery deleted
}
