#include "test_framework.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "image/decode.h"
#include "image/fixtures.h"
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
    std::vector<uint8_t> valid;
    vault::serialize_index(root, valid);

    for (int i = 0; i < 1500; ++i) {
        const auto blob = random_bytes(rng, rng.below(2048));
        vault::IndexNode out;
        (void)vault::deserialize_index(blob, out);
    }
    for (int i = 0; i < 1500; ++i) {
        const auto blob = mutate(rng, valid);
        vault::IndexNode out;
        (void)vault::deserialize_index(blob, out);
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
