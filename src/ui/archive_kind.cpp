#include "ui/archive_kind.h"

#include <array>
#include <cctype>
#include <span>
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

// Magic signatures as BYTES, not string literals. `\x` escapes are greedy in
// C++ — a magic byte followed by a hex-digit character silently absorbs it —
// and these are binary anyway, so a byte array says what is meant.
constexpr std::array<uint8_t, 2> MAGIC_GZIP{0x1F, 0x8B};
constexpr std::array<uint8_t, 4> MAGIC_ZIP{'P', 'K', 0x03, 0x04};
constexpr std::array<uint8_t, 6> MAGIC_7Z{'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
// Common to RAR4 and RAR5; the byte after it distinguishes them and libarchive
// dispatches on that for us.
constexpr std::array<uint8_t, 6> MAGIC_RAR{'R', 'a', 'r', '!', 0x1A, 0x07};
constexpr std::array<uint8_t, 5> MAGIC_TAR{'u', 's', 't', 'a', 'r'};

// True if `bytes` carries `magic` at `offset`. Safe on short/empty buffers.
[[nodiscard]] bool has_magic_at(std::span<const uint8_t> bytes, size_t offset,
                                std::span<const uint8_t> magic)
{
    if (bytes.size() < offset + magic.size()) {
        return false;
    }
    for (size_t i = 0; i < magic.size(); ++i) {
        if (bytes[offset + i] != magic[i]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool has_magic(std::span<const uint8_t> bytes, std::span<const uint8_t> magic)
{
    return has_magic_at(bytes, 0, magic);
}

// The extension must be preceded by an actual stem: ".zip" is a dotfile, not a
// ZIP named "".
[[nodiscard]] bool extension_is(std::string_view lower_name, std::string_view suffix)
{
    return lower_name.size() > suffix.size() && lower_name.ends_with(suffix);
}

// The single source of truth for "which extensions name an archive", shared by
// is_archive_name (plan time, names only) and detect_archive_kind (extract
// time, names + magic). Longest/most specific first: ".tar.gz" must be tried
// before ".tar" would ever see it.
constexpr std::array<std::string_view, 7> kArchiveExts{
    ".tar.gz", ".tgz", ".zip", ".cbz", ".7z", ".rar", ".tar"};

} // namespace

std::string_view archive_extension_of(std::string_view name)
{
    const std::string lower = lowered(name);
    for (const std::string_view ext : kArchiveExts) {
        if (extension_is(lower, ext)) {
            return ext;
        }
    }
    return {};
}

bool is_archive_name(std::string_view name)
{
    return !archive_extension_of(name).empty();
}

ArchiveKind detect_archive_kind(std::string_view filename, std::span<const uint8_t> bytes)
{
    using enum ArchiveKind;

    const std::string name = lowered(filename);

    // Longest/most specific extensions first: ".tar.gz" must not match ".gz"
    // handling or be mistaken for a plain ".tar".
    if (extension_is(name, ".tar.gz") || extension_is(name, ".tgz")) {
        return has_magic(bytes, MAGIC_GZIP) ? TarGz : None;
    }
    if (extension_is(name, ".zip")) {
        return has_magic(bytes, MAGIC_ZIP) ? Zip : None;
    }
    if (extension_is(name, ".cbz")) {
        return has_magic(bytes, MAGIC_ZIP) ? Cbz : None;
    }
    if (extension_is(name, ".7z")) {
        return has_magic(bytes, MAGIC_7Z) ? SevenZip : None;
    }
    if (extension_is(name, ".rar")) {
        return has_magic(bytes, MAGIC_RAR) ? Rar : None;
    }
    if (extension_is(name, ".tar")) {
        return has_magic_at(bytes, TAR_MAGIC_OFFSET, MAGIC_TAR) ? Tar : None;
    }
    return None;
}

} // namespace ui
