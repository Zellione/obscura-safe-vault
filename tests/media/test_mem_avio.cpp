#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <cstdio>
#include <span>
#include <vector>

#include "media/mem_avio.h"

extern "C" {
#include <libavformat/avio.h>
}

namespace {

// Simple test data: just a byte sequence
const std::vector<uint8_t> kTestData{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};

}  // namespace

TEST(mem_avio_read_sequential)
{
    media::MemAvio avio(kTestData);
    REQUIRE(avio.valid());

    uint8_t buf[8];
    int n = avio_read(avio.ctx(), buf, 8);
    REQUIRE(n == 8);
    CHECK(buf[0] == 0x00);
    CHECK(buf[7] == 0x07);

    // Read next chunk
    n = avio_read(avio.ctx(), buf, 8);
    REQUIRE(n == 8);
    CHECK(buf[0] == 0x08);
    CHECK(buf[7] == 0x0f);
}

TEST(mem_avio_seek_set)
{
    media::MemAvio avio(kTestData);
    REQUIRE(avio.valid());

    // Seek to position 4
    int64_t pos = avio_seek(avio.ctx(), 4, SEEK_SET);
    REQUIRE(pos == 4);

    uint8_t buf[4];
    int n = avio_read(avio.ctx(), buf, 4);
    REQUIRE(n == 4);
    CHECK(buf[0] == 0x04);
    CHECK(buf[3] == 0x07);
}

TEST(mem_avio_seek_cur)
{
    media::MemAvio avio(kTestData);
    REQUIRE(avio.valid());

    // Read 5 bytes
    uint8_t buf[5];
    avio_read(avio.ctx(), buf, 5);

    // Seek forward 3 more bytes from current position
    int64_t pos = avio_seek(avio.ctx(), 3, SEEK_CUR);
    REQUIRE(pos == 8);  // 5 + 3

    int n = avio_read(avio.ctx(), buf, 4);
    REQUIRE(n == 4);
    CHECK(buf[0] == 0x08);
}

TEST(mem_avio_seek_end)
{
    media::MemAvio avio(kTestData);
    REQUIRE(avio.valid());

    // Seek from beginning to position 20 (4 bytes from the end)
    int sz = static_cast<int>(kTestData.size());
    int64_t pos = avio_seek(avio.ctx(), sz - 4, SEEK_SET);
    CHECK(pos == sz - 4);

    uint8_t buf[8];
    int n = avio_read(avio.ctx(), buf, 8);
    REQUIRE(n == 4);  // Only 4 bytes left
    CHECK(buf[0] == kTestData[sz - 4]);
}

TEST(mem_avio_seek_size_query)
{
    media::MemAvio avio(kTestData);
    REQUIRE(avio.valid());

    // Query size using AVSEEK_SIZE
    int64_t sz = avio_seek(avio.ctx(), 0, AVSEEK_SIZE);
    REQUIRE(sz == static_cast<int64_t>(kTestData.size()));
}

TEST(mem_avio_seek_before_start_fails)
{
    media::MemAvio avio(kTestData);
    REQUIRE(avio.valid());

    // Try to seek before start
    int64_t pos = avio_seek(avio.ctx(), -100, SEEK_SET);
    CHECK(pos < 0);  // Should return error code
}

TEST(mem_avio_eof)
{
    media::MemAvio avio(kTestData);
    REQUIRE(avio.valid());

    // Seek to end
    int sz = static_cast<int>(kTestData.size());
    avio_seek(avio.ctx(), sz, SEEK_SET);

    // Try to read from EOF
    uint8_t buf[4];
    int n = avio_read(avio.ctx(), buf, 4);
    // Should return 0 or error at EOF
    CHECK(n <= 0);
}

#endif  // OSV_VENDORED_AV
