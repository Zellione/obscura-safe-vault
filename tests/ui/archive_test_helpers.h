#pragma once

// Shared helpers for the libarchive-backed (7z/RAR/TAR) reader + import tests.
// Mirrors zip_test_helpers.h's role for the miniz-backed ZIP/CBZ tests: builds
// fixture archives via libarchive's OWN writer API at test time (no committed
// binary fixtures), so the same library round-trips its own output.

#ifdef OSV_VENDORED_ARCHIVE

#include <archive.h>
#include <archive_entry.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace archivetest {

namespace fs = std::filesystem;

// Synthetic "image": valid JPEG SOI/APP0 magic (so image::detect_format
// routes to Vault::add_image, mirroring ziptest::fake_jpeg) followed by a
// seed-derived tail, so each blob is distinct and byte-checkable.
inline std::vector<uint8_t> fake_bytes(uint8_t seed, size_t n = 64)
{
    std::vector<uint8_t> v{0xFF, 0xD8, 0xFF, 0xE0};
    for (size_t i = 0; i < n; ++i) v.push_back(static_cast<uint8_t>(seed + i));
    return v;
}

inline fs::path fresh_path(const char* name)
{
    fs::path p = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove(p, ec);
    return p;
}

// Write items (archive-path -> bytes; a trailing '/' with empty bytes creates
// an explicit directory entry) as a libarchive-produced archive of
// `format_name` ("7zip" | "ustar" | "gnutar_gz" | "gnutar_xz") to `out`.
// Returns `out` for chaining, mirroring ziptest::make_archive's signature.
inline fs::path make_archive(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& items,
                             const char* format_name, const fs::path& out)
{
    struct archive* a = archive_write_new();
    if (std::strcmp(format_name, "7zip") == 0) {
        archive_write_set_format_7zip(a);
    } else if (std::strcmp(format_name, "gnutar_gz") == 0) {
        archive_write_set_format_gnutar(a);
        archive_write_add_filter_gzip(a);
    } else if (std::strcmp(format_name, "gnutar_xz") == 0) {
        archive_write_set_format_gnutar(a);
        archive_write_add_filter_xz(a);
    } else {
        archive_write_set_format_ustar(a);
    }
    archive_write_open_filename(a, out.string().c_str());

    for (const auto& [path, bytes] : items) {
        struct archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, path.c_str());
        const bool is_dir = !path.empty() && path.back() == '/';
        if (is_dir) {
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_perm(entry, 0755);
        } else {
            archive_entry_set_size(entry, static_cast<int64_t>(bytes.size()));
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
        }
        archive_write_header(a, entry);
        if (!is_dir) archive_write_data(a, bytes.data(), bytes.size());
        archive_entry_free(entry);
    }
    archive_write_close(a);
    archive_write_free(a);
    return out;
}

// Like make_archive, but for a ZIP archive using traditional (ZipCrypto)
// encryption — libarchive's own writer supports this natively (no external
// crypto lib needed, unlike WinZip AES), so the same library that reads it
// back in ArchiveReader can produce a real encrypted fixture at test time.
inline fs::path make_encrypted_zip(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& items,
                                   const char* password, const fs::path& out)
{
    struct archive* a = archive_write_new();
    archive_write_set_format_zip(a);
    archive_write_set_options(a, "zip:encryption=traditional");
    archive_write_set_passphrase(a, password);
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
    return out;
}


// Read a whole file back into memory (used by the ArchiveReader-level tests,
// which operate on in-memory buffers rather than paths).
inline std::vector<uint8_t> read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    const std::streamoff sz = f.tellg();
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

} // namespace archivetest

#endif // OSV_VENDORED_ARCHIVE
