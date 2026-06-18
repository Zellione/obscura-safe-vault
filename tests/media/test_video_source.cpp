#include "test_framework.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "media/video_source.h"
#include "vault/vault.h"
#include "vault/index.h"
#include "crypto/secure_mem.h"

namespace {

namespace fs = std::filesystem;

// Test KDF params: cheap Argon2 so the test suite stays fast.
static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

// RAII temp path for a unique .osv file.
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

// Read a file into a vector.
std::vector<uint8_t> read_file(const char* file_path)
{
    std::ifstream f(file_path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Convert std::string to span of bytes for password.
static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

} // namespace

TEST(video_source_reads_whole_stream_byte_exact)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("video_whole_stream");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", video_bytes, "v.mp4", 4096) == vault::VaultResult::Ok);

    auto children = v.list("c");
    REQUIRE(children.size() == 1);

    media::VideoSource src = media::VideoSource::open(v, *children[0]);
    REQUIRE(src.size() == video_bytes.size());

    // Read in odd-sized pieces that straddle the 4096-byte chunk boundaries.
    std::vector<uint8_t> out(video_bytes.size());
    uint64_t off = 0;
    while (off < src.size()) {
        const size_t want = std::min<size_t>(1000, src.size() - off);  // 1000 ∤ 4096
        const int64_t n = src.read(off, std::span(out.data() + off, want));
        REQUIRE(n == static_cast<int64_t>(want));
        off += static_cast<uint64_t>(n);
    }
    CHECK(testing::bytes_equal(std::span(out), std::span(video_bytes)));
    CHECK(src.read(src.size(), std::span(out.data(), 1)) == 0);   // EOF → 0
}

TEST(video_source_read_past_eof_is_zero)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("video_eof");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", video_bytes, "v.mp4", 4096) == vault::VaultResult::Ok);

    auto children = v.list("c");
    REQUIRE(children.size() == 1);

    media::VideoSource src = media::VideoSource::open(v, *children[0]);
    std::vector<uint8_t> buf(16);
    CHECK(src.read(src.size() + 100, std::span(buf)) == 0);
}

TEST(video_source_tampered_chunk_returns_negative)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("video_tampered");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", video_bytes, "v.mp4", 4096) == vault::VaultResult::Ok);

    auto children = v.list("c");
    REQUIRE(children.size() == 1);
    const vault::IndexNode& node = *children[0];

    // Flip one ciphertext byte of the first chunk on disk, then reopen.
    const uint64_t corrupt_at = node.vmeta.chunks[0].offset + 30;
    v.lock();

    // Open the file in read-write binary mode, flip a byte, and close.
    FILE* f = std::fopen(tv.str().c_str(), "r+b");
    REQUIRE(f != nullptr);
    if (std::fseek(f, static_cast<long>(corrupt_at), SEEK_SET) == 0) {
        int byte = std::fgetc(f);
        if (byte != EOF) {
            std::fseek(f, static_cast<long>(corrupt_at), SEEK_SET);
            std::fputc(byte ^ 0xFF, f);
        }
    }
    std::fclose(f);

    // Reopen and unlock
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto children2 = v2.list("c");
    REQUIRE(children2.size() == 1);

    media::VideoSource src = media::VideoSource::open(v2, *children2[0]);
    std::vector<uint8_t> buf(64);
    CHECK(src.read(0, std::span(buf)) == -1);                       // auth fail → -1
}
