#include "test_framework.h"

#include <cstring>
#include <span>
#include <vector>

#include "ui/spanned_zip.h"

// Helper to create a minimal ZIP local file header (30-byte minimum)
// signature + version + flags + compression + time + date + crc + compressed_size
//  + uncompressed_size + filename_len + extra_len + filename
static std::vector<uint8_t> make_local_header(std::string_view filename = "") {
    std::vector<uint8_t> lfh(30 + filename.size());
    // Signature: 0x504b0304
    lfh[0] = 0x50; lfh[1] = 0x4b; lfh[2] = 0x03; lfh[3] = 0x04;
    // Version needed: 2.0 (bytes 4-5)
    lfh[4] = 0x14; lfh[5] = 0x00;
    // General purpose bit flag (bytes 6-7): 0
    lfh[6] = 0x00; lfh[7] = 0x00;
    // Compression method (bytes 8-9): 0 (stored)
    lfh[8] = 0x00; lfh[9] = 0x00;
    // File modification time (bytes 10-11): 0
    lfh[10] = 0x00; lfh[11] = 0x00;
    // File modification date (bytes 12-13): 0
    lfh[12] = 0x00; lfh[13] = 0x00;
    // CRC-32 (bytes 14-17): 0
    lfh[14] = 0x00; lfh[15] = 0x00; lfh[16] = 0x00; lfh[17] = 0x00;
    // Compressed size (bytes 18-21): 0
    lfh[18] = 0x00; lfh[19] = 0x00; lfh[20] = 0x00; lfh[21] = 0x00;
    // Uncompressed size (bytes 22-25): 0
    lfh[22] = 0x00; lfh[23] = 0x00; lfh[24] = 0x00; lfh[25] = 0x00;
    // Filename length (bytes 26-27): little-endian
    uint16_t fn_len = static_cast<uint16_t>(filename.size());
    lfh[26] = static_cast<uint8_t>(fn_len & 0xff);
    lfh[27] = static_cast<uint8_t>((fn_len >> 8) & 0xff);
    // Extra field length (bytes 28-29): 0
    lfh[28] = 0x00; lfh[29] = 0x00;
    // Filename
    if (!filename.empty()) {
        std::memcpy(lfh.data() + 30, filename.data(), filename.size());
    }
    return lfh;
}

// Helper to create a Central Directory entry (46-byte header minimum)
static std::vector<uint8_t> make_cd_entry(std::string_view filename,
                                          uint32_t local_offset,
                                          uint16_t disk_start = 0) {
    std::vector<uint8_t> cd(46 + filename.size());
    // Signature: 0x504b0102
    cd[0] = 0x50; cd[1] = 0x4b; cd[2] = 0x01; cd[3] = 0x02;
    // Version made by (bytes 4-5): 0x140a (2.0 Unix)
    cd[4] = 0x0a; cd[5] = 0x14;
    // Version needed (bytes 6-7): 0x0a14 (2.0)
    cd[6] = 0x14; cd[7] = 0x00;
    // General purpose bit flag (bytes 8-9): 0
    cd[8] = 0x00; cd[9] = 0x00;
    // Compression method (bytes 10-11): 0 (stored)
    cd[10] = 0x00; cd[11] = 0x00;
    // File modification time (bytes 12-13): 0
    cd[12] = 0x00; cd[13] = 0x00;
    // File modification date (bytes 14-15): 0
    cd[14] = 0x00; cd[15] = 0x00;
    // CRC-32 (bytes 16-19): 0
    cd[16] = 0x00; cd[17] = 0x00; cd[18] = 0x00; cd[19] = 0x00;
    // Compressed size (bytes 20-23): 0
    cd[20] = 0x00; cd[21] = 0x00; cd[22] = 0x00; cd[23] = 0x00;
    // Uncompressed size (bytes 24-27): 0
    cd[24] = 0x00; cd[25] = 0x00; cd[26] = 0x00; cd[27] = 0x00;
    // Filename length (bytes 28-29): little-endian
    uint16_t fn_len = static_cast<uint16_t>(filename.size());
    cd[28] = static_cast<uint8_t>(fn_len & 0xff);
    cd[29] = static_cast<uint8_t>((fn_len >> 8) & 0xff);
    // Extra field length (bytes 30-31): 0
    cd[30] = 0x00; cd[31] = 0x00;
    // File comment length (bytes 32-33): 0
    cd[32] = 0x00; cd[33] = 0x00;
    // Disk number start (bytes 34-35): little-endian
    cd[34] = static_cast<uint8_t>(disk_start & 0xff);
    cd[35] = static_cast<uint8_t>((disk_start >> 8) & 0xff);
    // Internal file attributes (bytes 36-37): 0
    cd[36] = 0x00; cd[37] = 0x00;
    // External file attributes (bytes 38-41): 0
    cd[38] = 0x00; cd[39] = 0x00; cd[40] = 0x00; cd[41] = 0x00;
    // Relative offset of local header (bytes 42-45): little-endian
    cd[42] = static_cast<uint8_t>(local_offset & 0xff);
    cd[43] = static_cast<uint8_t>((local_offset >> 8) & 0xff);
    cd[44] = static_cast<uint8_t>((local_offset >> 16) & 0xff);
    cd[45] = static_cast<uint8_t>((local_offset >> 24) & 0xff);
    // Filename
    if (!filename.empty()) {
        std::memcpy(cd.data() + 46, filename.data(), filename.size());
    }
    return cd;
}

