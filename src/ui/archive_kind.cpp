#include "ui/archive_kind.h"

#include <cctype>
#include <string>

namespace ui {
namespace {

// Offset of the "ustar" magic inside a 512-byte tar header.
constexpr size_t TAR_MAGIC_OFFSET = 257;

[[nodiscard]] std::string lowered(std::string_view s)
{
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// True if `bytes` begins with `magic`. Safe on short/empty buffers.
[[nodiscard]] bool has_magic_at(std::span<const uint8_t> bytes, size_t offset, std::string_view magic)
{
    if (bytes.size() < offset + magic.size()) {
        return false;
    }
    for (size_t i = 0; i < magic.size(); ++i) {
        if (bytes[offset + i] != static_cast<uint8_t>(magic[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool has_magic(std::span<const uint8_t> bytes, std::string_view magic)
{
    return has_magic_at(bytes, 0, magic);
}

// The extension must be preceded by an actual stem: ".zip" is a dotfile, not a
// ZIP named "". Returns true and sets `out` to the matched suffix.
[[nodiscard]] bool extension_is(const std::string& lower_name, std::string_view suffix)
{
    return lower_name.size() > suffix.size() && lower_name.ends_with(suffix);
}

} // namespace

ArchiveKind detect_archive_kind(std::string_view filename, std::span<const uint8_t> bytes)
{
    const std::string name = lowered(filename);

    // Longest/most specific extensions first: ".tar.gz" must not match ".gz"
    // handling or be mistaken for a plain ".tar".
    if (extension_is(name, ".tar.gz") || extension_is(name, ".tgz")) {
        return has_magic(bytes, "\x1F\x8B") ? ArchiveKind::TarGz : ArchiveKind::None;
    }
    if (extension_is(name, ".zip")) {
        return has_magic(bytes, "PK\x03\x04") ? ArchiveKind::Zip : ArchiveKind::None;
    }
    if (extension_is(name, ".cbz")) {
        return has_magic(bytes, "PK\x03\x04") ? ArchiveKind::Cbz : ArchiveKind::None;
    }
    if (extension_is(name, ".7z")) {
        return has_magic(bytes, "7z\xBC\xAF\x27\x1C") ? ArchiveKind::SevenZip : ArchiveKind::None;
    }
    if (extension_is(name, ".rar")) {
        // "Rar!\x1A\x07" is common to RAR4 and RAR5; the byte that follows
        // distinguishes them, and libarchive dispatches on it for us.
        return has_magic(bytes, "Rar!\x1A\x07") ? ArchiveKind::Rar : ArchiveKind::None;
    }
    if (extension_is(name, ".tar")) {
        return has_magic_at(bytes, TAR_MAGIC_OFFSET, "ustar") ? ArchiveKind::Tar : ArchiveKind::None;
    }
    return ArchiveKind::None;
}

} // namespace ui
