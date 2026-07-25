#include "ui/spanned_zip.h"

#include <algorithm>
#include <cstring>

namespace ui {

namespace {

// Helper: read little-endian uint16 at offset
[[nodiscard]] uint16_t read_u16_le(const uint8_t* data, size_t offset) {
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
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
constexpr uint8_t PK_SPANNING_MARKER[] = {0x50, 0x4b, 0x07, 0x08};  // PK\x07\x08
constexpr uint8_t PK_LOCAL_HEADER[]    = {0x50, 0x4b, 0x03, 0x04};  // PK\x03\x04
constexpr uint8_t PK_CD_ENTRY[]        = {0x50, 0x4b, 0x01, 0x02};  // PK\x01\x02
constexpr uint8_t PK_EOCD[]            = {0x50, 0x4b, 0x05, 0x06};  // PK\x05\x06
constexpr uint8_t PK_ZIP64_LOCATOR[]   = {0x50, 0x4b, 0x06, 0x07};  // PK\x06\x07
constexpr uint8_t PK_ZIP64_EOCD[]      = {0x50, 0x4b, 0x06, 0x06};  // PK\x06\x06

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

    if (volumes[0].size() >= 4) {
        if (std::memcmp(volumes[0].data(), PK_SPANNING_MARKER, 4) == 0) {
            marker_size = 4;
        }
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

    // Step 3: Check for Zip64 signatures (reject if found)
    for (size_t i = 0; i + 4 <= merged.size(); ++i) {
        if (std::memcmp(&merged[i], PK_ZIP64_LOCATOR, 4) == 0 ||
            std::memcmp(&merged[i], PK_ZIP64_EOCD, 4) == 0) {
            return SpannedZipResult{
                .merged = {}, .error = SpannedZipError::Zip64Unsupported};
        }
    }

    // Step 4: Find EOCD signature from the end
    if (merged.size() < 22) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }

    size_t search_start = 0;
    if (merged.size() > 65557) {
        search_start = merged.size() - 65557;
    }

    int eocd_pos = -1;
    for (int i = static_cast<int>(merged.size()) - 22; i >= static_cast<int>(search_start);
         --i) {
        if (std::memcmp(&merged[i], PK_EOCD, 4) == 0) {
            eocd_pos = i;
            break;
        }
    }

    if (eocd_pos < 0) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }

    size_t eocd_idx = static_cast<size_t>(eocd_pos);

    // Step 5: Bounds check EOCD
    if (eocd_idx + 22 > merged.size()) {
        return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
    }

    // Step 6: Parse EOCD fields
    uint16_t disk_with_cd       = read_u16_le(merged.data(), eocd_idx + 6);
    uint32_t cd_size            = read_u32_le(merged.data(), eocd_idx + 12);
    uint32_t cd_offset_on_disk  = read_u32_le(merged.data(), eocd_idx + 16);
    uint16_t comment_length     = read_u16_le(merged.data(), eocd_idx + 20);

    // Check EOCD comment doesn't extend past buffer
    if (eocd_idx + 22 + comment_length > merged.size()) {
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

    // Step 10: Walk and update CD entries
    size_t cd_pos = 0;

    while (cd_pos + 46 <= cd_size) {
        // Check CD entry signature
        if (std::memcmp(&merged[cd_absolute + cd_pos], PK_CD_ENTRY, 4) != 0) {
            break;  // End of CD entries
        }

        // Parse variable-length fields
        uint16_t filename_len =
            read_u16_le(merged.data(), cd_absolute + cd_pos + 28);
        uint16_t extra_len = read_u16_le(merged.data(), cd_absolute + cd_pos + 30);
        uint16_t comment_len = read_u16_le(merged.data(), cd_absolute + cd_pos + 32);

        // Check we don't overrun CD
        if (cd_pos + 46 + filename_len + extra_len + comment_len > cd_size) {
            break;
        }

        // Read disk_start and local_offset
        uint16_t disk_start =
            read_u16_le(merged.data(), cd_absolute + cd_pos + 34);
        uint32_t local_offset =
            read_u32_le(merged.data(), cd_absolute + cd_pos + 42);

        // Bounds check: disk_start must be valid
        if (disk_start >= volume_offsets.size()) {
            return SpannedZipResult{
                .merged = {}, .error = SpannedZipError::Malformed};
        }

        // Calculate absolute offset of local header.
        //
        // Disk 0 needs the marker subtracted: the stored offset is measured
        // from the start of the ORIGINAL volume 0, which still had the 4-byte
        // spanning marker, but the merged buffer has it stripped. Volumes >= 1
        // need no adjustment — volume_offsets already began accumulating from
        // (volumes[0].size() - marker_size), so the shift is baked in there.
        // Getting this wrong leaves a merged archive whose central directory
        // enumerates perfectly while every extraction fails.
        if (disk_start == 0 && static_cast<size_t>(local_offset) < marker_size) {
            return SpannedZipResult{.merged = {}, .error = SpannedZipError::Malformed};
        }
        size_t local_abs = volume_offsets[disk_start] + static_cast<size_t>(local_offset)
                           - (disk_start == 0 ? marker_size : 0);

        // Bounds check: local header must be in buffer and have minimum size
        if (local_abs + 4 > merged.size()) {
            return SpannedZipResult{
                .merged = {}, .error = SpannedZipError::Malformed};
        }

        // Rewrite disk_start to 0
        write_u16_le(merged.data(), cd_absolute + cd_pos + 34, 0);

        // Rewrite local_offset to absolute offset
        write_u32_le(merged.data(), cd_absolute + cd_pos + 42,
                     static_cast<uint32_t>(local_abs));

        // Move to next CD entry
        cd_pos += 46 + filename_len + extra_len + comment_len;
    }

    // Step 11: Rewrite EOCD fields
    write_u16_le(merged.data(), eocd_idx + 4, 0);   // disk_num = 0
    write_u16_le(merged.data(), eocd_idx + 6, 0);   // disk_with_cd = 0
    write_u32_le(merged.data(), eocd_idx + 16,
                 static_cast<uint32_t>(cd_absolute));  // CD offset = absolute

    return SpannedZipResult{.merged = std::move(merged), .error = SpannedZipError::None};
}

} // namespace ui
