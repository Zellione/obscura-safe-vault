#include "test_framework.h"

#ifdef OSV_VENDORED_ARCHIVE

#include "ui/archive_reader.h"
#include "archive_test_helpers.h"

#include <cstring>
#include <string>
#include <vector>

using archivetest::fake_bytes;
using archivetest::fresh_path;
using archivetest::make_archive;
using archivetest::read_file;
using archivetest::make_encrypted_zip;

TEST(archive_reader_opens_tar_and_lists_entries)
{
    auto bytes = read_file(make_archive({{"a.jpg", fake_bytes(1)}, {"sub/b.jpg", fake_bytes(2)}},
                                        "ustar", fresh_path("reader_tar_list.tar")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));
    REQUIRE(r.entries().size() == size_t{2});
    CHECK_EQ(r.entries()[0].path, "a.jpg");
    CHECK_FALSE(r.entries()[0].is_dir);
    CHECK_EQ(r.entries()[1].path, "sub/b.jpg");
}

TEST(archive_reader_extract_returns_correct_bytes)
{
    const auto data1 = fake_bytes(10, 100);
    const auto data2 = fake_bytes(20, 200);
    auto bytes = read_file(make_archive({{"one.jpg", data1}, {"two.jpg", data2}},
                                        "ustar", fresh_path("reader_tar_extract.tar")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));

    crypto::SecureBytes out;
    REQUIRE(r.extract(0, out));
    REQUIRE(out.size() == data1.size());
    CHECK(std::memcmp(out.data(), data1.data(), data1.size()) == 0);

    REQUIRE(r.extract(1, out));
    REQUIRE(out.size() == data2.size());
    CHECK(std::memcmp(out.data(), data2.data(), data2.size()) == 0);
}

TEST(archive_reader_open_rejects_garbage)
{
    std::vector<uint8_t> junk{0x00, 0x01, 0x02, 0x03, 'n', 'o', 't', 'a', 'n', 'a', 'r', 'c'};
    ui::ArchiveReader r;
    CHECK_FALSE(r.open(junk));
}

TEST(archive_reader_extract_out_of_range_index_fails)
{
    auto bytes = read_file(make_archive({{"a.jpg", fake_bytes(1)}}, "ustar", fresh_path("reader_tar_oob.tar")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));
    crypto::SecureBytes out;
    CHECK_FALSE(r.extract(5, out));
}

TEST(archive_reader_lists_explicit_directory_entries)
{
    auto bytes = read_file(make_archive({{"sub/", {}}}, "ustar", fresh_path("reader_tar_dir.tar")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));
    REQUIRE(r.entries().size() == size_t{1});
    CHECK(r.entries()[0].is_dir);
}

TEST(archive_reader_opens_7z_and_extracts)
{
    const auto data = fake_bytes(42, 500);
    auto bytes = read_file(make_archive({{"page.jpg", data}}, "7zip", fresh_path("reader_7z.7z")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));
    REQUIRE(r.entries().size() == size_t{1});
    crypto::SecureBytes out;
    REQUIRE(r.extract(0, out));
    REQUIRE(out.size() == data.size());
    CHECK(std::memcmp(out.data(), data.data(), data.size()) == 0);
}

TEST(archive_reader_opens_gzip_compressed_tar)
{
    const auto data = fake_bytes(5, 300);
    auto bytes = read_file(make_archive({{"gz.jpg", data}}, "gnutar_gz", fresh_path("reader_targz.tar.gz")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));
    REQUIRE(r.entries().size() == size_t{1});
    crypto::SecureBytes out;
    REQUIRE(r.extract(0, out));
    REQUIRE(out.size() == data.size());
    CHECK(std::memcmp(out.data(), data.data(), data.size()) == 0);
}

TEST(archive_reader_opens_xz_compressed_tar)
{
    const auto data = fake_bytes(7, 300);
    auto bytes = read_file(make_archive({{"xz.jpg", data}}, "gnutar_xz", fresh_path("reader_tarxz.tar.xz")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));
    REQUIRE(r.entries().size() == size_t{1});
    crypto::SecureBytes out;
    REQUIRE(r.extract(0, out));
    REQUIRE(out.size() == data.size());
    CHECK(std::memcmp(out.data(), data.data(), data.size()) == 0);
}

