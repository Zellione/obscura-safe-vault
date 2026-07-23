#include "test_framework.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/file_util.h"
#include "vault/header.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

// Phase 7 hardening: verify the crash-safe double-buffered index swap.
//   * commit_index aborted by an fsync failure at *any* step leaves a vault
//     that reopens with a valid index (the previous one, or — if the abort hit
//     after the flip was written — the new one). Never a broken vault.
//   * A corrupt active index blob falls back to the inactive slot.
//   * Both slots are independently valid: pointing active_slot back at the
//     older slot (the crash-between-step-B-and-C state) loads the old index.

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

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
               ("osv_htest_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

}  // namespace

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

static bool flip_byte(const std::string& path, uint64_t pos)
{
    std::FILE* fp = std::fopen(path.c_str(), "r+b");
    if (!fp) return false;
    bool ok = std::fseek(fp, static_cast<long>(pos), SEEK_SET) == 0;
    int c = ok ? std::fgetc(fp) : EOF;
    ok = ok && c != EOF && std::fseek(fp, static_cast<long>(pos), SEEK_SET) == 0 &&
         std::fputc(c ^ 0x01, fp) != EOF;
    std::fclose(fp);
    return ok;
}

static bool read_header(const std::string& path, vault::Header& out)
{
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::array<uint8_t, vault::HEADER_SIZE> raw{};
    const bool ok = std::fread(raw.data(), 1, raw.size(), fp) == raw.size() &&
                    vault::Header::parse(raw, out);
    std::fclose(fp);
    return ok;
}

// An fsync failure injected at every step of an add_image (data sync, index
// append sync, slot-pointer header sync, active-slot flip sync) must surface
// as IoError — and the on-disk vault must still reopen with a consistent
// index: "first.jpg" always present, "second.jpg" present only if the commit
// got far enough that the new index governs.
TEST(hardening_commit_survives_sync_failure_at_every_step)
{
    const auto img1 = pattern(5000, 1);
    const auto img2 = pattern(6000, 2);

    bool reached_success = false;
    for (int fail_at = 0; fail_at < 8 && !reached_success; ++fail_at) {
        TempVault tv("syncfail");
        {
            vault::Vault v;
            REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                    == vault::VaultResult::Ok);
            REQUIRE(v.add_image("", img1, "first.jpg") == vault::VaultResult::Ok);

            vault::fileutil::inject_sync_failure(fail_at);
            const vault::VaultResult r = v.add_image("", img2, "second.jpg");
            vault::fileutil::clear_sync_failure();

            if (r == vault::VaultResult::Ok) {
                // fail_at exceeded the number of syncs in the call; no failure
                // fired. Sanity-check we exercised at least one failing step.
                reached_success = true;
                CHECK_TRUE(fail_at > 0);
            } else {
                CHECK_EQ(r, vault::VaultResult::IoError);
            }
        }

        // Whatever step failed, the file must reopen with a valid index.
        vault::Vault v2;
        REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
        REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

        auto kids = v2.list("");
        REQUIRE(kids.size() == 1 || kids.size() == 2);
        CHECK_EQ(kids[0]->name, std::string("first.jpg"));
        for (const auto* k : kids) {
            crypto::SecureBytes out;
            CHECK_EQ(v2.read_image(*k, out), vault::VaultResult::Ok);
        }
    }
    CHECK_TRUE(reached_success);  // the loop must eventually pass all syncs
}

// A corrupt *active* index blob (torn write) must fall back to the inactive
// slot's previous index.
TEST(hardening_corrupt_active_index_falls_back_to_inactive)
{
    TempVault tv("corruptidx");
    const auto img1 = pattern(3000, 3);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", img1, "first.jpg")            == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(4000, 4), "second.jpg") == vault::VaultResult::Ok);
    }

    vault::Header h;
    REQUIRE(read_header(tv.str(), h));
    const vault::IndexSlot& active = h.slot[h.active_slot];
    REQUIRE(active.length > 8);
    REQUIRE(flip_byte(tv.str(), active.offset + 4));

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto kids = v2.list("");
    REQUIRE(kids.size() == 1);  // previous index: only first.jpg
    CHECK_EQ(kids[0]->name, std::string("first.jpg"));
    crypto::SecureBytes out;
    REQUIRE(v2.read_image(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img1));
}

// Both slots must be independently valid: flipping active_slot back to the
// older slot (the exact on-disk state of a crash after step B, before the
// step-C flip) loads the previous index cleanly.
TEST(hardening_both_slots_valid_after_commit)
{
    TempVault tv("slotflip");
    const auto img1 = pattern(3500, 5);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", img1, "first.jpg")            == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(4500, 6), "second.jpg") == vault::VaultResult::Ok);
    }

    vault::Header h;
    REQUIRE(read_header(tv.str(), h));
    // active_slot lives at byte 206 (spec table); both 0->1 and 1->0 are a
    // single-bit flip, which is exactly what flip_byte does.
    REQUIRE(flip_byte(tv.str(), 206));

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto kids = v2.list("");
    REQUIRE(kids.size() == 1);
    CHECK_EQ(kids[0]->name, std::string("first.jpg"));
    crypto::SecureBytes out;
    REQUIRE(v2.read_image(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img1));
}
