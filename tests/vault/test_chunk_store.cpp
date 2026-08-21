#include "test_framework.h"

#include <cstdio>
#include <vector>

#include "crypto/random.h"
#include "crypto/secure_mem.h"
#include "vault/chunk_store.h"

using vault::ChunkSpan;
using vault::ChunkStore;

static crypto::SecureBuffer<crypto::KEY_SIZE> random_key()
{
    crypto::SecureBuffer<crypto::KEY_SIZE> k;
    (void)crypto::fill_random(k.span());
    return k;
}

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 17 + seed);
    return v;
}

TEST(chunk_store_append_read_roundtrip)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), false);

    auto plain = pattern(5000, 3);
    ChunkSpan span;
    REQUIRE(store.append_chunk(plain, span));
    CHECK_EQ(span.length, plain.size() + crypto::NONCE_SIZE + crypto::TAG_SIZE);

    std::vector<uint8_t> out;
    REQUIRE(store.read_chunk(span, out));
    CHECK_BYTES_EQ(std::span<const uint8_t>(out), std::span<const uint8_t>(plain));
    std::fclose(fp);
}

TEST(chunk_store_appends_are_contiguous)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), false);

    auto p1 = pattern(100, 1);
    auto p2 = pattern(200, 2);
    ChunkSpan s1, s2;
    REQUIRE(store.append_chunk(p1, s1));
    REQUIRE(store.append_chunk(p2, s2));

    CHECK_EQ(s1.offset, static_cast<uint64_t>(0));
    CHECK_EQ(s2.offset, s1.offset + s1.length);

    // Both still read back correctly and independently.
    std::vector<uint8_t> o1, o2;
    REQUIRE(store.read_chunk(s1, o1));
    REQUIRE(store.read_chunk(s2, o2));
    CHECK_BYTES_EQ(std::span<const uint8_t>(o1), std::span<const uint8_t>(p1));
    CHECK_BYTES_EQ(std::span<const uint8_t>(o2), std::span<const uint8_t>(p2));
    std::fclose(fp);
}

TEST(chunk_store_reads_into_secure_bytes)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), false);

    auto plain = pattern(4096, 9);
    ChunkSpan span;
    REQUIRE(store.append_chunk(plain, span));

    crypto::SecureBytes secure;
    REQUIRE(store.read_chunk(span, secure));
    REQUIRE(secure.size() == plain.size());
    CHECK_BYTES_EQ(secure.as_span(), std::span<const uint8_t>(plain));
    std::fclose(fp);
}

TEST(chunk_store_read_detects_tamper)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), false);

    auto plain = pattern(64, 5);
    ChunkSpan span;
    REQUIRE(store.append_chunk(plain, span));

    // Flip a byte inside the ciphertext region directly in the file.
    const long cipher_pos = static_cast<long>(span.offset + crypto::NONCE_SIZE + 3);
    REQUIRE(std::fseek(fp, cipher_pos, SEEK_SET) == 0);
    int c = std::fgetc(fp);
    REQUIRE(c != EOF);
    REQUIRE(std::fseek(fp, cipher_pos, SEEK_SET) == 0);
    REQUIRE(std::fputc(c ^ 0x01, fp) != EOF);
    std::fflush(fp);

    std::vector<uint8_t> out;
    CHECK_FALSE(store.read_chunk(span, out));
    CHECK_TRUE(out.empty());
    std::fclose(fp);
}

TEST(chunk_store_raw_append_read_roundtrip)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), false);

    // Raw bytes are stored verbatim (used by the vault for the sealed index blob).
    auto raw = pattern(321, 7);
    uint64_t offset = 0;
    REQUIRE(store.append_raw(raw, offset));
    CHECK_EQ(offset, static_cast<uint64_t>(0));

    std::vector<uint8_t> out;
    REQUIRE(store.read_raw(offset, raw.size(), out));
    CHECK_BYTES_EQ(std::span<const uint8_t>(out), std::span<const uint8_t>(raw));
    std::fclose(fp);
}

TEST(chunk_store_read_rejects_out_of_range_span)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), false);

    auto plain = pattern(32, 1);
    ChunkSpan span;
    REQUIRE(store.append_chunk(plain, span));

    // A span claiming far more than the file holds must fail, not read garbage.
    ChunkSpan bogus{.offset = span.offset, .length = span.length + 100000};
    std::vector<uint8_t> out;
    CHECK_FALSE(store.read_chunk(bogus, out));
    std::fclose(fp);
}

TEST(chunk_store_framed_roundtrip_both_overloads)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), true);

    const std::vector<uint8_t> compressible(200 * 1024, 0x42);
    ChunkSpan span{};
    REQUIRE(store.append_chunk(compressible, span));
    // Framed + deflated: ciphertext is much smaller than the payload.
    CHECK(span.length < compressible.size() / 4);

    std::vector<uint8_t> out_vec;
    REQUIRE(store.read_chunk(span, out_vec));
    CHECK(out_vec == compressible);

    crypto::SecureBytes out_sec;
    REQUIRE(store.read_chunk(span, out_sec));
    REQUIRE(out_sec.size() == compressible.size());
    CHECK(std::memcmp(out_sec.data(), compressible.data(), compressible.size()) == 0);
    std::fclose(fp);
}