// Helper to create an EOCD (22 bytes minimum)
static std::vector<uint8_t> make_eocd(uint16_t disk_num,
                                      uint16_t disk_with_cd,
                                      uint16_t entries_on_disk,
                                      uint16_t entries_total,
                                      uint32_t cd_size,
                                      uint32_t cd_offset) {
    std::vector<uint8_t> eocd(22);
    // Signature: 0x504b0506
    eocd[0] = 0x50; eocd[1] = 0x4b; eocd[2] = 0x05; eocd[3] = 0x06;
    // Disk number (bytes 4-5): little-endian
    eocd[4] = static_cast<uint8_t>(disk_num & 0xff);
    eocd[5] = static_cast<uint8_t>((disk_num >> 8) & 0xff);
    // Disk with CD (bytes 6-7): little-endian
    eocd[6] = static_cast<uint8_t>(disk_with_cd & 0xff);
    eocd[7] = static_cast<uint8_t>((disk_with_cd >> 8) & 0xff);
    // Entries on this disk (bytes 8-9): little-endian
    eocd[8] = static_cast<uint8_t>(entries_on_disk & 0xff);
    eocd[9] = static_cast<uint8_t>((entries_on_disk >> 8) & 0xff);
    // Total entries (bytes 10-11): little-endian
    eocd[10] = static_cast<uint8_t>(entries_total & 0xff);
    eocd[11] = static_cast<uint8_t>((entries_total >> 8) & 0xff);
    // CD size (bytes 12-15): little-endian
    eocd[12] = static_cast<uint8_t>(cd_size & 0xff);
    eocd[13] = static_cast<uint8_t>((cd_size >> 8) & 0xff);
    eocd[14] = static_cast<uint8_t>((cd_size >> 16) & 0xff);
    eocd[15] = static_cast<uint8_t>((cd_size >> 24) & 0xff);
    // CD offset (bytes 16-19): little-endian
    eocd[16] = static_cast<uint8_t>(cd_offset & 0xff);
    eocd[17] = static_cast<uint8_t>((cd_offset >> 8) & 0xff);
    eocd[18] = static_cast<uint8_t>((cd_offset >> 16) & 0xff);
    eocd[19] = static_cast<uint8_t>((cd_offset >> 24) & 0xff);
    // Comment length (bytes 20-21): 0
    eocd[20] = 0x00;
    eocd[21] = 0x00;
    return eocd;
}

