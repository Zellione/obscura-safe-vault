// Phase 53: multi-volume archive detection.
//
// Naming below is REAL output observed from the tools, not convention:
//   7z -v      -> archive.7z.001 … .103   (3-digit, starts at 001)
//   split -d   -> archive.tar.00  … .54   (2-digit, starts at 00)
//   zip -s     -> archive.z01 … .z51 + archive.zip
//   RAR        -> .part1.rar / .part01.rar / .part0001.rar (libarchive fixtures)

#include "test_framework.h"
#include "ui/volume_set.h"

#include <string>
#include <vector>

using ui::detect_volume_set;
using ui::VolumeStyle;

namespace {

std::vector<std::string> names(std::initializer_list<const char*> v)
{
    return {v.begin(), v.end()};
}

} // namespace

// --- NumericSuffix ----------------------------------------------------------

TEST(volume_set_detects_7z_numeric_suffix)
{
    const auto sibs = names({"archive.7z.001", "archive.7z.002", "archive.7z.003"});
    const auto s    = detect_volume_set("archive.7z.001", sibs);

    CHECK(s.style == VolumeStyle::NumericSuffix);
    CHECK_EQ(s.stem, std::string("archive.7z"));
    CHECK_EQ(s.volumes.size(), static_cast<size_t>(3));
    CHECK(s.missing.empty());
}

TEST(volume_set_orders_numeric_volumes_ascending)
{
    // Listing order is whatever the directory yields; the set must be ordered.
    const auto sibs = names({"a.7z.003", "a.7z.001", "a.7z.002"});
    const auto s    = detect_volume_set("a.7z.002", sibs);

    REQUIRE(s.volumes.size() == 3);
    CHECK_EQ(s.volumes[0], std::string("a.7z.001"));
    CHECK_EQ(s.volumes[2], std::string("a.7z.003"));
}

TEST(volume_set_accepts_a_set_starting_at_zero)
{
    // split -d starts at .00; 7z starts at .001. A set is contiguous from its
    // OWN minimum, not required to start at 1.
    const auto sibs = names({"a.tar.00", "a.tar.01", "a.tar.02"});
    const auto s    = detect_volume_set("a.tar.00", sibs);

    CHECK(s.style == VolumeStyle::NumericSuffix);
    CHECK_EQ(s.volumes.size(), static_cast<size_t>(3));
    CHECK(s.missing.empty());
}

TEST(volume_set_can_be_found_from_a_middle_volume)
{
    const auto sibs = names({"a.7z.001", "a.7z.002", "a.7z.003"});
    const auto s    = detect_volume_set("a.7z.002", sibs);

    CHECK(s.style == VolumeStyle::NumericSuffix);
    CHECK_EQ(s.volumes.size(), static_cast<size_t>(3));
}

TEST(volume_set_numeric_stem_keeps_the_inner_archive_extension)
{
    // movie.tar.001 -> the thing being reassembled is "movie.tar".
    const auto sibs = names({"movie.tar.001", "movie.tar.002"});
    const auto s    = detect_volume_set("movie.tar.001", sibs);

    CHECK_EQ(s.stem, std::string("movie.tar"));
}

// --- SpannedZip: the .zip is the LAST volume --------------------------------

TEST(volume_set_detects_spanned_zip_with_zip_last)
{
    const auto sibs = names({"archive.z01", "archive.z02", "archive.zip"});
    const auto s    = detect_volume_set("archive.zip", sibs);

    CHECK(s.style == VolumeStyle::SpannedZip);
    CHECK_EQ(s.stem, std::string("archive"));
    REQUIRE(s.volumes.size() == 3);
    // The trap: .zip sorts last alphabetically AND is genuinely last, but .z01
    // must come first — a plain sort would put ".zip" after ".z02" by luck and
    // ".z10" before ".z02" by disaster.
    CHECK_EQ(s.volumes[0], std::string("archive.z01"));
    CHECK_EQ(s.volumes[2], std::string("archive.zip"));
}

