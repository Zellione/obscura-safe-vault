#include "test_framework.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "crypto/random.h"
#include "crypto/secure_mem.h"
#include "vault/chunk_store.h"
#include "vault/transfer.h"
#include "vault/vault.h"

// Phase 99 (OSV-AUD-004): every encrypted record's authentication is bound to
// a logical identity, so a complete-record swap, a replay of a record under a
// different per-record id/sequence, and any byte-level tampering fail with
// AuthFailed — while compaction (byte-verbatim ciphertext moves) and
// cross-vault transfer (fresh destination identity) keep working.

namespace fs = std::filesystem;

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 31 + seed);
    return v;
}

static crypto::SecureBuffer<crypto::KEY_SIZE> random_key()
{
    crypto::SecureBuffer<crypto::KEY_SIZE> k;
    (void)crypto::fill_random(k.span());
    return k;
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
               ("osv_ctx_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

bool write_bytes(const std::string& path, uint64_t off, std::span<const uint8_t> data)
{
    std::FILE* fp = std::fopen(path.c_str(), "r+b");
    if (!fp) return false;
    const bool ok = std::fseek(fp, static_cast<long>(off), SEEK_SET) == 0 &&
                    std::fwrite(data.data(), 1, data.size(), fp) == data.size() &&
                    std::fflush(fp) == 0;
    std::fclose(fp);
    return ok;
}

bool read_bytes(const std::string& path, uint64_t off, size_t len, std::vector<uint8_t>& out)
{
    out.assign(len, 0);
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    const bool ok = std::fseek(fp, static_cast<long>(off), SEEK_SET) == 0 &&
                    std::fread(out.data(), 1, len, fp) == len;
    std::fclose(fp);
    return ok;
}
}  // namespace

// Two equal-sized images in one vault: swapping their on-disk ciphertext
// records must make BOTH reads fail authentication (the context AD binds each
// record to its own node + per-record id), not silently substitute content.
TEST(context_swap_two_equal_image_records_fails_auth)
{
    TempVault tv("swaprec");
    const auto blob = pattern(4096, 1);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", blob, "a.bin") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", blob, "b.bin") == vault::VaultResult::Ok);
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
    REQUIRE(read_bytes(tv.str(), a->meta.data_offset,
                       static_cast<size_t>(a->meta.data_length), a_rec));
    REQUIRE(read_bytes(tv.str(), b->meta.data_offset,
                       static_cast<size_t>(b->meta.data_length), b_rec));

    // Honest reads both succeed BEFORE the swap.
    crypto::SecureBytes ok_a, ok_b;
    REQUIRE(v.read_image(*a, ok_a) == vault::VaultResult::Ok);
    REQUIRE(v.read_image(*b, ok_b) == vault::VaultResult::Ok);

    // Swap the two ciphertext records byte-for-byte (the index is untouched —
    // an attacker cannot re-seal the authenticated index).
    REQUIRE(write_bytes(tv.str(), a->meta.data_offset, b_rec));
    REQUIRE(write_bytes(tv.str(), b->meta.data_offset, a_rec));

    // Both reads now fail authentication (node-bound context mismatch).
    crypto::SecureBytes xa, xb;
    CHECK_EQ(v.read_image(*a, xa), vault::VaultResult::AuthFailed);
    CHECK_EQ(v.read_image(*b, xb), vault::VaultResult::AuthFailed);
}

// Same video, two chunks: swapping the two equal-length chunk records must fail
// at the swapped sequence (the AD binds sequence + per-chunk record id).
TEST(context_swap_video_chunks_fails_auth)
{
    TempVault tv("swapvid");
    // A real MP4 fixture split at 1 KiB: chunks 0..4 are all full 1024 bytes,
    // so chunk 0 and chunk 1 are equal-length records to swap.
    std::vector<uint8_t> video = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video.empty());

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(v.add_video("g", video, "clip.mp4", 1024) == vault::VaultResult::Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    auto nodes = v.list("g");
    REQUIRE(nodes.size() == 1);
    REQUIRE(nodes[0]->vmeta.chunks.size() >= 2);
    // Find the first adjacent equal-length pair (compression makes chunk sizes
    // data-dependent; an equal-length pair is the undetectable swap case).
    size_t swap_idx = SIZE_MAX;
    for (size_t i = 0; i + 1 < nodes[0]->vmeta.chunks.size(); ++i) {
        if (nodes[0]->vmeta.chunks[i].length == nodes[0]->vmeta.chunks[i + 1].length) {
            swap_idx = i;
            break;
        }
    }
    REQUIRE(swap_idx != SIZE_MAX);
    const auto& c0 = nodes[0]->vmeta.chunks[swap_idx];
    const auto& c1 = nodes[0]->vmeta.chunks[swap_idx + 1];
    REQUIRE(c0.length == c1.length);

    std::vector<uint8_t> r0, r1;
    REQUIRE(read_bytes(tv.str(), c0.offset, static_cast<size_t>(c0.length), r0));
    REQUIRE(read_bytes(tv.str(), c1.offset, static_cast<size_t>(c1.length), r1));

    crypto::SecureBytes whole;
    REQUIRE(v.read_video(*nodes[0], whole) == vault::VaultResult::Ok);

    // Swap chunk 0 and chunk 1 ciphertext.
    REQUIRE(write_bytes(tv.str(), c0.offset, r1));
    REQUIRE(write_bytes(tv.str(), c1.offset, r0));

    crypto::SecureBytes bad;
    CHECK_EQ(v.read_video(*nodes[0], bad), vault::VaultResult::AuthFailed);
}

