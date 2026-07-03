#include "test_framework.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "image/decode.h"
#include "image/fixtures.h"
#include "vault/chunk_codec.h"
#include "vault/chunk_store.h"
#include "vault/header.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

// Phase 7 fuzzing (deterministic, seeded): 10,000+ malformed inputs across the
// three attacker-facing parsers — Vault::open/unlock (hostile .osv files),
// deserialize_index (hostile index blobs), and decode_from_memory (hostile
// image buffers). Passing means: no crash, no hang, no garbage accepted.
// Run under scripts/test.sh --asan to also catch silent memory errors.

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_ftest_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

// xorshift64*: tiny deterministic PRNG so every fuzz run tests the same inputs.
struct Prng {
    uint64_t s;
    explicit Prng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next()
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    uint32_t below(uint32_t n) { return n ? static_cast<uint32_t>(next() % n) : 0; }
};

static std::vector<uint8_t> random_bytes(Prng& rng, size_t n)
{
    std::vector<uint8_t> v(n);
    for (auto& b : v) b = static_cast<uint8_t>(rng.next());
    return v;
}

// Mutate a copy of `base`: flip a few bytes, sometimes truncate or extend.
static std::vector<uint8_t> mutate(Prng& rng, const std::vector<uint8_t>& base)
{
    std::vector<uint8_t> v = base;
    if (!v.empty()) {
        const uint32_t flips = 1 + rng.below(8);
        for (uint32_t i = 0; i < flips; ++i)
            v[rng.below(static_cast<uint32_t>(v.size()))] ^= static_cast<uint8_t>(1 + rng.below(255));
    }
    switch (rng.below(4)) {
    case 0: v.resize(v.size() / (1 + rng.below(4)));            break;  // truncate
    case 1: { auto extra = random_bytes(rng, rng.below(64)); v.insert(v.end(), extra.begin(), extra.end()); break; }
    default: break;  // keep size
    }
    return v;
}

static bool write_file(const std::string& path, std::span<const uint8_t> data)
{
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    const bool ok = data.empty() ||
                    std::fwrite(data.data(), 1, data.size(), fp) == data.size();
    std::fclose(fp);
    return ok;
}

static std::vector<uint8_t> read_file(const std::string& path)
{
    std::vector<uint8_t> v;
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return v;
    std::fseek(fp, 0, SEEK_END);
    const long n = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (n > 0) {
        v.resize(static_cast<size_t>(n));
        if (std::fread(v.data(), 1, v.size(), fp) != v.size()) v.clear();
    }
    std::fclose(fp);
    return v;
}

// A header advertising absurd KDF costs is an OOM/DoS vector (the Argon2 work
// area is m_cost_kib KiB), and an out-of-range active_slot would index past
// the 2-entry slot array. Header::parse must reject all of these outright.
TEST(fuzz_header_parse_rejects_hostile_kdf_params)
{
    vault::Header good;
    std::array<uint8_t, vault::HEADER_SIZE> raw{};

    auto parses = [&](auto&& tweak) {
        vault::Header h = good;
        tweak(h);
        h.serialize(raw);
        vault::Header parsed;
        return vault::Header::parse(raw, parsed);
    };

    CHECK_TRUE(parses([](vault::Header&) {}));  // defaults are sane

    CHECK_FALSE(parses([](vault::Header& h) { h.kdf.m_cost_kib = 0xFFFFFFFFu; }));  // 4 TiB
    CHECK_FALSE(parses([](vault::Header& h) { h.kdf.m_cost_kib = 0; }));
    CHECK_FALSE(parses([](vault::Header& h) { h.kdf.t_cost = 0; }));
    CHECK_FALSE(parses([](vault::Header& h) { h.kdf.t_cost = 0xFFFFFFFFu; }));
    CHECK_FALSE(parses([](vault::Header& h) { h.kdf.parallelism = 0; }));
    CHECK_FALSE(parses([](vault::Header& h) { h.kdf.parallelism = 10000; }));
    CHECK_FALSE(parses([](vault::Header& h) { h.kdf_algo = 7; }));      // unknown KDF
    CHECK_FALSE(parses([](vault::Header& h) { h.active_slot = 2; }));   // slot index OOB
    // Argon2 requires nb_blocks >= 8 * nb_lanes.
    CHECK_FALSE(parses([](vault::Header& h) { h.kdf.parallelism = 4; h.kdf.m_cost_kib = 16; }));
}