TEST(chunk_store_framed_incompressible_overhead_is_one_byte)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), true);
    std::vector<uint8_t> noise(4096);
    (void)crypto::fill_random(noise);
    ChunkSpan span{};
    REQUIRE(store.append_chunk(noise, span));
    // raw frame: 1 method byte + AEAD overhead (nonce 24 + tag 16).
    CHECK(span.length == noise.size() + 1 + 40);
    std::vector<uint8_t> out;
    REQUIRE(store.read_chunk(span, out));
    CHECK(out == noise);
    std::fclose(fp);
}

TEST(chunk_store_unframed_layout_unchanged)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), false);
    const auto payload = pattern(1000, 3);
    ChunkSpan span{};
    REQUIRE(store.append_chunk(payload, span));
    CHECK(span.length == payload.size() + 40);    // exactly as before Phase 26
    std::vector<uint8_t> out;
    REQUIRE(store.read_chunk(span, out));
    CHECK(out == payload);
    std::fclose(fp);
}

TEST(chunk_store_framed_failed_read_leaves_out_empty)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), true);

    const std::vector<uint8_t> compressible(200 * 1024, 0x42);
    ChunkSpan span{};
    REQUIRE(store.append_chunk(compressible, span));

    // Flip a byte inside the ciphertext region to trigger decrypt failure.
    const long cipher_pos = static_cast<long>(span.offset + crypto::NONCE_SIZE + 30);
    REQUIRE(std::fseek(fp, cipher_pos, SEEK_SET) == 0);
    int c = std::fgetc(fp);
    REQUIRE(c != EOF);
    REQUIRE(std::fseek(fp, cipher_pos, SEEK_SET) == 0);
    REQUIRE(std::fputc(c ^ 0xFF, fp) != EOF);
    std::fflush(fp);

    // Pre-load out with sentinel content (as if from a previous operation).
    crypto::SecureBytes out;
    REQUIRE(out.resize(16));
    // read_chunk must fail AND leave out empty (not with stale caller content).
    CHECK_FALSE(store.read_chunk(span, out));
    CHECK_EQ(out.size(), size_t(0));
    std::fclose(fp);
}

TEST(chunk_store_out_of_bounds_secure_read_clears_stale_output)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), true);

    crypto::SecureBytes out;
    REQUIRE(out.resize(16));
    CHECK_FALSE(store.read_chunk(ChunkSpan{.offset = 1, .length = 64}, out));
    CHECK_EQ(out.size(), size_t(0));
    std::fclose(fp);
}

// Covers ChunkStore::append_at_end's post-write flush-failure path. /dev/full
// accepts a small buffered write but fails the fflush with ENOSPC — exactly the
// case the flush guard exists for (a partial append must be reported, not
// silently accepted). Linux-only device; skipped elsewhere.
#ifndef _WIN32
TEST(chunk_store_append_reports_flush_failure)
{
    auto key = random_key();
    std::FILE* fp = std::fopen("/dev/full", "wb");
    if (fp == nullptr) return;  // environment without /dev/full: nothing to assert
    ChunkStore store(fp, key.as_span(), false);

    // A small payload buffers cleanly on fwrite; the failure surfaces at fflush.
    auto raw = pattern(64, 9);
    uint64_t offset = 0;
    CHECK_FALSE(store.append_raw(raw, offset));

    std::fclose(fp);
}
#endif

TEST(chunk_store_write_raw_at_overwrites_in_place)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), false);

    // Lay down 1000 bytes, then overwrite the middle 200 at offset 300.
    auto base = pattern(1000, 4);
    uint64_t off0 = 0;
    REQUIRE(store.append_raw(base, off0));
    auto patch = pattern(200, 9);
    REQUIRE(store.write_raw_at(300, patch));

    std::vector<uint8_t> out;
    REQUIRE(store.read_raw(300, 200, out));
    CHECK_BYTES_EQ(std::span<const uint8_t>(out), std::span<const uint8_t>(patch));
    // Bytes outside the patch are untouched.
    REQUIRE(store.read_raw(0, 300, out));
    CHECK_BYTES_EQ(std::span<const uint8_t>(out),
                   std::span<const uint8_t>(base).subspan(0, 300));
    REQUIRE(store.read_raw(500, 500, out));
    CHECK_BYTES_EQ(std::span<const uint8_t>(out),
                   std::span<const uint8_t>(base).subspan(500, 500));
    std::fclose(fp);
}

TEST(chunk_store_write_raw_at_end_extends_file)
{
    auto key = random_key();
    std::FILE* fp = std::tmpfile();
    REQUIRE(fp != nullptr);
    ChunkStore store(fp, key.as_span(), false);

    auto base = pattern(100, 1);
    uint64_t off0 = 0;
    REQUIRE(store.append_raw(base, off0));
    auto tail = pattern(50, 2);
    REQUIRE(store.write_raw_at(100, tail));  // exactly at EOF

    std::vector<uint8_t> out;
    REQUIRE(store.read_raw(100, 50, out));
    CHECK_BYTES_EQ(std::span<const uint8_t>(out), std::span<const uint8_t>(tail));
    std::fclose(fp);
}