TEST(volume_set_spanned_zip_orders_double_digit_volumes_numerically)
{
    const auto sibs = names({"a.z01", "a.z02", "a.z10", "a.zip"});
    const auto s    = detect_volume_set("a.z01", sibs);

    REQUIRE(s.volumes.size() == 4);
    CHECK_EQ(s.volumes[1], std::string("a.z02"));
    CHECK_EQ(s.volumes[2], std::string("a.z10"));   // not sorted as a string
    CHECK_EQ(s.volumes[3], std::string("a.zip"));
}

TEST(volume_set_lone_zip_is_not_a_spanned_set)
{
    // THE most important negative: an ordinary single zip must stay ordinary.
    const auto sibs = names({"photos.zip", "notes.txt"});
    const auto s    = detect_volume_set("photos.zip", sibs);

    CHECK(s.style == VolumeStyle::None);
}

TEST(volume_set_spanned_zip_reports_a_missing_middle_volume)
{
    const auto sibs = names({"a.z01", "a.z03", "a.zip"});
    const auto s    = detect_volume_set("a.zip", sibs);

    CHECK(s.style == VolumeStyle::SpannedZip);
    REQUIRE(s.missing.size() == 1);
    CHECK_EQ(s.missing[0], 2);
}

// --- RarPart: three padding widths, all real --------------------------------

TEST(volume_set_detects_unpadded_rar_parts)
{
    const auto sibs = names({"scans.part1.rar", "scans.part2.rar", "scans.part3.rar"});
    const auto s    = detect_volume_set("scans.part1.rar", sibs);

    CHECK(s.style == VolumeStyle::RarPart);
    CHECK_EQ(s.stem, std::string("scans"));
    CHECK_EQ(s.volumes.size(), static_cast<size_t>(3));
}

TEST(volume_set_detects_zero_padded_rar_parts_across_the_ten_boundary)
{
    const auto sibs = names({"u.part09.rar", "u.part10.rar", "u.part01.rar"});
    const auto s    = detect_volume_set("u.part01.rar", sibs);

    CHECK(s.style == VolumeStyle::RarPart);
    REQUIRE(s.volumes.size() == 3);
    CHECK_EQ(s.volumes[0], std::string("u.part01.rar"));
    CHECK_EQ(s.volumes[2], std::string("u.part10.rar"));
}

TEST(volume_set_detects_four_digit_rar_parts)
{
    const auto sibs = names({"m.part0001.rar", "m.part0002.rar"});
    const auto s    = detect_volume_set("m.part0001.rar", sibs);

    CHECK(s.style == VolumeStyle::RarPart);
    CHECK_EQ(s.volumes.size(), static_cast<size_t>(2));
}

// --- RarOld: .rar is volume ONE ---------------------------------------------

TEST(volume_set_detects_old_style_rar_with_rar_first)
{
    const auto sibs = names({"old.rar", "old.r00", "old.r01"});
    const auto s    = detect_volume_set("old.rar", sibs);

    CHECK(s.style == VolumeStyle::RarOld);
    REQUIRE(s.volumes.size() == 3);
    // The trap: ".rar" sorts AFTER ".r00" alphabetically but is volume one.
    CHECK_EQ(s.volumes[0], std::string("old.rar"));
    CHECK_EQ(s.volumes[1], std::string("old.r00"));
    CHECK_EQ(s.volumes[2], std::string("old.r01"));
}

TEST(volume_set_lone_rar_is_not_an_old_style_set)
{
    const auto sibs = names({"scans.rar", "readme.txt"});
    const auto s    = detect_volume_set("scans.rar", sibs);

    CHECK(s.style == VolumeStyle::None);
}

// --- false positives --------------------------------------------------------

