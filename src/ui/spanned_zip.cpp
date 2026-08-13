#include "ui/spanned_zip.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace ui {

namespace {

// Helper: read little-endian uint16 at offset
[[nodiscard]] uint16_t read_u16_le(const uint8_t* data, size_t offset) {
    // The shift promotes uint16_t to int, so the `|` result is an int and
    // narrowing it back on return is what clang's -Wconversion rejects. gcc
    // does not warn here, which is why this only failed on the clang CI leg.
    return static_cast<uint16_t>(static_cast<unsigned>(data[offset]) |
                                 (static_cast<unsigned>(data[offset + 1]) << 8U));
}

// Helper: read little-endian uint32 at offset
[[nodiscard]] uint32_t read_u32_le(const uint8_t* data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

// Helper: write little-endian uint16 at offset
void write_u16_le(uint8_t* data, size_t offset, uint16_t val) {
    data[offset]     = static_cast<uint8_t>(val & 0xff);
    data[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xff);
}

// Helper: write little-endian uint32 at offset
void write_u32_le(uint8_t* data, size_t offset, uint32_t val) {
    data[offset]     = static_cast<uint8_t>(val & 0xff);
    data[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xff);
    data[offset + 2] = static_cast<uint8_t>((val >> 16) & 0xff);
    data[offset + 3] = static_cast<uint8_t>((val >> 24) & 0xff);
}

// Marker signatures
constexpr std::array<uint8_t, 4> PK_SPANNING_MARKER{0x50, 0x4b, 0x07, 0x08};  // PK\x07\x08
constexpr std::array<uint8_t, 4> PK_CD_ENTRY{0x50, 0x4b, 0x01, 0x02};         // PK\x01\x02
constexpr std::array<uint8_t, 4> PK_EOCD{0x50, 0x4b, 0x05, 0x06};             // PK\x05\x06
constexpr std::array<uint8_t, 4> PK_ZIP64_LOCATOR{0x50, 0x4b, 0x06, 0x07};    // PK\x06\x07
constexpr std::array<uint8_t, 4> PK_ZIP64_EOCD{0x50, 0x4b, 0x06, 0x06};       // PK\x06\x06

// Rewrite every central-directory entry so the merged buffer is a single-disk
// archive: disk_number_start to 0, and the local-header offset to an absolute
// position. Split out of merge_spanned_zip because it carries most of that
// function's branching.
//
// Disk 0 must have the stripped marker subtracted — its stored offsets were
// measured from the ORIGINAL volume, which still had the 4 leading bytes.
// Volumes >= 1 need no adjustment; volume_offsets already accounts for it.
// Getting this wrong leaves a CD that enumerates perfectly while every
// extraction fails.
// One central-directory entry. Split out so the walk below has a single exit
// rather than two nested breaks.
enum class CdStep : uint8_t { Rewritten, EndOfEntries, Malformed };

[[nodiscard]] CdStep rewrite_one_cd_entry(std::vector<uint8_t>& merged, size_t cd_absolute,
                                          size_t cd_pos, uint32_t cd_size,
                                          const std::vector<size_t>& volume_offsets,
                                          size_t marker_size, size_t& consumed)
{
    if (std::memcmp(&merged[cd_absolute + cd_pos], PK_CD_ENTRY.data(), 4) != 0) {
        return CdStep::EndOfEntries;
    }

    const uint16_t filename_len = read_u16_le(merged.data(), cd_absolute + cd_pos + 28);
    const uint16_t extra_len    = read_u16_le(merged.data(), cd_absolute + cd_pos + 30);
    const uint16_t comment_len  = read_u16_le(merged.data(), cd_absolute + cd_pos + 32);
    if (cd_pos + 46 + filename_len + extra_len + comment_len > cd_size) {
        return CdStep::EndOfEntries;
    }

    const uint16_t disk_start   = read_u16_le(merged.data(), cd_absolute + cd_pos + 34);
    const uint32_t local_offset = read_u32_le(merged.data(), cd_absolute + cd_pos + 42);
    if (disk_start >= volume_offsets.size()) {
        return CdStep::Malformed;
    }
    if (disk_start == 0 && static_cast<size_t>(local_offset) < marker_size) {
        return CdStep::Malformed;
    }

    // Disk 0 must have the stripped marker subtracted — its stored offsets were
    // measured from the ORIGINAL volume, which still had the 4 leading bytes.
    // Volumes >= 1 need no adjustment; volume_offsets already accounts for it.
    // Getting this wrong leaves a CD that enumerates perfectly while every
    // extraction fails.
    const size_t local_abs = volume_offsets[disk_start] + static_cast<size_t>(local_offset) -
                             (disk_start == 0 ? marker_size : 0);
    if (local_abs + 4 > merged.size()) {
        return CdStep::Malformed;
    }

    write_u16_le(merged.data(), cd_absolute + cd_pos + 34, 0);
    write_u32_le(merged.data(), cd_absolute + cd_pos + 42, static_cast<uint32_t>(local_abs));

    consumed = 46 + filename_len + extra_len + comment_len;
    return CdStep::Rewritten;
}

// Rewrite every central-directory entry so the merged buffer is a single-disk
// archive: disk_number_start to 0, local-header offsets absolute.
[[nodiscard]] bool rewrite_central_directory(std::vector<uint8_t>& merged, size_t cd_absolute,
                                             uint32_t cd_size,
                                             const std::vector<size_t>& volume_offsets,
                                             size_t                     marker_size)
{
    size_t cd_pos = 0;
    while (cd_pos + 46 <= cd_size) {
        size_t       consumed = 0;
        const CdStep step     = rewrite_one_cd_entry(merged, cd_absolute, cd_pos, cd_size,
                                                     volume_offsets, marker_size, consumed);
        if (step == CdStep::Malformed) {
            return false;
        }
        if (step == CdStep::EndOfEntries) {
            break;
        }
        cd_pos += consumed;
    }
    return true;
}

// Zip64 is out of scope for v1; detect it so the merge refuses rather than
// producing a subtly wrong archive.
[[nodiscard]] bool has_zip64_markers(const std::vector<uint8_t>& merged)
{
    for (size_t i = 0; i + 4 <= merged.size(); ++i) {
        if (std::memcmp(&merged[i], PK_ZIP64_LOCATOR.data(), 4) == 0 ||
            std::memcmp(&merged[i], PK_ZIP64_EOCD.data(), 4) == 0) {
            return true;
        }
    }
    return false;
}

// EOCD, searched backwards from the end within the 64 KiB a comment may span.
[[nodiscard]] std::optional<size_t> find_eocd(const std::vector<uint8_t>& merged)
{
    if (merged.size() < 22) {
        return std::nullopt;
    }
    const size_t search_start = merged.size() > 65557 ? merged.size() - 65557 : 0;
    for (size_t i = merged.size() - 22; ; --i) {
        if (std::memcmp(&merged[i], PK_EOCD.data(), 4) == 0) {
            return i;
        }
        if (i == search_start) {
            return std::nullopt;
        }
    }
}

} // namespace

[[nodiscard]] SpannedZipResult merge_spanned_zip(
    std::span<const std::span<const uint8_t>> volumes)
{
    // Step 0: Validate input
    if (volumes.empty()) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }

