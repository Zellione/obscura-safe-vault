#include "test_framework.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "crypto/random.h"
#include "vault/migration.h"
#include "vault/vault.h"

// Phase 99 (OSV-AUD-004) v1→v2 migration: a legacy vault (header flag clear,
// every record under the plain AEAD) is re-encoded in place to the
// context-bound AEAD, the master-key wrap is re-sealed under the session KEK,
// and the flag + AD-bound index land in ONE atomic commit. Cancel leaves a
// mixed-but-consistent vault that reopens; the completed migration detects the
// same chunk swaps PR 1 tests.

namespace fs = std::filesystem;

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

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

static std::vector<uint8_t> read_file(const char* path)
{
    std::FILE* fp = std::fopen(path, "rb");
    if (!fp) return {};
    std::vector<uint8_t> out;
    std::fseek(fp, 0, SEEK_END);
    const long len = std::ftell(fp);
    std::rewind(fp);
    out.resize(static_cast<size_t>(len));
    const bool ok = len == 0 || std::fread(out.data(), 1, out.size(), fp) == out.size();
    std::fclose(fp);
    if (!ok) out.clear();
    return out;
}

namespace {
struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_mctx_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

// A v2 vault with one image (and its thumbnail), then downgraded to a genuine
// legacy vault. Returns false on any setup failure; `out` is left unlocked.
bool build_legacy(vault::Vault& out, const std::string& path, const std::vector<uint8_t>& img)
{
    if (vault::Vault::create(path, bytes("pw"), {}, kTestKdf, out) != vault::VaultResult::Ok)
        return false;
    if (out.create_gallery("g") != vault::VaultResult::Ok) return false;
    if (out.add_image("g", img, "pic.bin") != vault::VaultResult::Ok) return false;
    vault::test_only_downgrade_to_legacy(out);
    return !vault::uses_context_chunks(out);
}
}  // namespace

TEST(context_scan_counts_legacy_records_only)
{
    TempVault tv("scan");
    const auto img = pattern(2048, 1);
    vault::Vault v;
    REQUIRE(build_legacy(v, tv.str(), img));

    // Legacy: every media record is owed.
    const vault::MigrationScan legacy = vault::scan_migration(v, false, /*context_stale=*/true);
    CHECK_EQ(legacy.context, 1u);
    CHECK_EQ(legacy.total(), 1u);
    CHECK_FALSE(legacy.empty());

    // A v2 vault (flag set) never owes the context arm.
    const vault::MigrationScan v2 = vault::scan_migration(v, false, /*context_stale=*/false);
    CHECK_EQ(v2.context, 0u);

    // migration_pending sees a legacy vault immediately.
    vault::VaultSettings s = vault::stamp_migrated(vault::VaultSettings::seeded(), 1, 512);
    CHECK(vault::migration_pending(s, 1, 512, /*context_stale=*/true));
    CHECK_FALSE(vault::migration_pending(s, 1, 512, /*context_stale=*/false));
}

TEST(context_rewrite_and_finalize_migrate_the_vault_and_reopen)
{
    TempVault tv("migrate");
    const auto img = pattern(4096, 2);
    vault::Vault v;
    REQUIRE(build_legacy(v, tv.str(), img));

    // Rewrite every legacy record (the migration's context arm).
    auto nodes = v.list("g");
    REQUIRE(nodes.size() == 1);
    REQUIRE(vault::apply_context_rewrite(v, "g/pic.bin") == vault::VaultResult::Ok);

    const auto* n = v.resolve_node("g/pic.bin");
    REQUIRE(n != nullptr);
    CHECK(n->meta.context_bound);
    CHECK_FALSE(testing::bytes_equal(n->node_id, std::array<uint8_t, 16>{}));
    CHECK_FALSE(testing::bytes_equal(n->meta.data_id, std::array<uint8_t, 16>{}));

    // Finalize + one commit (what commit_migration does).
    REQUIRE(vault::finalize_context_migration(v) == vault::VaultResult::Ok);
    CHECK(vault::uses_context_chunks(v));
    REQUIRE(vault::commit_migration(v, vault::stamp_migrated(vault::vault_settings(v), 1, 512))
            == vault::VaultResult::Ok);

    // Content still decrypts identically under the new records.
    crypto::SecureBytes out;
    REQUIRE(v.read_image(*v.resolve_node("g/pic.bin"), out) == vault::VaultResult::Ok);
    CHECK_EQ(out.size(), img.size());
    CHECK(std::equal(out.data(), out.data() + out.size(), img.begin()));

    // Reopen + unlock: the re-wrapped master key / AD-bound index both parse.
    v.lock();
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    crypto::SecureBytes after;
    REQUIRE(v.read_image(*v.resolve_node("g/pic.bin"), after) == vault::VaultResult::Ok);
    CHECK_EQ(after.size(), img.size());
}

