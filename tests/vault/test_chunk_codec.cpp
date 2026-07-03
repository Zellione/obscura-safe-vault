#include "test_framework.h"

#include <vector>

#include "crypto/random.h"
#include "vault/chunk_codec.h"

using namespace vault;

static std::vector<uint8_t> zeros(size_t n) { return std::vector<uint8_t>(n, 0); }
static std::vector<uint8_t> rnd(size_t n)
{
    std::vector<uint8_t> v(n);
    (void)crypto::fill_random(v);
    return v;
}

TEST(codec_compressible_roundtrip_shrinks)
{
    const auto payload = zeros(64 * 1024);
    std::vector<uint8_t> framed;
    REQUIRE(chunk_codec::encode_frame(payload, framed));
    CHECK(framed[0] == chunk_codec::METHOD_DEFLATE);
    CHECK(framed.size() < payload.size() / 10);  // zeros compress massively
    std::vector<uint8_t> back;
    REQUIRE(chunk_codec::decode_frame(framed, back));
    CHECK(back == payload);
}

TEST(codec_incompressible_stays_raw)
{
    const auto payload = rnd(64 * 1024);
    std::vector<uint8_t> framed;
    REQUIRE(chunk_codec::encode_frame(payload, framed));
    CHECK(framed[0] == chunk_codec::METHOD_RAW);
    CHECK(framed.size() == payload.size() + 1);
    std::vector<uint8_t> back;
    REQUIRE(chunk_codec::decode_frame(framed, back));
    CHECK(back == payload);
}

TEST(codec_empty_payload)
{
    std::vector<uint8_t> framed;
    REQUIRE(chunk_codec::encode_frame({}, framed));
    CHECK(framed.size() == 1);
    CHECK(framed[0] == chunk_codec::METHOD_RAW);
    std::vector<uint8_t> back{1, 2, 3};
    REQUIRE(chunk_codec::decode_frame(framed, back));
    CHECK(back.empty());
}

TEST(codec_secure_bytes_roundtrip)
{
    const auto payload = zeros(4096);
    crypto::SecureBytes framed;
    REQUIRE(chunk_codec::encode_frame(payload, framed));
    crypto::SecureBytes back;
    REQUIRE(chunk_codec::decode_frame(framed.as_span(), back));
    REQUIRE(back.size() == payload.size());
    CHECK(std::memcmp(back.data(), payload.data(), payload.size()) == 0);
}

TEST(codec_rejects_hostile_frames)
{
    std::vector<uint8_t> out;
    CHECK(!chunk_codec::decode_frame({}, out));               // empty: no method byte

    std::vector<uint8_t> bad_method{2, 0, 0};                 // unknown method
    CHECK(!chunk_codec::decode_frame(bad_method, out));

    std::vector<uint8_t> short_deflate{1, 0, 0, 0};           // deflate but < 9-byte header
    CHECK(!chunk_codec::decode_frame(short_deflate, out));

    // Decompression bomb claim: 4-byte "stream" claiming 2^62 bytes of output.
    std::vector<uint8_t> bomb(1 + 8 + 4, 0);
    bomb[0] = chunk_codec::METHOD_DEFLATE;
    bomb[7] = 0x40;                                           // orig_len LE byte 6 -> 2^62
    CHECK(!chunk_codec::decode_frame(bomb, out));

    // orig_len mismatch: valid stream, wrong claimed length.
    const auto payload = zeros(1000);
    std::vector<uint8_t> framed;
    REQUIRE(chunk_codec::encode_frame(payload, framed));
    REQUIRE(framed[0] == chunk_codec::METHOD_DEFLATE);
    framed[1] = static_cast<uint8_t>(framed[1] ^ 0x01);       // orig_len 1000 -> 1001
    CHECK(!chunk_codec::decode_frame(framed, out));

    // Truncated deflate stream.
    std::vector<uint8_t> framed2;
    REQUIRE(chunk_codec::encode_frame(payload, framed2));
    framed2.resize(framed2.size() - 3);
    CHECK(!chunk_codec::decode_frame(framed2, out));
}