    // Step 1: Calculate total size and check for spanning marker
    size_t total_size = 0;
    size_t marker_size = 0;

    if (volumes[0].size() >= 4 && std::memcmp(volumes[0].data(), PK_SPANNING_MARKER.data(), 4) == 0) {
        marker_size = 4;
    }

    // Calculate merged size
    for (size_t i = 0; i < volumes.size(); ++i) {
        if (i == 0) {
            total_size += volumes[0].size() - marker_size;
        } else {
            total_size += volumes[i].size();
        }
    }

    if (total_size == 0) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }

    // Step 2: Concatenate volumes into merged buffer
    std::vector<uint8_t> merged;
    merged.reserve(total_size);

    // Add volume 1, skipping marker if present
    const auto* vol0_data = volumes[0].data() + marker_size;
    size_t vol0_len = volumes[0].size() - marker_size;
    merged.insert(merged.end(), vol0_data, vol0_data + vol0_len);

    // Add remaining volumes
    for (size_t i = 1; i < volumes.size(); ++i) {
        const auto* vol_data = volumes[i].data();
        merged.insert(merged.end(), vol_data, vol_data + volumes[i].size());
    }

    // Step 3: Zip64 is unsupported in v1.
    if (has_zip64_markers(merged)) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Zip64Unsupported};
    }

    // Step 4: Locate the EOCD.
    const std::optional<size_t> eocd = find_eocd(merged);
    if (!eocd.has_value()) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }
    const size_t eocd_idx = *eocd;

    // Step 5: Bounds check EOCD
    if (eocd_idx + 22 > merged.size()) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }

    // Step 6: Parse EOCD fields
    uint16_t disk_with_cd       = read_u16_le(merged.data(), eocd_idx + 6);
    uint32_t cd_size            = read_u32_le(merged.data(), eocd_idx + 12);
    uint32_t cd_offset_on_disk  = read_u32_le(merged.data(), eocd_idx + 16);
    // Check the EOCD comment doesn't extend past the buffer.
    if (const uint16_t comment_length = read_u16_le(merged.data(), eocd_idx + 20);
        eocd_idx + 22 + comment_length > merged.size()) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }

    // Step 7: Build volume_offsets array to know where each disk starts
    // This is based on the actual disk-relative offsets in the merged buffer
    // Volume 0 starts at offset 0
    // Volume 1 starts at offset volumes[0].size() - marker_size
    // etc.
    std::vector<size_t> volume_offsets;
    volume_offsets.push_back(0);
    size_t cumulative = volumes[0].size() - marker_size;
    for (size_t i = 1; i < volumes.size(); ++i) {
        volume_offsets.push_back(cumulative);
        cumulative += volumes[i].size();
    }

    // Step 8: Calculate absolute CD position
    if (disk_with_cd >= volume_offsets.size()) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }

    // Same marker adjustment as the per-entry offsets below. The central
    // directory normally lives on the LAST volume, so this branch rarely
    // fires — which is exactly why it would go unnoticed.
    if (disk_with_cd == 0 && static_cast<size_t>(cd_offset_on_disk) < marker_size) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }
    size_t cd_absolute = volume_offsets[disk_with_cd]
                         + static_cast<size_t>(cd_offset_on_disk)
                         - (disk_with_cd == 0 ? marker_size : 0);

    // Step 9: Bounds check CD
    if (cd_absolute + cd_size > merged.size()) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }

    if (!rewrite_central_directory(merged, cd_absolute, cd_size, volume_offsets, marker_size)) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }

    // Step 11: Rewrite EOCD fields
    write_u16_le(merged.data(), eocd_idx + 4, 0);   // disk_num = 0
    write_u16_le(merged.data(), eocd_idx + 6, 0);   // disk_with_cd = 0
    write_u32_le(merged.data(), eocd_idx + 16,
                 static_cast<uint32_t>(cd_absolute));  // CD offset = absolute

    return SpannedZipResult{.merged = std::move(merged), .error = SpannedZipError::None};
}

} // namespace ui
