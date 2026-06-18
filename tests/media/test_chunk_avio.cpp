#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "media/chunk_avio.h"
#include "media/video_source.h"
#include "vault/vault.h"
#include "crypto/secure_mem.h"

extern "C" {
#include <libavformat/avio.h>
}

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

// Count files in a directory (for the no-write invariant).
size_t count_files(const std::string& dir)
{
    size_t n = 0;
    for (auto& e : fs::directory_iterator(dir)) {
        (void)e;
        ++n;
    }
    return n;
}

} // namespace

TEST(chunk_avio_reads_and_seeks_byte_exact)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("chunk_avio_reads");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", video_bytes, "v.mp4", 4096) == vault::VaultResult::Ok);

    auto children = v.list("c");
    REQUIRE(children.size() == 1);
    const vault::IndexNode& node = *children[0];

    // Record file count before ChunkAvio operations.
    const std::string dir = tv.path.parent_path().string();
    const size_t before = count_files(dir);

    // Create ChunkAvio from the video node.
    media::ChunkAvio avio(media::VideoSource::open(v, node));
    REQUIRE(avio.valid());
    AVIOContext* ctx = avio.ctx();

    // Test AVSEEK_SIZE via avio_size.
    CHECK(avio_size(ctx) == static_cast<int64_t>(video_bytes.size()));

    // Test seeking to the beginning.
    REQUIRE(avio_seek(ctx, 0, SEEK_SET) == 0);

    // Read the whole stream via AVIO; compare to original.
    std::vector<uint8_t> out(video_bytes.size());
    int64_t total = 0;
    while (total < static_cast<int64_t>(video_bytes.size())) {
        const int n = avio_read(ctx, out.data() + total,
                                static_cast<int>(video_bytes.size() - total));
        REQUIRE(n > 0);
        total += n;
    }
    CHECK(testing::bytes_equal(out, video_bytes));

    // Seek to mid offset that straddles chunk boundary, read, compare.
    const int64_t mid = 4096 + 123;
    REQUIRE(avio_seek(ctx, mid, SEEK_SET) == mid);
    std::vector<uint8_t> piece(500);
    REQUIRE(avio_read(ctx, piece.data(), 500) == 500);
    CHECK(testing::bytes_equal(piece, std::span<const uint8_t>(video_bytes).subspan(mid, 500)));

    // INVARIANT #1: no new files appeared on disk.
    CHECK(count_files(dir) == before);
}

TEST(chunk_avio_surfaces_decrypt_failure)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("chunk_avio_decrypt_fail");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", video_bytes, "v.mp4", 4096) == vault::VaultResult::Ok);

    auto children = v.list("c");
    REQUIRE(children.size() == 1);
    const vault::IndexNode& node = *children[0];

    // Flip a ciphertext byte of the first chunk on disk.
    const uint64_t corrupt_at = node.vmeta.chunks[0].offset + 30;
    v.lock();

    // Open file, flip byte, close.
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

    // Reopen and unlock.
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto children2 = v2.list("c");
    REQUIRE(children2.size() == 1);
    const vault::IndexNode& node2 = *children2[0];

    // ChunkAvio over the tampered chunk should return AVERROR on read.
    media::ChunkAvio avio(media::VideoSource::open(v2, node2));
    std::vector<uint8_t> buf(64);
    CHECK(avio_read(avio.ctx(), buf.data(), 64) < 0);  // negative AVERROR
}

#endif // OSV_VENDORED_AV