TEST(context_completed_migration_detects_swaps)
{
    TempVault tv("swapdetect");
    const auto img = pattern(2048, 3);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", img, "a.bin") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", img, "b.bin") == vault::VaultResult::Ok);
        vault::test_only_downgrade_to_legacy(v);

        // Full context migration of both nodes.
        REQUIRE(vault::apply_context_rewrite(v, "g/a.bin") == vault::VaultResult::Ok);
        REQUIRE(vault::apply_context_rewrite(v, "g/b.bin") == vault::VaultResult::Ok);
        REQUIRE(vault::finalize_context_migration(v) == vault::VaultResult::Ok);
        REQUIRE(vault::commit_migration(v, vault::stamp_migrated(vault::vault_settings(v), 1, 512))
                == vault::VaultResult::Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto nodes = v.list("g");
    REQUIRE(nodes.size() == 2);
    const auto* a = nodes[0];
    const auto* b = nodes[1];
    REQUIRE(a->meta.data_length == b->meta.data_length);

    std::vector<uint8_t> a_rec, b_rec;
    auto read_at = [&](uint64_t off, uint64_t len, std::vector<uint8_t>& outv) {
        outv.assign(static_cast<size_t>(len), 0);
        std::FILE* fp = std::fopen(tv.str().c_str(), "rb");
        REQUIRE(fp);
        REQUIRE(std::fseek(fp, static_cast<long>(off), SEEK_SET) == 0);
        REQUIRE(std::fread(outv.data(), 1, outv.size(), fp) == outv.size());
        std::fclose(fp);
    };
    read_at(a->meta.data_offset, a->meta.data_length, a_rec);
    read_at(b->meta.data_offset, b->meta.data_length, b_rec);

    // Pre-swap reads succeed.
    crypto::SecureBytes ok_a, ok_b;
    REQUIRE(v.read_image(*a, ok_a) == vault::VaultResult::Ok);
    REQUIRE(v.read_image(*b, ok_b) == vault::VaultResult::Ok);

    auto splice = [&](uint64_t off, std::span<const uint8_t> data) {
        std::FILE* fp = std::fopen(tv.str().c_str(), "r+b");
        REQUIRE(fp);
        REQUIRE(std::fseek(fp, static_cast<long>(off), SEEK_SET) == 0);
        REQUIRE(std::fwrite(data.data(), 1, data.size(), fp) == data.size());
        REQUIRE(std::fflush(fp) == 0);
        std::fclose(fp);
    };
    splice(a->meta.data_offset, b_rec);
    splice(b->meta.data_offset, a_rec);

    // The migrated (context-bound) vault now detects the swap.
    crypto::SecureBytes xa, xb;
    CHECK_EQ(v.read_image(*a, xa), vault::VaultResult::AuthFailed);
    CHECK_EQ(v.read_image(*b, xb), vault::VaultResult::AuthFailed);
}

TEST(context_cancel_leaves_mixed_vault_reopenable)
{
    TempVault tv("cancel");
    const auto img = pattern(2048, 4);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", img, "a.bin") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", img, "b.bin") == vault::VaultResult::Ok);
        vault::test_only_downgrade_to_legacy(v);

        // "Cancel" after rewriting exactly ONE node: commit without finalizing.
        REQUIRE(vault::apply_context_rewrite(v, "g/a.bin") == vault::VaultResult::Ok);
        REQUIRE(vault::commit_migration(v, vault::stamp_migrated(vault::vault_settings(v), 1, 512))
                == vault::VaultResult::Ok);
        // The flag is still clear => still owed at the next unlock.
        CHECK_FALSE(vault::uses_context_chunks(v));
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto nodes = v.list("g");
    REQUIRE(nodes.size() == 2);
    for (const auto* n : nodes) {
        crypto::SecureBytes out;
        CHECK_EQ(v.read_image(*n, out), vault::VaultResult::Ok);   // both read fine
        CHECK_EQ(out.size(), img.size());
    }
    // Exactly one node is migrated; the scan reports the other.
    const vault::MigrationScan scan = vault::scan_migration(v, false, /*context_stale=*/true);
    CHECK_EQ(scan.context, 1u);
}

TEST(context_migration_reencodes_video_chunks)
{
    TempVault tv("video");
    std::vector<uint8_t> video = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video.empty());
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(v.add_video("g", video, "clip.mp4", 2048) == vault::VaultResult::Ok);
        vault::test_only_downgrade_to_legacy(v);
        CHECK_FALSE(vault::uses_context_chunks(v));
        REQUIRE(vault::apply_context_rewrite(v, "g/clip.mp4") == vault::VaultResult::Ok);
        REQUIRE(vault::finalize_context_migration(v) == vault::VaultResult::Ok);
        REQUIRE(vault::commit_migration(v, vault::stamp_migrated(vault::vault_settings(v), 1, 512))
                == vault::VaultResult::Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    CHECK(vault::uses_context_chunks(v));
    auto nodes = v.list("g");
    REQUIRE(nodes.size() == 1);
    REQUIRE(nodes[0]->vmeta.context_bound);
    // Every video chunk carries a 0-based sequence and a nonzero record id.
    for (size_t i = 0; i < nodes[0]->vmeta.chunks.size(); ++i) {
        CHECK_EQ(nodes[0]->vmeta.chunks[i].sequence, static_cast<uint32_t>(i));
        CHECK_FALSE(testing::bytes_equal(nodes[0]->vmeta.chunks[i].id,
                                         std::array<uint8_t, 16>{}));
    }
    crypto::SecureBytes out;
    REQUIRE(v.read_video(*nodes[0], out) == vault::VaultResult::Ok);
    CHECK_EQ(out.size(), video.size());
    CHECK(std::equal(out.data(), out.data() + out.size(), video.begin()));
}