#include "test_framework.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "vault/header.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

// --- helpers (copied from test_vault.cpp) ---------------------------------

// Cheap Argon2 params so the test suite stays fast (real vaults use the
// 64 MiB / 3-pass default). Security of the KDF is covered by the crypto tests.
static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// Pattern for new vaults (matches test_vault.cpp).
static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

// Pattern for legacy fixture (created by Task 1 with i * 31 + seed formula).
static std::vector<uint8_t> legacy_pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 31);
    return v;
}

// RAII temp path: a unique .osv file removed when the helper goes out of scope.
struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

// Helper to read flags from the vault file header (non-throwing).
namespace {
uint32_t read_flags(const std::string& path)
{
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return 0;
    std::array<uint8_t, vault::HEADER_SIZE> raw{};
    if (std::fread(raw.data(), 1, raw.size(), fp) != raw.size()) {
        std::fclose(fp);
        return 0;
    }
    std::fclose(fp);
    vault::Header h;
    if (!vault::Header::parse(raw, h)) return 0;
    return h.flags;
}
}  // namespace

// --- tests ----------------------------------------------------------------

TEST(new_vault_sets_framed_flag)
{
    TempVault tv("framed_flag");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("hunter2"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    CHECK((read_flags(tv.str()) & vault::FLAG_FRAMED_CHUNKS) != 0);
}

TEST(framed_vault_full_roundtrip_across_reopen)
{
    TempVault tv("framed_rt");
    const auto compressible = std::vector<uint8_t>(300 * 1024, 0x11);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("hunter2"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", compressible, "flat.bin") == vault::VaultResult::Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("hunter2"), {}) == vault::VaultResult::Ok);

    // Read back through the public API and compare bytes.
    auto children = v.list("g");
    REQUIRE(children.size() == 1);
    REQUIRE(children[0]->is_image());

    crypto::SecureBytes out;
    REQUIRE(v.read_image(*children[0], out) == vault::VaultResult::Ok);
    CHECK_EQ(out.size(), compressible.size());
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(compressible));
}

TEST(framed_vault_compressible_payload_shrinks_file)
{
    TempVault tv("framed_small");
    const size_t payload_size = 1024 * 1024;
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("hunter2"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", std::vector<uint8_t>(payload_size, 0x00), "zeros.bin")
                == vault::VaultResult::Ok);
    }
    CHECK(std::filesystem::file_size(tv.str()) < payload_size / 2);
}

TEST(index_blob_is_framed_and_roundtrips)
{
    // Many repetitive names/tags -> the index blob itself compresses.
    TempVault tv("framed_idx");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("hunter2"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("gallery-with-a-long-repetitive-name") == vault::VaultResult::Ok);
        for (int i = 0; i < 50; ++i) {
            // Vary the tag with a suffix per iteration so all 50 stick (add_tag de-dupes).
            const auto tag = "a-really-quite-long-repeated-tag-value-" + std::to_string(i);
            REQUIRE(v.add_tag("gallery-with-a-long-repetitive-name", tag) == vault::VaultResult::Ok);
        }
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("hunter2"), {}) == vault::VaultResult::Ok);  // proves framed index decodes
}

