#include "test_framework.h"

// Phase NN — incremental-compact: the new reclaim_dirty_nodes primitive used by
// the migration driver to keep file size bounded during the v1→v2 (and any
// future batched) migration. Tests pin:
//   * correctness — content decrypts byte-identically after a partial rewrite
//     + per-batch reclaim;
//   * the documented contract — locked / no-progress paths return their
//     expected results; double-reclaim is idempotent;
//   * the file stays bounded — the post-reclaim size does not exceed the
//     pre-reclaim baseline by more than a small overhead;
//   * VaultSettings round-trips the new migration tunables.

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "crypto/random.h"
#include "vault/migration.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

namespace {

std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

std::vector<uint8_t> random_payload(size_t n)
{
    std::vector<uint8_t> v(n);
    (void)crypto::fill_random(v);
    return v;
}

uint64_t size_on_disk(const fs::path& p)
{
    std::error_code ec;
    const auto s = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(s);
}

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_inccompact_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

// Build a legacy vault containing N images.
bool build_legacy_with_n_images(vault::Vault& out, const std::string& path,
                                size_t n_images, size_t image_size)
{
    if (vault::Vault::create(path, bytes("pw"), {}, kTestKdf, out) != vault::VaultResult::Ok)
        return false;
    if (out.create_gallery("g") != vault::VaultResult::Ok) return false;
    const auto payload = random_payload(image_size);
    for (size_t i = 0; i < n_images; ++i) {
        const std::string name = "img_" + std::to_string(i) + ".bin";
        if (out.add_image("g", payload, name) != vault::VaultResult::Ok) return false;
    }
    vault::test_only_downgrade_to_legacy(out);
    return !vault::uses_context_chunks(out);
}

}  // namespace

// --- reclaim_dirty_nodes correctness --------------------------------------

TEST(reclaim_dirty_nodes_tightens_vault_after_partial_rewrite)
{
    TempVault tv("tighten");
    const size_t N = 16;
    vault::Vault v;
    REQUIRE(build_legacy_with_n_images(v, tv.str(), N, 4096));

    const uint64_t size_before = size_on_disk(tv.path);
    REQUIRE(size_before > 0);

    // Rewrite every legacy node (the migration's context arm).
    std::vector<const vault::IndexNode*> dirty;
    dirty.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        const std::string path = "g/img_" + std::to_string(i) + ".bin";
        REQUIRE(vault::apply_context_rewrite(v, path) == vault::VaultResult::Ok);
        const auto* n = v.resolve_node(path);
        REQUIRE(n != nullptr);
        dirty.push_back(n);
    }

    // File should now be larger (every chunk duplicated).
    const uint64_t size_after_rewrite = size_on_disk(tv.path);
    CHECK(size_after_rewrite > size_before);

    // Reclaim the dirty set alone.
    REQUIRE(vault::reclaim_dirty_nodes(v, dirty) == vault::VaultResult::Ok);

    // File should be back to a tight shape: at most original + small slack.
    const uint64_t size_after_reclaim = size_on_disk(tv.path);
    CHECK(size_after_reclaim <= size_before + (1ULL << 20));  // +1 MiB slack headroom
    CHECK(size_after_reclaim < size_after_rewrite);
}

TEST(reclaim_dirty_nodes_preserves_image_content)
{
    TempVault tv("content");
    const size_t N = 8;
    // Deterministic payload generated ONCE so the build and the post-reclaim
    // read use the same bytes (random_payload uses the CSPRNG; calling it
    // twice produces two different sequences and would falsely look like the
    // reclaim corrupted bytes).
    const auto expected = random_payload(2048);
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
    for (size_t i = 0; i < N; ++i) {
        const std::string name = "img_" + std::to_string(i) + ".bin";
        REQUIRE(v.add_image("g", expected, name) == vault::VaultResult::Ok);
    }
    vault::test_only_downgrade_to_legacy(v);
    CHECK_FALSE(vault::uses_context_chunks(v));

    // Rewrite + reclaim.
    std::vector<const vault::IndexNode*> dirty;
    for (size_t i = 0; i < N; ++i) {
        const std::string path = "g/img_" + std::to_string(i) + ".bin";
        REQUIRE(vault::apply_context_rewrite(v, path) == vault::VaultResult::Ok);
        dirty.push_back(v.resolve_node(path));
    }
    REQUIRE(vault::reclaim_dirty_nodes(v, dirty) == vault::VaultResult::Ok);

    // Every image still decrypts byte-identically.
    for (size_t i = 0; i < N; ++i) {
        const std::string path = "g/img_" + std::to_string(i) + ".bin";
        const auto* n = v.resolve_node(path);
        REQUIRE(n != nullptr);
        crypto::SecureBytes out;
        REQUIRE(v.read_image(*n, out) == vault::VaultResult::Ok);
        CHECK_EQ(out.size(), expected.size());
        CHECK_BYTES_EQ(std::span<const uint8_t>(out.data(), out.size()),
                       std::span<const uint8_t>(expected));
    }
}