// The per-record random id + sequence defeat REPLAY: reading a record with a
// DIFFERENT per-record id, sequence, or role fails authentication (this is the
// defence that future re-encodes / migration rewrites rely on).
TEST(context_replay_wrong_record_id_or_sequence_fails_auth)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    vault::ChunkStore store(fp, key.as_span(), false);

    std::vector<uint8_t> plain = pattern(2048, 3);
    crypto::ChunkTag tag;
    tag.domain        = crypto::ChunkDomain::Data;
    tag.owner.fill(0x71);
    tag.context_bound = true;
    vault::ChunkSpan span;
    REQUIRE(store.append_chunk(plain, tag, span));   // tag.record = fresh id

    crypto::SecureBytes good;
    REQUIRE(store.read_chunk(span, tag, good));      // correct record opens

    // Same node/role but a different per-record id (a prior rewrite's id).
    crypto::ChunkTag replay = tag;
    ++replay.record[0];
    crypto::SecureBytes bad;
    CHECK_FALSE(store.read_chunk(span, replay, bad));

    // Same node/record but a different sequence.
    crypto::ChunkTag seq = tag;
    ++seq.sequence;
    CHECK_FALSE(store.read_chunk(span, seq, bad));

    // Same node but a different role (data vs thumb).
    crypto::ChunkTag role = tag;
    role.domain = crypto::ChunkDomain::Thumb;
    CHECK_FALSE(store.read_chunk(span, role, bad));

    std::fclose(fp);
}

// Compaction moves live ciphertext byte-for-byte (never a decrypt), so the
// context AD of every surviving chunk stays valid after an in-place compact.
TEST(context_compact_keeps_live_chunks_authenticating)
{
    TempVault tv("compact");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        for (int i = 0; i < 6; ++i)
            REQUIRE(v.add_image("g", pattern(20000, static_cast<uint8_t>(i + 1)),
                                std::string("img") + std::to_string(i) + ".bin")
                    == vault::VaultResult::Ok);
        for (int i = 0; i < 3; ++i)
            REQUIRE(v.remove_image("g", std::string("img") + std::to_string(i) + ".bin")
                    == vault::VaultResult::Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    REQUIRE(v.compact() == vault::VaultResult::Ok);

    auto nodes = v.list("g");
    REQUIRE(nodes.size() == 3);
    for (const auto* n : nodes) {
        crypto::SecureBytes out;
        CHECK_EQ(v.read_image(*n, out), vault::VaultResult::Ok);
        CHECK_EQ(out.size(), static_cast<size_t>(20000));
        crypto::SecureBytes thumb;
        if (n->meta.thumb_length > 0) CHECK_EQ(v.read_thumbnail(*n, thumb), vault::VaultResult::Ok);
    }
}

// Cross-vault transfer re-stages at the DESTINATION with a FRESH node identity,
// so the destination's chunk ADs bind to its own vault/node — the transferred
// content still authenticates at the destination.
TEST(context_transfer_assigns_fresh_destination_identity)
{
    TempVault tsrc("txsrc");
    TempVault tdst("txdst");
    const auto blob = pattern(8000, 9);
    {
        vault::Vault src;
        REQUIRE(vault::Vault::create(tsrc.str(), bytes("pw"), {}, kTestKdf, src)
                == vault::VaultResult::Ok);
        REQUIRE(src.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(src.add_image("g", blob, "pic.bin") == vault::VaultResult::Ok);
    }
    vault::Vault src;
    vault::Vault dst;
    REQUIRE(vault::Vault::open(tsrc.str(), src) == vault::VaultResult::Ok);
    REQUIRE(src.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    REQUIRE(vault::Vault::create(tdst.str(), bytes("pw"), {}, kTestKdf, dst)
            == vault::VaultResult::Ok);

    const auto s_nodes = src.list("g");
    REQUIRE(s_nodes.size() == 1);
    const auto src_id = s_nodes[0]->node_id;

    REQUIRE(vault::transfer_image(src, "g", "pic.bin", dst, "", vault::TransferMode::Move)
            == vault::VaultResult::Ok);

    auto d_nodes = dst.list("");
    REQUIRE(d_nodes.size() == 1);
    const auto& dn = *d_nodes[0];
    CHECK(dn.meta.context_bound);
    // A fresh destination identity: never the source's node_id.
    CHECK_FALSE(testing::bytes_equal(dn.node_id, std::span<const uint8_t>(src_id)));
    // The content reads back at the destination under its NEW identity.
    crypto::SecureBytes out;
    CHECK_EQ(dst.read_image(dn, out), vault::VaultResult::Ok);
    CHECK_EQ(out.size(), blob.size());
}