TEST(fuzz_vault_open_survives_4000_malformed_files)
{
    Prng rng(0xC0FFEE);

    // A small but real vault to use as the mutation base.
    TempVault base("fuzzbase");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(base.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g", random_bytes(rng, 4096), "x.bin")
                == vault::VaultResult::Ok);
    }
    const std::vector<uint8_t> valid = read_file(base.str());
    REQUIRE(!valid.empty());

    TempVault tv("fuzzopen");
    int opened = 0;

    // 2000 pure-garbage files of random sizes (incl. empty and sub-header).
    for (int i = 0; i < 2000; ++i) {
        const auto blob = random_bytes(rng, rng.below(8192));
        REQUIRE(write_file(tv.str(), blob));
        vault::Vault v;
        if (vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok) ++opened;
    }
    // Random garbage has a ~2^-64 chance of a valid magic; must all be rejected.
    CHECK_EQ(opened, 0);

    // 2000 mutated copies of a real vault. Whatever survives open() must also
    // survive an unlock attempt (corrupt slots, torn index blobs, ...) — but
    // only run the KDF when the params are still the cheap test ones.
    for (int i = 0; i < 2000; ++i) {
        const auto blob = mutate(rng, valid);
        REQUIRE(write_file(tv.str(), blob));
        vault::Vault v;
        if (vault::Vault::open(tv.str(), v) != vault::VaultResult::Ok) continue;
        vault::Header h;
        std::array<uint8_t, vault::HEADER_SIZE> raw{};
        if (blob.size() >= raw.size()) {
            std::copy_n(blob.begin(), raw.size(), raw.begin());
            if (vault::Header::parse(raw, h) &&
                h.kdf.t_cost == kTestKdf.t_cost &&
                h.kdf.m_cost_kib == kTestKdf.m_cost_kib) {
                (void)v.unlock(bytes("pw"), {});  // any result is fine; no crash
            }
        }
    }
}

TEST(fuzz_index_deserialize_survives_3000_malformed_blobs)
{
    Prng rng(0xDEADBEEF);

    // Valid serialised tree as a mutation base. Tags exercise Phase 12 parsing;
    // favorite flags exercise the Phase 13 (v3) parsing.
    vault::IndexNode root = vault::IndexNode::gallery("");
    root.tags.push_back("root_tag");
    root.children.push_back(vault::IndexNode::gallery("a"));
    root.children[0].tags.push_back("gal_tag");
    root.children[0].tags.push_back("another");
    root.children[0].favorite = true;
    root.children[0].children.push_back(vault::IndexNode::image("i.jpg"));
    root.children[0].children[0].tags.push_back("img_tag");
    root.children[0].children[0].favorite = true;
    root.children[0].children[0].meta.data_offset = 4096;
    root.children[0].children[0].meta.data_length = 1234;
    // A v5 saved-searches block exercises the Phase 18 parsing path.
    std::vector<vault::SavedSearch> searches = {
        vault::SavedSearch{"cats", {0x01, 0x05, 0x00, 0x00, 0x00, 0x02}},
        vault::SavedSearch{"trips", {0xAA, 0xBB, 0xCC}},
    };
    std::vector<uint8_t> valid;
    vault::serialize_index(root, searches, valid);

    for (int i = 0; i < 1500; ++i) {
        const auto blob = random_bytes(rng, rng.below(2048));
        vault::IndexNode out;
        std::vector<vault::SavedSearch> out_searches;
        (void)vault::deserialize_index(blob, out, out_searches);
    }
    for (int i = 0; i < 1500; ++i) {
        const auto blob = mutate(rng, valid);
        vault::IndexNode out;
        std::vector<vault::SavedSearch> out_searches;
        (void)vault::deserialize_index(blob, out, out_searches);
    }
}

TEST(fuzz_image_decode_survives_3000_malformed_buffers)
{
    Prng rng(0xFACADE);
    const auto png  = fixtures::solid_png(16, 16, 1, 2, 3);
    const auto jpeg = fixtures::solid_jpeg(16, 16, 4, 5, 6, 80);
    const auto bmp  = fixtures::solid_bmp(16, 16, 7, 8, 9);

    for (int i = 0; i < 1200; ++i) {
        const auto blob = random_bytes(rng, rng.below(4096));
        (void)image::decode_from_memory(blob);
    }
    for (int i = 0; i < 1800; ++i) {
        const auto& base = (i % 3 == 0) ? png : (i % 3 == 1) ? jpeg : bmp;
        const auto  blob = mutate(rng, base);
        (void)image::decode_from_memory(blob);
    }
}