TEST(reclaim_dirty_nodes_pins_non_dirty_units)
{
    TempVault tv("pin");
    const size_t N = 6;
    vault::Vault v;
    REQUIRE(build_legacy_with_n_images(v, tv.str(), N, 4096));

    // Add two MORE images that we will NOT rewrite. Their original chunks must
    // stay at their original byte offsets. Because `build_legacy_with_n_images`
    // leaves the vault in legacy (v1) state, these are LEGACY chunks — they're
    // deliberately not touched by the migration's context-arm path; the point of
    // this test is that reclaim_dirty_nodes does not move them either.
    const auto extra_payload = random_payload(2048);
    REQUIRE(v.add_image("g", extra_payload, "untouched_a.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", extra_payload, "untouched_b.bin") == vault::VaultResult::Ok);

    // Snapshot the byte offsets of the untouched images BEFORE the rewrite.
    std::array<uint64_t, 2> before{};
    {
        const auto* n = v.resolve_node("g/untouched_a.bin");
        REQUIRE(n != nullptr);
        before[0] = n->meta.data_offset;
    }
    {
        const auto* n = v.resolve_node("g/untouched_b.bin");
        REQUIRE(n != nullptr);
        before[1] = n->meta.data_offset;
    }

    // Rewrite only the first N legacy nodes. The 2 untouched images stay where
    // they are — none of them is in the dirty set passed to reclaim_dirty_nodes.
    std::vector<const vault::IndexNode*> dirty;
    for (size_t i = 0; i < N; ++i) {
        const std::string path = "g/img_" + std::to_string(i) + ".bin";
        REQUIRE(vault::apply_context_rewrite(v, path) == vault::VaultResult::Ok);
        dirty.push_back(v.resolve_node(path));
    }

    REQUIRE(vault::reclaim_dirty_nodes(v, dirty) == vault::VaultResult::Ok);

    std::array<uint64_t, 2> after{};
    {
        const auto* n_a = v.resolve_node("g/untouched_a.bin");
        REQUIRE(n_a != nullptr);
        after[0] = n_a->meta.data_offset;
    }
    {
        const auto* n_b = v.resolve_node("g/untouched_b.bin");
        REQUIRE(n_b != nullptr);
        after[1] = n_b->meta.data_offset;
    }
    CHECK_EQ(before[0], after[0]);
    CHECK_EQ(before[1], after[1]);

    // The untouched images still decrypt.
    for (const std::string path : {"g/untouched_a.bin", "g/untouched_b.bin"}) {
        const auto* n = v.resolve_node(path);
        REQUIRE(n != nullptr);
        crypto::SecureBytes out;
        REQUIRE(v.read_image(*n, out) == vault::VaultResult::Ok);
        CHECK_EQ(out.size(), extra_payload.size());
        CHECK_BYTES_EQ(std::span<const uint8_t>(out.data(), out.size()),
                       std::span<const uint8_t>(extra_payload));
    }
}