TEST(volume_set_single_numeric_volume_is_not_a_set)
{
    // A minimum of two volumes: "holiday.001" alone is just a file.
    const auto sibs = names({"holiday.001"});
    const auto s    = detect_volume_set("holiday.001", sibs);

    CHECK(s.style == VolumeStyle::None);
}

TEST(volume_set_does_not_merge_two_unrelated_sets_in_one_directory)
{
    const auto sibs = names({"photos.7z.001", "photos.7z.002",
                             "backup.7z.001", "backup.7z.002"});
    const auto s    = detect_volume_set("photos.7z.001", sibs);

    REQUIRE(s.volumes.size() == 2);
    CHECK_EQ(s.volumes[0], std::string("photos.7z.001"));
    CHECK_EQ(s.volumes[1], std::string("photos.7z.002"));
}

TEST(volume_set_does_not_absorb_a_different_stem_with_a_matching_extension)
{
    // data.zip must not be swept into archive's spanned set.
    const auto sibs = names({"archive.z01", "archive.z02", "archive.zip", "data.zip"});
    const auto s    = detect_volume_set("archive.zip", sibs);

    CHECK_EQ(s.volumes.size(), static_cast<size_t>(3));
}

TEST(volume_set_ignores_siblings_that_are_not_volumes_at_all)
{
    const auto sibs = names({"a.7z.001", "a.7z.002", "readme.txt", "cover.jpg"});
    const auto s    = detect_volume_set("a.7z.001", sibs);

    CHECK_EQ(s.volumes.size(), static_cast<size_t>(2));
}

TEST(volume_set_reports_a_gap_in_a_numeric_set)
{
    const auto sibs = names({"a.7z.001", "a.7z.002", "a.7z.004"});
    const auto s    = detect_volume_set("a.7z.001", sibs);

    CHECK(s.style == VolumeStyle::NumericSuffix);
    REQUIRE(s.missing.size() == 1);
    CHECK_EQ(s.missing[0], 3);
}

// --- assembly routing -------------------------------------------------------
//
// Each style needs a genuinely different mechanism. Getting this mapping wrong
// produces a corrupt import rather than an error: gluing RAR volumes together
// yields a buffer libarchive will read a little of and then give up on.

TEST(volume_assembly_maps_each_style_to_its_mechanism)
{
    CHECK(ui::assembly_for(ui::VolumeStyle::NumericSuffix) == ui::VolumeAssembly::Concatenate);
    CHECK(ui::assembly_for(ui::VolumeStyle::RarPart) == ui::VolumeAssembly::FileOriented);
    CHECK(ui::assembly_for(ui::VolumeStyle::RarOld) == ui::VolumeAssembly::FileOriented);
    CHECK(ui::assembly_for(ui::VolumeStyle::SpannedZip) == ui::VolumeAssembly::SpannedZipMerge);
    CHECK(ui::assembly_for(ui::VolumeStyle::None) == ui::VolumeAssembly::None);
}

TEST(concatenate_volumes_joins_in_order)
{
    const std::vector<uint8_t> a{1, 2, 3};
    const std::vector<uint8_t> b{4, 5};
    const std::vector<uint8_t> c{6};
    const std::vector<std::span<const uint8_t>> vols{a, b, c};

    const auto joined = ui::concatenate_volumes(vols);
    REQUIRE(joined.size() == 6);
    for (size_t i = 0; i < joined.size(); ++i) {
        CHECK_EQ(joined[i], static_cast<uint8_t>(i + 1));
    }
}

TEST(concatenate_volumes_handles_empty_and_single_inputs)
{
    CHECK(ui::concatenate_volumes({}).empty());

    const std::vector<uint8_t>                  only{9, 9, 9};
    const std::vector<std::span<const uint8_t>> one{only};
    CHECK_EQ(ui::concatenate_volumes(one).size(), static_cast<size_t>(3));
}