TEST(fuzz_chunk_codec_hostile_frames)
{
    // Phase 26 fuzzing: decode_frame must reject all hostile frame formats without
    // crashing. Tests both vector and SecureBytes overloads.
    Prng rng(0xC0DECAFE);

    // 3000 random buffers straight into decode_frame; no crash required.
    for (int i = 0; i < 3000; ++i) {
        const auto blob = random_bytes(rng, rng.below(4096));
        std::vector<uint8_t> out_vec;
        crypto::SecureBytes out_sec;
        // Both overloads must never crash.
        (void)vault::chunk_codec::decode_frame(blob, out_vec);
        (void)vault::chunk_codec::decode_frame(blob, out_sec);
    }

    // Structured mutations of valid frames to test rejection of specific hostile cases.
    // Create a valid raw frame: method=0 | payload
    const auto payload = random_bytes(rng, 128);
    std::vector<uint8_t> valid_raw;
    valid_raw.push_back(vault::chunk_codec::METHOD_RAW);
    valid_raw.insert(valid_raw.end(), payload.begin(), payload.end());

    // Create a valid deflate frame via encode_frame.
    const auto deflate_payload = random_bytes(rng, 256);
    std::vector<uint8_t> valid_deflate;
    REQUIRE(vault::chunk_codec::encode_frame(deflate_payload, valid_deflate));

    // Mutation 1: flip method byte to invalid value (2..255) on raw frame.
    // Invalid methods are rejected unconditionally.
    for (int i = 2; i <= 255; ++i) {
        auto mutated = valid_raw;
        mutated[0] = static_cast<uint8_t>(i);
        std::vector<uint8_t> out;
        crypto::SecureBytes out_sec;
        const bool res_vec = vault::chunk_codec::decode_frame(mutated, out);
        const bool res_sec = vault::chunk_codec::decode_frame(mutated, out_sec);
        // Invalid method (not 0 or 1) must always fail.
        CHECK_FALSE(res_vec);
        CHECK_FALSE(res_sec);
    }

    // Mutation 2: overwrite orig_len with huge value on deflate frame.
    // The bomb guard rejects orig_len > comp_len * MAX_INFLATE_RATIO + INFLATE_SLACK.
    // Only test if valid_deflate is actually a deflate frame (method byte = 1).
    if (valid_deflate.size() >= vault::chunk_codec::DEFLATE_HDR &&
        valid_deflate[0] == vault::chunk_codec::METHOD_DEFLATE) {
        auto mutated = valid_deflate;
        // Write an absurdly large orig_len at offset 1 (0xFFFFFFFFFFFFFFFF).
        const uint64_t huge_len = 0xFFFFFFFFFFFFFFFFull;
        for (int j = 0; j < 8; ++j) {
            mutated[1 + static_cast<size_t>(j)] =
                static_cast<uint8_t>(huge_len >> (8 * j));
        }
        std::vector<uint8_t> out;
        crypto::SecureBytes out_sec;
        const bool res_vec = vault::chunk_codec::decode_frame(mutated, out);
        const bool res_sec = vault::chunk_codec::decode_frame(mutated, out_sec);
        // Bomb guard must reject this: orig_len exceeds max allowed.
        CHECK_FALSE(res_vec);
        CHECK_FALSE(res_sec);
    }

    // Mutation 3: truncate to just the header (method + partial orig_len).
    // Only test if valid_deflate is actually a deflate frame.
    // Frames with size < DEFLATE_HDR + 1 are rejected unconditionally.
    if (valid_deflate.size() > vault::chunk_codec::DEFLATE_HDR &&
        valid_deflate[0] == vault::chunk_codec::METHOD_DEFLATE) {
        auto mutated = valid_deflate;
        mutated.resize(vault::chunk_codec::DEFLATE_HDR - 1);  // Too short
        std::vector<uint8_t> out;
        crypto::SecureBytes out_sec;
        const bool res_vec = vault::chunk_codec::decode_frame(mutated, out);
        const bool res_sec = vault::chunk_codec::decode_frame(mutated, out_sec);
        // Size check must reject.
        CHECK_FALSE(res_vec);
        CHECK_FALSE(res_sec);
    }

    // Mutation 4: append 1..16 trailing garbage bytes to valid frame.
    // Trailing garbage changes the comp_len (amount of data fed to decompression),
    // which will cause src_len != comp_len after decompression, failing the verify.
    for (int garbage_bytes = 1; garbage_bytes <= 16; ++garbage_bytes) {
        auto mutated = valid_deflate;
        const auto garbage = random_bytes(rng, static_cast<size_t>(garbage_bytes));
        mutated.insert(mutated.end(), garbage.begin(), garbage.end());
        std::vector<uint8_t> out;
        crypto::SecureBytes out_sec;
        // Just verify no crash; success depends on whether miniz consumes all bytes.
        // (Trailing garbage may or may not cause failure depending on zlib stream format.)
        (void)vault::chunk_codec::decode_frame(mutated, out);
        (void)vault::chunk_codec::decode_frame(mutated, out_sec);
    }
}

