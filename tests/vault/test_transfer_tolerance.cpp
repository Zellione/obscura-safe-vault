#include "test_framework.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
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
    CHECK_EQ(tally.failed, 2);
    REQUIRE(tally.failures.size() == 2u);
    CHECK_EQ(tally.failures[0].path, std::string("g/b.jpg"));
    CHECK(tally.failures[0].code == AuthFailed);
    CHECK(tally.failures[0].stage == vault::TransferFailure::Stage::Read);
    CHECK_EQ(tally.failures[1].path, std::string("g/c.jpg"));
    CHECK(tally.failures[1].code == AlreadyExists);
    CHECK(tally.failures[1].stage == vault::TransferFailure::Stage::Write);
    // Failed files stay in the source; moved ones are gone.
    CHECK(find_image(src, "g", "b.jpg") != nullptr);
    CHECK(find_image(src, "g", "c.jpg") != nullptr);
    CHECK(find_image(src, "g", "a.jpg") == nullptr);
    CHECK(find_image(dst, "", "d.jpg") != nullptr);
}
