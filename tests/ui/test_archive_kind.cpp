// Phase 53: archive-kind classification for recursive import.
//
// Extension proposes, magic bytes confirm. Both halves matter: an extension
// alone would let a renamed .jpg drag the importer into libarchive, and magic
// alone would make every .docx (a ZIP) a candidate sub-gallery.

#include "test_framework.h"
#include "ui/archive_kind.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

using ui::ArchiveKind;
using ui::detect_archive_kind;

namespace {

std::vector<uint8_t> bytes_of(std::string_view s)
{
    return {s.begin(), s.end()};
}

// A minimal 512-byte tar header carries "ustar" at offset 257.
std::vector<uint8_t> tar_header()
{
    std::vector<uint8_t> v(512, 0);
    const std::string_view magic = "ustar";
    for (size_t i = 0; i < magic.size(); ++i) {
        v[257 + i] = static_cast<uint8_t>(magic[i]);
    }
    return v;
}

const auto ZIP_MAGIC   = bytes_of("PK\x03\x04rest-of-file");
const auto SEVENZ_MAGIC = bytes_of("7z\xBC\xAF\x27\x1C-rest");
const auto RAR4_MAGIC  = bytes_of("Rar!\x1A\x07\x00-rest");
const auto RAR5_MAGIC  = bytes_of("Rar!\x1A\x07\x01\x00-rest");
const auto GZIP_MAGIC  = bytes_of("\x1F\x8B\x08-rest-of-file");

} // namespace

TEST(archive_kind_detects_zip)
{
    CHECK(detect_archive_kind("photos.zip", ZIP_MAGIC) == ArchiveKind::Zip);
}

TEST(archive_kind_detects_cbz_distinctly_from_zip)
{
    // Same magic as ZIP; only the extension separates them, because a .cbz
    // plans as a flat page gallery and a .zip mirrors its tree.
    CHECK(detect_archive_kind("chapter1.cbz", ZIP_MAGIC) == ArchiveKind::Cbz);
}

TEST(archive_kind_detects_7z)
{
    CHECK(detect_archive_kind("backup.7z", SEVENZ_MAGIC) == ArchiveKind::SevenZip);
}

TEST(archive_kind_detects_rar4)
{
    CHECK(detect_archive_kind("scans.rar", RAR4_MAGIC) == ArchiveKind::Rar);
}

TEST(archive_kind_detects_rar5)
{
    CHECK(detect_archive_kind("scans.rar", RAR5_MAGIC) == ArchiveKind::Rar);
}

TEST(archive_kind_detects_tar)
{
    CHECK(detect_archive_kind("stuff.tar", tar_header()) == ArchiveKind::Tar);
}

TEST(archive_kind_detects_tar_gz)
{
    CHECK(detect_archive_kind("stuff.tar.gz", GZIP_MAGIC) == ArchiveKind::TarGz);
}

TEST(archive_kind_detects_tgz_shorthand)
{
    // ".tgz" is the same thing as ".tar.gz" and shows up in the wild often
    // enough that skipping it would silently drop a nested archive.
    CHECK(detect_archive_kind("stuff.tgz", GZIP_MAGIC) == ArchiveKind::TarGz);
}

TEST(archive_kind_extension_is_case_insensitive)
{
    CHECK(detect_archive_kind("PHOTOS.ZIP", ZIP_MAGIC) == ArchiveKind::Zip);
}

// --- the extension lies -----------------------------------------------------

TEST(archive_kind_rejects_archive_extension_with_wrong_magic)
{
    // A text file renamed to .zip must not be handed to the archive reader.
    CHECK(detect_archive_kind("notreally.zip", bytes_of("hello world")) == ArchiveKind::None);
}

TEST(archive_kind_rejects_archive_magic_under_non_archive_extension)
{
    // .docx/.odt/.epub are all ZIPs. Extension proposes; without an archive
    // extension we never recurse into them.
    CHECK(detect_archive_kind("report.docx", ZIP_MAGIC) == ArchiveKind::None);
}

TEST(archive_kind_rejects_media_file)
{
    CHECK(detect_archive_kind("holiday.jpg", bytes_of("\xFF\xD8\xFF\xE0jfif")) == ArchiveKind::None);
}

// --- bounds safety: archives are untrusted input ----------------------------

TEST(archive_kind_handles_empty_bytes)
{
    CHECK(detect_archive_kind("photos.zip", {}) == ArchiveKind::None);
}

TEST(archive_kind_handles_bytes_shorter_than_magic)
{
    CHECK(detect_archive_kind("photos.zip", bytes_of("PK")) == ArchiveKind::None);
}

TEST(archive_kind_handles_tar_buffer_shorter_than_magic_offset)
{
    // "ustar" lives at offset 257; a 100-byte buffer must not be read past.
    const std::vector<uint8_t> shortbuf(100, 0);
    CHECK(detect_archive_kind("stuff.tar", shortbuf) == ArchiveKind::None);
}

TEST(archive_kind_handles_empty_filename)
{
    CHECK(detect_archive_kind("", ZIP_MAGIC) == ArchiveKind::None);
}

TEST(archive_kind_handles_filename_that_is_only_an_extension)
{
    CHECK(detect_archive_kind(".zip", ZIP_MAGIC) == ArchiveKind::None);
}
