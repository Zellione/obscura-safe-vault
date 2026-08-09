// tests/platform/test_path_utf8.cpp
#include "test_framework.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "platform/path_utf8.h"

namespace fs = std::filesystem;

// Round-trip must preserve every byte: CJK, emoji, Latin-1 supplement.
// This is exactly the class the old path::string() (→ ANSI code page on
// Windows) threw std::system_error on.
TEST(utf8_path_roundtrip_non_ascii)
{
    const std::string name = "caf\xC3\xA9_\xE6\x97\xA5\xE6\x9C\xAB_\xF0\x9F\x96\xBC.jpg";
    const fs::path p = platform::utf8_to_path(name);
    CHECK_EQ(platform::path_to_utf8(p), name);
}

TEST(utf8_path_roundtrip_ascii_identity)
{
    const std::string s = "plain/ascii/path.osv";
    CHECK_EQ(platform::path_to_utf8_generic(platform::utf8_to_path(s)), s);
}

// generic form uses '/' regardless of platform (vaults.list stability).
TEST(utf8_path_generic_uses_forward_slashes)
{
    const fs::path p = platform::utf8_to_path("a") / "b";
    CHECK_EQ(platform::path_to_utf8_generic(p), std::string("a/b"));
}

// fopen_path must open a file whose NAME is not ANSI-representable.
TEST(fopen_path_opens_unicode_named_file)
{
    const fs::path dir = fs::temp_directory_path();
    const fs::path file =
        dir / platform::utf8_to_path("osv_utf8_\xE6\x97\xA5\xE6\x9C\xAB\xF0\x9F\x96\xBC.bin");
    std::error_code ec; fs::remove(file, ec);

    std::FILE* w = platform::fopen_path(file, "wb");
    REQUIRE(w != nullptr);
    const char payload[] = "osv";
    CHECK_EQ(std::fwrite(payload, 1, 3, w), 3u);
    std::fclose(w);

    std::FILE* r = platform::fopen_path(file, "rb");
    REQUIRE(r != nullptr);
    char buf[4] = {};
    CHECK_EQ(std::fread(buf, 1, 3, r), 3u);
    std::fclose(r);
    CHECK_EQ(std::string(buf), std::string("osv"));
    fs::remove(file, ec);
}
