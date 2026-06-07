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
    ChunkStore store(fp, key.as_span());

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
    ChunkStore store(fp, key.as_span());

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
    ChunkStore store(fp, key.as_span());

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
    ChunkStore store(fp, key.as_span());

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
    ChunkStore store(fp, key.as_span());

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
    ChunkStore store(fp, key.as_span());

    auto plain = pattern(32, 1);
    ChunkSpan span;
    REQUIRE(store.append_chunk(plain, span));

    // A span claiming far more than the file holds must fail, not read garbage.
    ChunkSpan bogus{.offset = span.offset, .length = span.length + 100000};
    std::vector<uint8_t> out;
    CHECK_FALSE(store.read_chunk(bogus, out));
    std::fclose(fp);
}