TEST(concatenate_volumes_tolerates_a_zero_length_volume)
{
    // A truncated download can leave a 0-byte part; joining must not lose the
    // volumes on either side of it.
    const std::vector<uint8_t>                  a{1, 2};
    const std::vector<uint8_t>                  empty;
    const std::vector<uint8_t>                  c{3};
    const std::vector<std::span<const uint8_t>> vols{a, empty, c};

    const auto joined = ui::concatenate_volumes(vols);
    REQUIRE(joined.size() == 3);
    CHECK_EQ(joined[2], static_cast<uint8_t>(3));
}

// --- end-to-end over a REAL split set ---------------------------------------
//
// Detection and concatenation are unit-tested above with synthetic names and
// bytes. This runs the whole route on an archive actually split by `split -d`:
// detect the set from a directory listing, join it, and confirm miniz opens the
// result and returns the original bytes.

#include "miniz.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

namespace vfs = std::filesystem;

[[nodiscard]] std::vector<uint8_t> slurp_file(const vfs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(volume_set_real_numeric_split_concatenates_back_into_a_readable_zip)
{
    if (std::system("command -v zip >/dev/null 2>&1") != 0 ||
        std::system("command -v split >/dev/null 2>&1") != 0) {
        std::println("  SKIP  numeric-split end-to-end: zip/split not installed");
        return;
    }

    const vfs::path dir = vfs::temp_directory_path() / "osv_volset_e2e";
    std::error_code ec;
    vfs::remove_all(dir, ec);
    vfs::create_directories(dir / "payload", ec);

    // Incompressible, or the archive is too small for `split` to produce parts.
    std::vector<uint8_t> blob(120000);
    uint32_t             x = 0xC0FFEEu;
    for (uint8_t& b : blob) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b = static_cast<uint8_t>(x);
    }
    {
        std::ofstream f(dir / "payload" / "big.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(blob.data()),
                static_cast<std::streamsize>(blob.size()));
    }

    const std::string mk = "cd " + dir.string() +
                           " && zip -q -r whole.zip payload && split -d -b 40000 whole.zip whole.zip.";
    REQUIRE(std::system(mk.c_str()) == 0);

    // Feed the directory listing to detection, exactly as the picker will.
    std::vector<std::string> siblings;
    for (const auto& e : vfs::directory_iterator(dir)) {
        siblings.push_back(e.path().filename().string());
    }
    const auto set = ui::detect_volume_set("whole.zip.00", siblings);
    CHECK(set.style == ui::VolumeStyle::NumericSuffix);
    CHECK(set.missing.empty());
    REQUIRE(set.volumes.size() >= 2);   // if 1, `split` did not split and this proves nothing
    CHECK(ui::assembly_for(set.style) == ui::VolumeAssembly::Concatenate);

    std::vector<std::vector<uint8_t>> bufs;
    bufs.reserve(set.volumes.size());
    for (const std::string& name : set.volumes) {
        bufs.push_back(slurp_file(dir / name));
    }
    std::vector<std::span<const uint8_t>> spans;
    spans.reserve(bufs.size());
    for (const auto& b : bufs) spans.emplace_back(b);

    const auto joined = ui::concatenate_volumes(spans);
    CHECK_EQ(joined.size(), slurp_file(dir / "whole.zip").size());

    // The real assertion: it is a working zip again.
    mz_zip_archive zip{};
    REQUIRE(mz_zip_reader_init_mem(&zip, joined.data(), joined.size(), 0));
    bool matched = false;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        if (!std::string_view(st.m_filename).ends_with("big.bin")) continue;
        size_t      n   = 0;
        void* const raw = mz_zip_reader_extract_to_heap(&zip, i, &n, 0);
        REQUIRE(raw != nullptr);
        CHECK_EQ(n, blob.size());
        CHECK(std::memcmp(raw, blob.data(), std::min(n, blob.size())) == 0);
        mz_free(raw);
        matched = true;
    }
    mz_zip_reader_end(&zip);
    CHECK(matched);

    vfs::remove_all(dir, ec);
}