// Helper to concatenate multiple buffers
static std::vector<uint8_t> concat(const std::vector<std::vector<uint8_t>>& parts) {
    std::vector<uint8_t> result;
    for (const auto& part : parts) {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

TEST(empty_volume_array_returns_error) {
    std::vector<std::span<const uint8_t>> volumes;
    auto result = ui::merge_spanned_zip(volumes);
    CHECK_EQ(result.error, ui::SpannedZipError::Malformed);
    CHECK_EQ(result.merged.size(), 0);
}

TEST(single_non_spanned_volume_is_not_spanned) {
    // A single volume without spanning marker should merge successfully
    auto lfh = make_local_header("file.txt");
    auto cd = make_cd_entry("file.txt", 0, 0);
    // CD starts at: lfh.size() (40 bytes)
    uint32_t cd_offset = static_cast<uint32_t>(lfh.size());
    auto eocd = make_eocd(0, 0, 1, 1, static_cast<uint32_t>(cd.size()), cd_offset);

    auto vol = concat({lfh, cd, eocd});
    std::vector<std::span<const uint8_t>> volumes = {std::span(vol)};

    auto result = ui::merge_spanned_zip(volumes);
    // A single file without spanning marker should be accepted as-is
    CHECK_EQ(result.error, ui::SpannedZipError::None);
    CHECK(result.merged.size() > 0);
}

TEST(spanned_marker_detection_and_strip) {
    // Volume 1: spanning marker (4 bytes) + LFH
    std::vector<uint8_t> vol1;
    vol1.push_back(0x50); vol1.push_back(0x4b);  // PK
    vol1.push_back(0x07); vol1.push_back(0x08);  // \x07\x08 (spanning marker)
    auto lfh = make_local_header("file.txt");
    vol1.insert(vol1.end(), lfh.begin(), lfh.end());

    // Volume 2 (final): CD + EOCD
    auto cd = make_cd_entry("file.txt", 0x04, 0);  // offset is 4 (after marker in disk 0)
    // CD starts at offset 0 within vol2 (it's the first thing in vol2)
    auto eocd = make_eocd(1, 1, 1, 1, static_cast<uint32_t>(cd.size()), 0);
    auto vol2 = concat({cd, eocd});

    std::vector<std::span<const uint8_t>> volumes = {
        std::span(vol1),
        std::span(vol2)
    };

    auto result = ui::merge_spanned_zip(volumes);

    // After strip + concat: marker should be gone, data should be continuous
    CHECK_EQ(result.error, ui::SpannedZipError::None);
    REQUIRE(result.merged.size() > 0);
    // First 4 bytes should be LFH signature, not spanning marker
    CHECK_EQ(result.merged[0], 0x50);
    CHECK_EQ(result.merged[1], 0x4b);
    CHECK_EQ(result.merged[2], 0x03);  // Local file header, not 0x07
    CHECK_EQ(result.merged[3], 0x04);  // Continue LFH
}

TEST(eocd_disk_fields_rewritten_to_zero) {
    // Build a simple 2-volume set
    std::vector<uint8_t> vol1;
    vol1.push_back(0x50); vol1.push_back(0x4b);
    vol1.push_back(0x07); vol1.push_back(0x08);  // spanning marker
    auto lfh = make_local_header("file.txt");
    vol1.insert(vol1.end(), lfh.begin(), lfh.end());

    auto cd = make_cd_entry("file.txt", 0x04, 0);
    // EOCD with disk_num=1 and disk_with_cd=1 (indicating this is disk 1 of 2)
    auto eocd = make_eocd(1, 1, 1, 1, static_cast<uint32_t>(cd.size()), 0x00);
    auto vol2 = concat({cd, eocd});

    std::vector<std::span<const uint8_t>> volumes = {
        std::span(vol1),
        std::span(vol2)
    };

    auto result = ui::merge_spanned_zip(volumes);
    CHECK_EQ(result.error, ui::SpannedZipError::None);

    // Find EOCD in merged buffer
    size_t eocd_pos = 0;
    REQUIRE(result.merged.size() >= 22);
    // Search for EOCD signature from the end
    for (int i = (int)result.merged.size() - 22; i >= 0; --i) {
        if (result.merged[i] == 0x50 && result.merged[i+1] == 0x4b &&
            result.merged[i+2] == 0x05 && result.merged[i+3] == 0x06) {
            eocd_pos = i;
            break;
        }
    }

    REQUIRE(eocd_pos > 0);
    REQUIRE(eocd_pos + 22 <= result.merged.size());

    // Check disk_num field (offset +4 in EOCD)
    uint16_t disk_num = result.merged[eocd_pos + 4] |
                        (result.merged[eocd_pos + 5] << 8);
    CHECK_EQ(disk_num, 0);

    // Check disk_with_cd field (offset +6)
    uint16_t disk_with_cd = result.merged[eocd_pos + 6] |
                            (result.merged[eocd_pos + 7] << 8);
    CHECK_EQ(disk_with_cd, 0);
}

TEST(cd_entries_disk_start_set_to_zero) {
    // Build a 2-volume set with file on disk 0 and CD on disk 1
    std::vector<uint8_t> vol1;
    vol1.push_back(0x50); vol1.push_back(0x4b);
    vol1.push_back(0x07); vol1.push_back(0x08);  // spanning marker
    auto lfh = make_local_header("file.txt");
    vol1.insert(vol1.end(), lfh.begin(), lfh.end());

    // CD entry with disk_start=0 (it's already on disk 0)
    auto cd = make_cd_entry("file.txt", 0x04, 0);
    auto eocd = make_eocd(1, 1, 1, 1, static_cast<uint32_t>(cd.size()), 0);
    auto vol2 = concat({cd, eocd});

    std::vector<std::span<const uint8_t>> volumes = {
        std::span(vol1),
        std::span(vol2)
    };

    auto result = ui::merge_spanned_zip(volumes);
    CHECK_EQ(result.error, ui::SpannedZipError::None);

    // Find CD entry (should start with 0x504b0102)
    size_t cd_pos = 0;
    REQUIRE(result.merged.size() >= 46);
    for (size_t i = 0; i < result.merged.size() - 46; ++i) {
        if (result.merged[i] == 0x50 && result.merged[i+1] == 0x4b &&
            result.merged[i+2] == 0x01 && result.merged[i+3] == 0x02) {
            cd_pos = i;
            break;
        }
    }

    REQUIRE(cd_pos > 0);
    REQUIRE(cd_pos + 46 <= result.merged.size());

    // Check disk_start field (offset +34 in CD entry)
    uint16_t disk_start = result.merged[cd_pos + 34] |
                          (result.merged[cd_pos + 35] << 8);
    CHECK_EQ(disk_start, 0);
}

TEST(malformed_no_eocd_signature) {
    // Volume without EOCD signature
    std::vector<uint8_t> garbage(100, 0xff);
    std::vector<std::span<const uint8_t>> volumes = {std::span(garbage)};

    auto result = ui::merge_spanned_zip(volumes);
    CHECK_EQ(result.error, ui::SpannedZipError::Malformed);
    CHECK_EQ(result.merged.size(), 0);
}

TEST(zip64_eocd_locator_rejected) {
    // Build a buffer containing Zip64 EOCD locator
    std::vector<uint8_t> vol1;
    vol1.push_back(0x50); vol1.push_back(0x4b);
    vol1.push_back(0x07); vol1.push_back(0x08);  // spanning marker
    auto lfh = make_local_header("file.txt");
    vol1.insert(vol1.end(), lfh.begin(), lfh.end());

    std::vector<uint8_t> vol2;
    // Add Zip64 EOCD Locator signature (0x504b0607)
    vol2.push_back(0x50); vol2.push_back(0x4b);
    vol2.push_back(0x06); vol2.push_back(0x07);
    vol2.insert(vol2.end(), 16, 0x00);  // 16 more bytes of locator
    auto cd = make_cd_entry("file.txt", 0x04, 0);
    auto eocd = make_eocd(1, 1, 1, 1, static_cast<uint32_t>(cd.size()), 0);
    vol2.insert(vol2.end(), cd.begin(), cd.end());
    vol2.insert(vol2.end(), eocd.begin(), eocd.end());

    std::vector<std::span<const uint8_t>> volumes = {
        std::span(vol1),
        std::span(vol2)
    };

    auto result = ui::merge_spanned_zip(volumes);
    CHECK_EQ(result.error, ui::SpannedZipError::Zip64Unsupported);
    CHECK_EQ(result.merged.size(), 0);
}

TEST(buffer_too_small_for_eocd) {
    // Buffer smaller than EOCD minimum (22 bytes)
    std::vector<uint8_t> small(10, 0x50);
    std::vector<std::span<const uint8_t>> volumes = {std::span(small)};

    auto result = ui::merge_spanned_zip(volumes);
    CHECK_EQ(result.error, ui::SpannedZipError::Malformed);
    CHECK_EQ(result.merged.size(), 0);
}

// --- end-to-end against a REAL split archive --------------------------------
//
// Everything above hand-builds ZIP bytes and asserts that fields were
// rewritten, which proves the code does what the code does. It does NOT prove
// the merged buffer is a ZIP that miniz can open — which is the entire point of
// the merger. This test closes that gap: it makes a genuine split archive with
// `zip -s`, merges the volumes, and then actually reads the result back through
// miniz, comparing extracted bytes to the originals.

#include "miniz.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

namespace e2e_fs = std::filesystem;

[[nodiscard]] bool have_zip_tool()
{
    return std::system("command -v zip >/dev/null 2>&1") == 0;
}

[[nodiscard]] std::vector<uint8_t> slurp(const e2e_fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(spanned_zip_merged_output_is_readable_by_miniz)
{
    if (!have_zip_tool()) {
        std::println("  SKIP  spanned_zip end-to-end: `zip` not installed");
        return;
    }

    const e2e_fs::path dir = e2e_fs::temp_directory_path() / "osv_spanned_e2e";
    std::error_code    ec;
    e2e_fs::remove_all(dir, ec);
    e2e_fs::create_directories(dir / "payload", ec);

    // The payload MUST be incompressible, or deflate shrinks the archive below
    // the split threshold and `zip -s` silently produces a single volume —
    // which would make this test pass while proving nothing. A simple pattern
    // like (i*31)^0xA5 compresses to almost nothing; xorshift32 output does
    // not, and is deterministic so the fixture is reproducible.
    auto fill_incompressible = [](std::vector<uint8_t>& v, uint32_t seed) {
        uint32_t x = seed;
        for (uint8_t& byte : v) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            byte = static_cast<uint8_t>(x);
        }
    };
    std::vector<uint8_t> a(90000);
    std::vector<uint8_t> b(70000);
    fill_incompressible(a, 0x1234567u);
    fill_incompressible(b, 0x89abcdefu);
    { std::ofstream f(dir / "payload" / "a.bin", std::ios::binary);
      f.write(reinterpret_cast<const char*>(a.data()), static_cast<std::streamsize>(a.size())); }
    { std::ofstream f(dir / "payload" / "b.bin", std::ios::binary);
      f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size())); }

    const std::string cmd =
        "cd " + dir.string() + " && zip -q -s 64k -r out.zip payload >/dev/null 2>&1";
    REQUIRE(std::system(cmd.c_str()) == 0);

    // Collect volumes in set order: .z01, .z02, …, then out.zip last.
    std::vector<e2e_fs::path> vol_paths;
    for (int i = 1; i < 100; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "out.z%02d", i);
        const e2e_fs::path p = dir / buf;
        if (!e2e_fs::exists(p)) break;
        vol_paths.push_back(p);
    }
    vol_paths.push_back(dir / "out.zip");
    // If this is 1, `zip -s` did not actually split and the test proves nothing.
    REQUIRE(vol_paths.size() >= 2);

    std::vector<std::vector<uint8_t>> bufs;
    bufs.reserve(vol_paths.size());
    for (const auto& p : vol_paths) bufs.push_back(slurp(p));

    std::vector<std::span<const uint8_t>> vols;
    vols.reserve(bufs.size());
    for (const auto& v : bufs) vols.emplace_back(v);

    const auto result = ui::merge_spanned_zip(vols);
    CHECK(result.error == ui::SpannedZipError::None);
    REQUIRE(!result.merged.empty());

    // The real assertion: miniz opens the merged buffer and the bytes survive.
    mz_zip_archive zip{};
    REQUIRE(mz_zip_reader_init_mem(&zip, result.merged.data(), result.merged.size(), 0));

    bool saw_a = false;
    bool saw_b = false;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        const std::string_view name(st.m_filename);
        const std::vector<uint8_t>* want = nullptr;
        if (name.ends_with("a.bin")) { want = &a; saw_a = true; }
        else if (name.ends_with("b.bin")) { want = &b; saw_b = true; }
        if (want == nullptr) continue;

        size_t      out_size = 0;
        void* const raw = mz_zip_reader_extract_to_heap(&zip, i, &out_size, 0);
        REQUIRE(raw != nullptr);
        CHECK_EQ(out_size, want->size());
        CHECK(std::memcmp(raw, want->data(), std::min(out_size, want->size())) == 0);
        mz_free(raw);
    }
    mz_zip_reader_end(&zip);

    CHECK(saw_a);
    CHECK(saw_b);
    e2e_fs::remove_all(dir, ec);
}
