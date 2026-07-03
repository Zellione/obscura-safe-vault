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