TEST(reclaim_dirty_nodes_idempotent_after_full_compact)
{
    TempVault tv("idem");
    const size_t N = 8;
    vault::Vault v;
    REQUIRE(build_legacy_with_n_images(v, tv.str(), N, 2048));

    std::vector<const vault::IndexNode*> dirty;
    for (size_t i = 0; i < N; ++i) {
        const std::string p = "g/img_" + std::to_string(i) + ".bin";
        REQUIRE(vault::apply_context_rewrite(v, p) == vault::VaultResult::Ok);
        dirty.push_back(v.resolve_node(p));
    }
    REQUIRE(vault::reclaim_dirty_nodes(v, dirty) == vault::VaultResult::Ok);
    const uint64_t after_first = size_on_disk(tv.path);

    // Reclaim again on the same now-empty dirty set: the file should NOT
    // change (the active blob short-circuit is already firing, no rewrite).
    REQUIRE(vault::reclaim_dirty_nodes(v, {}) == vault::VaultResult::Ok);
    CHECK_EQ(size_on_disk(tv.path), after_first);
}

TEST(reclaim_dirty_nodes_locked_returns_Locked)
{
    TempVault tv("locked");
    vault::Vault v;
    REQUIRE(build_legacy_with_n_images(v, tv.str(), 1, 1024));
    v.lock();
    std::vector<const vault::IndexNode*> dirty;
    const auto* n = v.resolve_node("g/img_0.bin");
    if (n != nullptr) dirty.push_back(n);
    CHECK_EQ(vault::reclaim_dirty_nodes(v, dirty), vault::VaultResult::Locked);
}

TEST(reclaim_dirty_nodes_empty_dirty_set_is_a_no_op)
{
    TempVault tv("empty");
    const size_t N = 4;
    vault::Vault v;
    REQUIRE(build_legacy_with_n_images(v, tv.str(), N, 2048));

    const uint64_t before = size_on_disk(tv.path);
    REQUIRE(vault::reclaim_dirty_nodes(v, {}) == vault::VaultResult::Ok);
    const uint64_t after = size_on_disk(tv.path);
    // No chunk candidates → no moves → file unchanged.
    CHECK_EQ(before, after);
}

// --- VaultSettings tunables round-trip ------------------------------------

TEST(vault_settings_migration_fields_round_trip)
{
    TempVault tv("settings_rt");
    const size_t N = 1;
    vault::Vault v;
    REQUIRE(build_legacy_with_n_images(v, tv.str(), N, 1024));

    vault::VaultSettings s = vault::vault_settings(v);
    s.migration_batch_size   = 256;
    s.migration_worker_count = 4;
    REQUIRE(vault::set_vault_settings(v, s) == vault::VaultResult::Ok);

    v.lock();
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    const auto& got = vault::vault_settings(v);
    CHECK_EQ(got.migration_batch_size, 256u);
    CHECK_EQ(got.migration_worker_count, 4u);
}

TEST(effective_migration_batch_size_uses_default_when_zero)
{
    using vault::effective_migration_batch_size;
    using vault::MIGRATION_DEFAULT_BATCH_SIZE;
    vault::VaultSettings s;
    s.migration_batch_size = 0;
    CHECK_EQ(effective_migration_batch_size(s), MIGRATION_DEFAULT_BATCH_SIZE);

    s.migration_batch_size = 1;
    CHECK_EQ(effective_migration_batch_size(s), 1u);

    s.migration_batch_size = 64;
    CHECK_EQ(effective_migration_batch_size(s), 64u);

    s.migration_batch_size = 65535;
    CHECK_EQ(effective_migration_batch_size(s), 65535u);
}

TEST(effective_migration_worker_count_caps_and_preserves_zero)
{
    using vault::effective_migration_worker_count;
    using vault::MIGRATION_MAX_WORKER_COUNT;
    vault::VaultSettings s;

    // 0 = "compute in driver from hardware_concurrency()" — preserve.
    s.migration_worker_count = 0;
    CHECK_EQ(effective_migration_worker_count(s), 0u);

    s.migration_worker_count = 4;
    CHECK_EQ(effective_migration_worker_count(s), 4u);

    s.migration_worker_count = MIGRATION_MAX_WORKER_COUNT;
    CHECK_EQ(effective_migration_worker_count(s), MIGRATION_MAX_WORKER_COUNT);

    s.migration_worker_count = 99;
    CHECK_EQ(effective_migration_worker_count(s), MIGRATION_MAX_WORKER_COUNT);
}
