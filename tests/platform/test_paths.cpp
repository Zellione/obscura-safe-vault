#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <string>

#if !defined(_WIN32)
#  include <sys/stat.h>
#endif

#include "platform/path_utf8.h"
#include "platform/paths.h"

TEST(paths_default_vault_filename)
{
    CHECK(platform::default_vault_path().filename() == "vault.osv");
}

TEST(paths_default_vault_under_config_dir)
{
    auto cfg = platform::config_dir();
    if (!cfg.empty())
        CHECK(platform::default_vault_path().parent_path() == cfg);
}

TEST(paths_read_file_roundtrip)
{
    auto tmp = std::filesystem::temp_directory_path() / "osv_paths_test.bin";
    const std::vector<uint8_t> data{1, 2, 3, 4, 250};
    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }
    auto got = platform::read_file(tmp, data.size());
    REQUIRE(got.has_value());
    CHECK_BYTES_EQ(std::span<const uint8_t>(*got), std::span<const uint8_t>(data));
    std::filesystem::remove(tmp);
}

TEST(paths_read_file_missing_returns_nullopt)
{
    CHECK_FALSE(platform::read_file("/no/such/osv/file.xyz", 1024).has_value());
}

TEST(paths_read_file_empty_file_returns_empty_vector)
{
    const auto tmp = std::filesystem::temp_directory_path() / "osv_paths_empty.bin";
    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fclose(f);
    }
    const auto got = platform::read_file(tmp, 0);
    REQUIRE(got.has_value());
    CHECK_TRUE(got->empty());
    std::filesystem::remove(tmp);
}

TEST(paths_write_new_keyfile_creates_random_bytes)
{
    const auto a = std::filesystem::temp_directory_path() / "osv_keyfile_a.key";
    const auto b = std::filesystem::temp_directory_path() / "osv_keyfile_b.key";
    std::filesystem::remove(a);
    std::filesystem::remove(b);

    REQUIRE(platform::write_new_keyfile(a));
    REQUIRE(platform::write_new_keyfile(b));

    const auto fa = platform::read_keyfile(a);
    const auto fb = platform::read_keyfile(b);
    REQUIRE(fa.has_value() && fb.has_value());
    CHECK_EQ(fa->size(), platform::KEYFILE_SIZE);
    CHECK_EQ(fb->size(), platform::KEYFILE_SIZE);
    // Two CSPRNG keyfiles colliding is a 2^-512 event.
    CHECK_FALSE(testing::bytes_equal(*fa, *fb));

    std::filesystem::remove(a);
    std::filesystem::remove(b);
}

TEST(paths_write_new_keyfile_refuses_to_overwrite)
{
    // Overwriting an existing keyfile with fresh random bytes would permanently
    // lock every vault bound to it; creation must refuse.
    const auto p = std::filesystem::temp_directory_path() / "osv_keyfile_keep.key";
    std::filesystem::remove(p);
    REQUIRE(platform::write_new_keyfile(p));
    const auto before = platform::read_keyfile(p);
    REQUIRE(before.has_value());

    CHECK_FALSE(platform::write_new_keyfile(p));
    const auto after = platform::read_keyfile(p);
    REQUIRE(after.has_value());
    CHECK_BYTES_EQ(std::span<const uint8_t>(*after), std::span<const uint8_t>(*before));

    std::filesystem::remove(p);
}

TEST(paths_write_new_keyfile_fails_on_bad_path)
{
    CHECK_FALSE(platform::write_new_keyfile("/no/such/dir/osv.key"));
}

TEST(paths_read_file_refuses_file_over_limit)
{
    const auto p = std::filesystem::temp_directory_path() / "osv_read_limit.bin";
    {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "12345";
    }
    CHECK_FALSE(platform::read_file(p, 4).has_value());
    const auto exact = platform::read_file(p, 5);
    REQUIRE(exact.has_value());
    CHECK_EQ(exact->size(), size_t{5});
    std::filesystem::remove(p);
}

#if !defined(_WIN32)
TEST(paths_write_new_keyfile_is_owner_only_despite_umask)
{
    const auto p = std::filesystem::temp_directory_path() / "osv_keyfile_mode.key";
    std::filesystem::remove(p);
    const mode_t previous = ::umask(0);
    const bool created = platform::write_new_keyfile(p);
    ::umask(previous);
    REQUIRE(created);

    struct stat st {};
    REQUIRE(::stat(p.c_str(), &st) == 0);
    CHECK_EQ(st.st_mode & 0777, mode_t{0600});
    std::filesystem::remove(p);
}
#endif