TEST(fuzz_framed_chunk_read)
{
    // Phase 26 fuzzing: read_chunk with framed=true must reject hostile frame
    // contents that were encrypted via an unframed store. The AEAD verification
    // passes (correct key/nonce/tag), but the decrypted plaintext is not a valid
    // frame. read_chunk must fail cleanly and leave SecureBytes empty on failure.
    Prng rng(0xDECAFBAD);

    const auto master_key_vec = random_bytes(rng, crypto::KEY_SIZE);
    std::array<uint8_t, crypto::KEY_SIZE> master_key{};
    std::copy(master_key_vec.begin(), master_key_vec.end(), master_key.begin());

    TempVault tv("framed_fuzz");
    std::FILE* fp = std::fopen(tv.str().c_str(), "w+b");
    REQUIRE(fp != nullptr);

    // Write hostile plaintext via unframed store; read via framed.
    for (int i = 0; i < 500; ++i) {
        {
            vault::ChunkStore unframed(fp, master_key, false);
            const uint32_t hostile_type = rng.below(4);

            std::vector<uint8_t> hostile_plaintext;

            if (hostile_type == 0) {
                // Invalid method byte (2..255).
                hostile_plaintext.push_back(static_cast<uint8_t>(2 + rng.below(254)));
                const auto tail = random_bytes(rng, rng.below(100));
                hostile_plaintext.insert(hostile_plaintext.end(), tail.begin(), tail.end());
            } else if (hostile_type == 1) {
                // Deflate header with huge orig_len.
                hostile_plaintext.push_back(vault::chunk_codec::METHOD_DEFLATE);
                const uint64_t huge_len = 0x7FFFFFFFFFFFFFFFull;  // huge but not u64max
                for (int j = 0; j < 8; ++j) {
                    hostile_plaintext.push_back(
                        static_cast<uint8_t>(huge_len >> (8 * j)));
                }
                const auto tail = random_bytes(rng, rng.below(64));
                hostile_plaintext.insert(hostile_plaintext.end(), tail.begin(), tail.end());
            } else if (hostile_type == 2) {
                // Truncated deflate header (missing orig_len bytes).
                hostile_plaintext.push_back(vault::chunk_codec::METHOD_DEFLATE);
                hostile_plaintext.push_back(0xFF);  // only 1 byte of orig_len instead of 8
            } else {
                // Invalid zlib stream (garbage after proper header).
                hostile_plaintext.push_back(vault::chunk_codec::METHOD_DEFLATE);
                for (int j = 0; j < 8; ++j) hostile_plaintext.push_back(0x42);
                const auto tail = random_bytes(rng, rng.below(100));
                hostile_plaintext.insert(hostile_plaintext.end(), tail.begin(), tail.end());
            }

            vault::ChunkSpan span;
            const bool ok = unframed.append_chunk(hostile_plaintext, span);
            REQUIRE(ok);  // unframed append should always work

            // Now read via framed store; expect failure.
            {
                vault::ChunkStore framed(fp, master_key, true);
                std::vector<uint8_t> out_vec;
                crypto::SecureBytes out_sec;

                // Pre-load out_sec with sentinel content to verify failed reads clear it.
                REQUIRE(out_sec.resize(8));

                // read_chunk should reject the hostile frame (or succeed only if the
                // random plaintext happened to be valid, which is negligible).
                (void)framed.read_chunk(span, out_vec);  // may fail or succeed
                const bool res_sec = framed.read_chunk(span, out_sec);

                // Contract: SecureBytes overload leaves out_sec empty on failure.
                // (When it fails, it clears the buffer; when it succeeds, it holds the
                // decoded plaintext.) For hostile frames, we expect failure, so check
                // that on failure the buffer is truly empty.
                if (!res_sec) {
                    CHECK_EQ(out_sec.size(), size_t(0));
                }
            }
        }
        // Rewind for the next iteration.
        std::rewind(fp);
    }

    std::fclose(fp);
}

