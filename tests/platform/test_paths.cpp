#include "test_framework.h"

#include <filesystem>
#include <string>

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
    auto got = platform::read_file(tmp);
    REQUIRE(got.has_value());
    CHECK_BYTES_EQ(std::span<const uint8_t>(*got), std::span<const uint8_t>(data));
    std::filesystem::remove(tmp);
}

TEST(paths_read_file_missing_returns_nullopt)
{
    CHECK_FALSE(platform::read_file("/no/such/osv/file.xyz").has_value());
}

TEST(paths_read_file_empty_file_returns_empty_vector)
{
    const auto tmp = std::filesystem::temp_directory_path() / "osv_paths_empty.bin";
    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fclose(f);
    }
    const auto got = platform::read_file(tmp);
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

    const auto fa = platform::read_file(a);
    const auto fb = platform::read_file(b);
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
    const auto before = platform::read_file(p);
    REQUIRE(before.has_value());

    CHECK_FALSE(platform::write_new_keyfile(p));
    const auto after = platform::read_file(p);
    REQUIRE(after.has_value());
    CHECK_BYTES_EQ(std::span<const uint8_t>(*after), std::span<const uint8_t>(*before));

    std::filesystem::remove(p);
}

TEST(paths_write_new_keyfile_fails_on_bad_path)
{
    CHECK_FALSE(platform::write_new_keyfile("/no/such/dir/osv.key"));
}

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

TEST(normalize_user_path_collapses_traversal_components)
{
    auto p = platform::normalize_user_path("/home/u/vaults/../vaults/a.osv");
    REQUIRE(p.has_value());
    CHECK_EQ(p->string(), std::string("/home/u/vaults/a.osv"));

    auto q = platform::normalize_user_path("/home/u/./a.osv");
    REQUIRE(q.has_value());
    CHECK_EQ(q->string(), std::string("/home/u/a.osv"));
}

// An ordinary absolute path — the overwhelmingly common case — must survive
// completely untouched. Vaults legitimately live anywhere, including on
// removable media, so normalization must not become confinement.
TEST(normalize_user_path_leaves_an_ordinary_path_alone)
{
    auto p = platform::normalize_user_path("/media/usb/photos.osv");
    REQUIRE(p.has_value());
    CHECK_EQ(p->string(), std::string("/media/usb/photos.osv"));
}