TEST(legacy_vault_reads_and_appends_raw)
{
    // Regression test: legacy (FLAG_FRAMED_CHUNKS clear) vaults must remain unframed
    // when appended to. Fixture: password "legacy-password", gallery "pics",
    // images a.bin = pattern(4096, 0xA5), b.bin = pattern(100000, 0x5A).
    const std::string src  = std::string(OSV_VAULT_FIXTURE_DIR) + "/legacy_noflags.osv";
    TempVault tv("legacy_copy");
    const std::string path = tv.str();
    std::filesystem::copy_file(src, path, std::filesystem::copy_options::overwrite_existing);

    const std::string kPw = "legacy-password";
    const auto lpw = bytes(kPw);

    // Flag must be clear (no framing).
    CHECK((read_flags(path) & vault::FLAG_FRAMED_CHUNKS) == 0);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(path, v) == vault::VaultResult::Ok);
        REQUIRE(v.unlock(lpw, {}) == vault::VaultResult::Ok);

        // Read and verify both images.
        auto pics = v.list("pics");
        REQUIRE(pics.size() == 2);

        // Find a.bin and b.bin by name.
        const vault::IndexNode* a_node = nullptr;
        const vault::IndexNode* b_node = nullptr;
        for (auto node : pics) {
            if (node->name == "a.bin") {
                a_node = node;
            } else if (node->name == "b.bin") {
                b_node = node;
            }
        }
        REQUIRE(a_node != nullptr);
        REQUIRE(b_node != nullptr);

        // Read a.bin and verify against legacy_pattern(4096, 0xA5).
        crypto::SecureBytes a_out;
        REQUIRE(v.read_image(*a_node, a_out) == vault::VaultResult::Ok);
        auto expected_a = legacy_pattern(4096, 0xA5);
        CHECK_EQ(a_out.size(), expected_a.size());
        CHECK_BYTES_EQ(a_out.as_span(), std::span<const uint8_t>(expected_a));

        // Read b.bin and verify against legacy_pattern(100000, 0x5A).
        crypto::SecureBytes b_out;
        REQUIRE(v.read_image(*b_node, b_out) == vault::VaultResult::Ok);
        auto expected_b = legacy_pattern(100000, 0x5A);
        CHECK_EQ(b_out.size(), expected_b.size());
        CHECK_BYTES_EQ(b_out.as_span(), std::span<const uint8_t>(expected_b));

        // Append c.bin = pattern(2048, 0x3C); vault must stay raw.
        auto c_data = pattern(2048, 0x3C);
        REQUIRE(v.add_image("pics", c_data, "c.bin") == vault::VaultResult::Ok);
    }

    // Flag must still be clear after append.
    CHECK((read_flags(path) & vault::FLAG_FRAMED_CHUNKS) == 0);

    // Reopen and verify all three images.
    {
        vault::Vault v2;
        REQUIRE(vault::Vault::open(path, v2) == vault::VaultResult::Ok);
        REQUIRE(v2.unlock(lpw, {}) == vault::VaultResult::Ok);

        auto pics = v2.list("pics");
        REQUIRE(pics.size() == 3);

        const vault::IndexNode* a_node = nullptr;
        const vault::IndexNode* b_node = nullptr;
        const vault::IndexNode* c_node = nullptr;
        for (auto node : pics) {
            if (node->name == "a.bin") {
                a_node = node;
            } else if (node->name == "b.bin") {
                b_node = node;
            } else if (node->name == "c.bin") {
                c_node = node;
            }
        }
        REQUIRE(a_node != nullptr);
        REQUIRE(b_node != nullptr);
        REQUIRE(c_node != nullptr);

        // Read all three and verify.
        crypto::SecureBytes a_out;
        REQUIRE(v2.read_image(*a_node, a_out) == vault::VaultResult::Ok);
        auto expected_a = legacy_pattern(4096, 0xA5);
        CHECK_EQ(a_out.size(), expected_a.size());
        CHECK_BYTES_EQ(a_out.as_span(), std::span<const uint8_t>(expected_a));

        crypto::SecureBytes b_out;
        REQUIRE(v2.read_image(*b_node, b_out) == vault::VaultResult::Ok);
        auto expected_b = legacy_pattern(100000, 0x5A);
        CHECK_EQ(b_out.size(), expected_b.size());
        CHECK_BYTES_EQ(b_out.as_span(), std::span<const uint8_t>(expected_b));

        crypto::SecureBytes c_out;
        REQUIRE(v2.read_image(*c_node, c_out) == vault::VaultResult::Ok);
        auto expected_c = pattern(2048, 0x3C);
        CHECK_EQ(c_out.size(), expected_c.size());
        CHECK_BYTES_EQ(c_out.as_span(), std::span<const uint8_t>(expected_c));
    }
}