// --- normalize_user_path ---------------------------------------------------
//
// The boundary every externally-chosen path crosses before it can reach fopen():
// the native file dialog's raw C strings, and the lines read back out of
// vaults.list. See src/platform/paths.h for why this does NOT confine the result
// to a base directory.

TEST(normalize_user_path_rejects_empty_and_nul_and_overlong)
{
    CHECK_FALSE(platform::normalize_user_path("").has_value());

    // An embedded NUL would truncate the string fopen() actually receives, so the
    // opened path would differ from the validated one.
    CHECK_FALSE(platform::normalize_user_path(std::string("/tmp/a\0/etc/passwd", 18)).has_value());

    CHECK_FALSE(platform::normalize_user_path(
                    std::string(platform::MAX_USER_PATH_BYTES + 1, 'a')).has_value());
}

// generic_string() throughout: lexically_normal() renders with the platform's
// preferred separator, so .string() is "\home\u\a.osv" on Windows. The separator
// is not what these tests are about.
TEST(normalize_user_path_collapses_traversal_components)
{
    auto p = platform::normalize_user_path("/home/u/vaults/../vaults/a.osv");
    REQUIRE(p.has_value());
    CHECK_EQ(p->generic_string(), std::string("/home/u/vaults/a.osv"));

    auto q = platform::normalize_user_path("/home/u/./a.osv");
    REQUIRE(q.has_value());
    CHECK_EQ(q->generic_string(), std::string("/home/u/a.osv"));
}

// An ordinary absolute path — the overwhelmingly common case — must survive
// completely untouched. Vaults legitimately live anywhere, including on
// removable media, so normalization must not become confinement.
TEST(normalize_user_path_leaves_an_ordinary_path_alone)
{
    auto p = platform::normalize_user_path("/media/usb/photos.osv");
    REQUIRE(p.has_value());
    CHECK_EQ(p->generic_string(), std::string("/media/usb/photos.osv"));
}

// normalize_user_path must decode its input as UTF-8 (SDL dialogs hand back
// UTF-8), not the ANSI code page — byte-identical round-trip is the contract.
TEST(normalize_user_path_preserves_utf8_bytes)
{
    const std::string raw = "vaults/\xE6\x97\xA5\xE6\x9C\xAC/\xF0\x9F\x96\xBC.osv";
    const auto p = platform::normalize_user_path(raw);
    REQUIRE(p.has_value());
    CHECK_EQ(platform::path_to_utf8_generic(*p), raw);
}

// --- normalize_external_path_utf8 -------------------------------------------
//
// The dialog callbacks store their picked paths as std::string, and project
// convention says a std::string holding a path is UTF-8. Phase 70 fixed every
// conversion EXCEPT the callbacks themselves: they rendered the normalized
// path with path::string(), which on Windows converts through the ANSI code
// page and THROWS std::system_error for a CJK filename — unhandled inside the
// SDL dialog callback, i.e. the "import of archives and files crashes on
// Japanese/Chinese names" report (Phase 72). This helper is the one sanctioned
// normalize→UTF-8 conversion for externally-chosen paths.

TEST(normalize_external_path_utf8_preserves_cjk_bytes)
{
    // 日本語/中文相册.7z — not representable in any single ANSI code page.
    const std::string raw =
        "vaults/\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E/"
        "\xE4\xB8\xAD\xE6\x96\x87\xE7\x9B\xB8\xE5\x86\x8C.7z";
    const auto s = platform::normalize_external_path_utf8(raw);
    REQUIRE(s.has_value());
    // Compare in generic form: lexically_normal renders with the platform's
    // preferred separator ('\' on Windows), which is not what this test checks.
    CHECK_EQ(platform::path_to_utf8_generic(platform::utf8_to_path(*s)), raw);
}

TEST(normalize_external_path_utf8_rejects_what_normalize_user_path_rejects)
{
    CHECK_FALSE(platform::normalize_external_path_utf8("").has_value());
    CHECK_FALSE(
        platform::normalize_external_path_utf8(std::string("/tmp/a\0b", 8)).has_value());
}
