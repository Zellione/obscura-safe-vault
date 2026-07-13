#include "test_framework.h"

#ifdef OSV_VENDORED_ARCHIVE

#include "ui/archive_reader.h"

#include <archive.h>
#include <archive_entry.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Synthetic "image" bytes: distinct per seed, byte-checkable.
std::vector<uint8_t> fake_bytes(uint8_t seed, size_t n = 64)
{
    std::vector<uint8_t> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) v.push_back(static_cast<uint8_t>(seed + i));
    return v;
}

fs::path fresh_path(const char* name)
{
    fs::path p = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove(p, ec);
    return p;
}

// Write items (path -> bytes) as a libarchive-produced archive of `format` to
// a temp file via libarchive's own writer, then read the whole file back into
// memory (mirrors ziptest::make_archive's miniz-based approach, but through
// libarchive so the same library round-trips its own output).
std::vector<uint8_t> make_archive(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& items,
                                  const char* format_name, const char* filename)
{
    fs::path out = fresh_path(filename);

    struct archive* a = archive_write_new();
    if (std::strcmp(format_name, "7zip") == 0) archive_write_set_format_7zip(a);
    else if (std::strcmp(format_name, "ustar") == 0) archive_write_set_format_ustar(a);
    else if (std::strcmp(format_name, "gnutar_gz") == 0) {
        archive_write_set_format_gnutar(a);
        archive_write_add_filter_gzip(a);
    } else if (std::strcmp(format_name, "gnutar_xz") == 0) {
        archive_write_set_format_gnutar(a);
        archive_write_add_filter_xz(a);
    }
    archive_write_open_filename(a, out.string().c_str());

    for (const auto& [path, bytes] : items) {
        struct archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, path.c_str());
        archive_entry_set_size(entry, static_cast<int64_t>(bytes.size()));
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_write_header(a, entry);
        archive_write_data(a, bytes.data(), bytes.size());
        archive_entry_free(entry);
    }
    archive_write_close(a);
    archive_write_free(a);

    std::ifstream f(out, std::ios::binary | std::ios::ate);
    const std::streamoff sz = f.tellg();
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    std::error_code ec;
    fs::remove(out, ec);
    return buf;
}

} // namespace

TEST(archive_reader_opens_tar_and_lists_entries)
{
    auto bytes = make_archive({{"a.jpg", fake_bytes(1)}, {"sub/b.jpg", fake_bytes(2)}},
                              "ustar", "reader_tar_list.tar");
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
    auto bytes = make_archive({{"one.jpg", data1}, {"two.jpg", data2}},
                              "ustar", "reader_tar_extract.tar");
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
    auto bytes = make_archive({{"a.jpg", fake_bytes(1)}}, "ustar", "reader_tar_oob.tar");
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));
    crypto::SecureBytes out;
    CHECK_FALSE(r.extract(5, out));
}

TEST(archive_reader_lists_explicit_directory_entries)
{
    struct archive* a = archive_write_new();
    archive_write_set_format_ustar(a);
    fs::path out = fresh_path("reader_tar_dir.tar");
    archive_write_open_filename(a, out.string().c_str());
    struct archive_entry* entry = archive_entry_new();
    archive_entry_set_pathname(entry, "sub/");
    archive_entry_set_filetype(entry, AE_IFDIR);
    archive_entry_set_perm(entry, 0755);
    archive_write_header(a, entry);
    archive_entry_free(entry);
    archive_write_close(a);
    archive_write_free(a);

    std::ifstream f(out, std::ios::binary | std::ios::ate);
    const std::streamoff sz = f.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), sz);
    std::error_code ec;
    fs::remove(out, ec);

    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));
    REQUIRE(r.entries().size() == size_t{1});
    CHECK(r.entries()[0].is_dir);
}

TEST(archive_reader_opens_7z_and_extracts)
{
    const auto data = fake_bytes(42, 500);
    auto bytes = make_archive({{"page.jpg", data}}, "7zip", "reader_7z.7z");
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
    auto bytes = make_archive({{"gz.jpg", data}}, "gnutar_gz", "reader_targz.tar.gz");
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
    auto bytes = make_archive({{"xz.jpg", data}}, "gnutar_xz", "reader_tarxz.tar.xz");
    ui::ArchiveReader r;
    REQUIRE(r.open(bytes));
    REQUIRE(r.entries().size() == size_t{1});
    crypto::SecureBytes out;
    REQUIRE(r.extract(0, out));
    REQUIRE(out.size() == data.size());
    CHECK(std::memcmp(out.data(), data.data(), data.size()) == 0);
}

#endif // OSV_VENDORED_ARCHIVE