// Real RAR fixture: libarchive has no RAR *writer* (format is proprietary;
// only decode is implemented), so unlike every other format above this can't
// be synthesized via make_archive(). Reused verbatim from libarchive's own
// test corpus (BSD-2-Clause, same license as the vendored library itself):
// vendor/libarchive/libarchive/test/test_read_format_rar.rar.uu, decoded from
// uuencoding. Ground truth for the exact entries/content below is
// test_read_format_rar_basic() in vendor/libarchive/libarchive/test/test_read_format_rar.c.
TEST(archive_reader_opens_rar_and_lists_entries)
{
    auto bytes = read_file(OSV_UI_FIXTURE_DIR "/test_read_format_rar.rar");
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));
    REQUIRE(r.entries().size() == size_t{5});
    CHECK_EQ(r.entries()[0].path, "test.txt");
    CHECK_FALSE(r.entries()[0].is_dir);
    CHECK_EQ(r.entries()[1].path, "testlink");
    CHECK_EQ(r.entries()[2].path, "testdir/test.txt");
    CHECK(r.entries()[3].is_dir);
    CHECK_EQ(r.entries()[3].path, "testdir");
    CHECK(r.entries()[4].is_dir);
    CHECK_EQ(r.entries()[4].path, "testemptydir");
}

TEST(archive_reader_extracts_rar_entry_bytes)
{
    auto bytes = read_file(OSV_UI_FIXTURE_DIR "/test_read_format_rar.rar");
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));

    crypto::SecureBytes out;
    REQUIRE(r.extract(0, out));
    const std::string expected = "test text document\r\n";
    REQUIRE(out.size() == expected.size());
    CHECK(std::memcmp(out.data(), expected.data(), expected.size()) == 0);
}

TEST(archive_reader_opens_encrypted_zip_with_correct_password)
{
    const auto data = fake_bytes(11, 80);
    auto bytes = read_file(make_encrypted_zip(
        {{"secret.jpg", data}}, "hunter2", fresh_path("reader_enc_ok.zip")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes, "hunter2"));
    REQUIRE(r.entries().size() == size_t{1});
    crypto::SecureBytes out;
    REQUIRE(r.extract(0, out));
    REQUIRE(out.size() == data.size());
    CHECK(std::memcmp(out.data(), data.data(), data.size()) == 0);
}

TEST(archive_reader_extract_fails_with_wrong_password)
{
    const auto data = fake_bytes(12, 80);
    auto bytes = read_file(make_encrypted_zip(
        {{"secret.jpg", data}}, "hunter2", fresh_path("reader_enc_wrong.zip")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes, "wrong-password"));
    REQUIRE(r.entries().size() == size_t{1});
    crypto::SecureBytes out;
    CHECK_FALSE(r.extract(0, out));
}

TEST(archive_reader_extract_fails_with_no_password)
{
    const auto data = fake_bytes(13, 80);
    auto bytes = read_file(make_encrypted_zip(
        {{"secret.jpg", data}}, "hunter2", fresh_path("reader_enc_none.zip")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));   // no passphrase argument
    REQUIRE(r.entries().size() == size_t{1});
    crypto::SecureBytes out;
    CHECK_FALSE(r.extract(0, out));
}

TEST(archive_reader_extract_failed_needs_password_true_for_wrong_password)
{
    const auto data = fake_bytes(14, 80);
    auto bytes = read_file(make_encrypted_zip(
        {{"secret.jpg", data}}, "hunter2", fresh_path("reader_enc_flag.zip")));
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes, "wrong-password"));
    crypto::SecureBytes out;
    REQUIRE(!r.extract(0, out));   // no REQUIRE_FALSE macro in this test framework
    CHECK(r.extract_failed_needs_password());
}

TEST(archive_error_is_passphrase_issue_matches_libarchive_wording)
{
    // Exact literal strings from vendor/libarchive/libarchive/
    // archive_read_support_format_zip.c's init_traditional_PKWARE_decryption
    // and init_WinZip_AES_decryption.
    CHECK(ui::archive_error_is_passphrase_issue("Passphrase required for this entry"));
    CHECK(ui::archive_error_is_passphrase_issue("Incorrect passphrase"));
    // A WinZip AES entry fails differently when no crypto backend is
    // compiled in (this project's build: -DENABLE_OPENSSL=OFF etc., see
    // scripts/build_codecs.sh) — no password could ever satisfy this, so it
    // must NOT be classified as a passphrase issue, or the UI would loop
    // forever re-prompting for a password that can never work.
    CHECK_FALSE(ui::archive_error_is_passphrase_issue(
        "Decryption is unsupported due to lack of crypto library"));
    CHECK_FALSE(ui::archive_error_is_passphrase_issue("Truncated ZIP file data"));
    CHECK_FALSE(ui::archive_error_is_passphrase_issue(""));
}

#endif // OSV_VENDORED_ARCHIVE
