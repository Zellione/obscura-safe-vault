// Phase 53: describing a multi-volume set for the confirm dialog, and turning
// it into bytes the importer can use.

#include "test_framework.h"
#include "ui/volume_import.h"
#include "ui/zip_test_helpers.h"

#include <fstream>
#include <string>
#include <vector>

namespace {

ui::VolumeSet make_set(ui::VolumeStyle style, std::vector<std::string> volumes,
                       std::vector<int> missing = {})
{
    ui::VolumeSet s;
    s.style   = style;
    s.stem    = "archive";
    s.volumes = std::move(volumes);
    s.missing = std::move(missing);
    return s;
}

} // namespace

// --- summary ----------------------------------------------------------------

TEST(volume_summary_lists_every_volume_in_order)
{
    const auto s = summarize_volume_set(
        make_set(ui::VolumeStyle::NumericSuffix, {"a.7z.001", "a.7z.002", "a.7z.003"}));

    REQUIRE(s.volume_lines.size() == 3);
    CHECK(s.volume_lines[0].find("a.7z.001") != std::string::npos);
    CHECK(s.volume_lines[2].find("a.7z.003") != std::string::npos);
    CHECK(s.can_import);
    CHECK(s.warning.empty());
}

TEST(volume_summary_refuses_a_set_with_a_gap)
{
    // An incomplete set produces a CORRUPT gallery, not a partial one, so it
    // must be blocked rather than attempted.
    const auto s = summarize_volume_set(
        make_set(ui::VolumeStyle::NumericSuffix, {"a.7z.001", "a.7z.003"}, {2}));

    CHECK_FALSE(s.can_import);
    CHECK(!s.warning.empty());
    CHECK(s.warning.find('2') != std::string::npos);
}

TEST(volume_summary_names_several_missing_volumes)
{
    const auto s = summarize_volume_set(
        make_set(ui::VolumeStyle::NumericSuffix, {"a.7z.001", "a.7z.004"}, {2, 3}));

    CHECK_FALSE(s.can_import);
    CHECK(s.warning.find('2') != std::string::npos);
    CHECK(s.warning.find('3') != std::string::npos);
}

TEST(volume_summary_heading_states_the_volume_count)
{
    const auto s =
        summarize_volume_set(make_set(ui::VolumeStyle::SpannedZip, {"a.z01", "a.z02", "a.zip"}));
    CHECK(s.heading.find('3') != std::string::npos);
}

TEST(volume_summary_of_a_non_set_cannot_import)
{
    ui::VolumeSet none;
    const auto    s = summarize_volume_set(none);
    CHECK_FALSE(s.can_import);
}

// --- assembly ---------------------------------------------------------------

TEST(volume_assemble_concatenates_a_numeric_set)
{
    const auto dir = ziptest::fresh_dir("volimport_concat");
    { std::ofstream f(dir / "a.tar.00", std::ios::binary); f << "AAAA"; }
    { std::ofstream f(dir / "a.tar.01", std::ios::binary); f << "BB"; }

    const auto got = assemble_volume_set(
        make_set(ui::VolumeStyle::NumericSuffix, {"a.tar.00", "a.tar.01"}), dir);

    CHECK(got.ok());
    CHECK(got.assembly == ui::VolumeAssembly::Concatenate);
    REQUIRE(got.bytes.size() == 6);
    CHECK_EQ(got.bytes[0], static_cast<uint8_t>('A'));
    CHECK_EQ(got.bytes[5], static_cast<uint8_t>('B'));
    CHECK(got.paths.empty());

    ziptest::cleanup_dir(dir);
}

TEST(volume_assemble_returns_paths_for_rar)
{
    // A RAR volume carries its own header, so libarchive must see the paths —
    // handing it a joined buffer silently truncates the archive.
    const auto dir = ziptest::fresh_dir("volimport_rar");
    { std::ofstream f(dir / "a.part1.rar", std::ios::binary); f << "x"; }
    { std::ofstream f(dir / "a.part2.rar", std::ios::binary); f << "y"; }

    const auto got = assemble_volume_set(
        make_set(ui::VolumeStyle::RarPart, {"a.part1.rar", "a.part2.rar"}), dir);

    CHECK(got.ok());
    CHECK(got.assembly == ui::VolumeAssembly::FileOriented);
    REQUIRE(got.paths.size() == 2);
    CHECK(got.bytes.empty());
    CHECK(got.paths[0].filename().string() == "a.part1.rar");

    ziptest::cleanup_dir(dir);
}

TEST(volume_assemble_refuses_a_set_with_a_gap)
{
    const auto dir = ziptest::fresh_dir("volimport_gap");
    { std::ofstream f(dir / "a.7z.001", std::ios::binary); f << "x"; }

    const auto got =
        assemble_volume_set(make_set(ui::VolumeStyle::NumericSuffix, {"a.7z.001"}, {2}), dir);

    CHECK_FALSE(got.ok());
    CHECK(!got.error.empty());

    ziptest::cleanup_dir(dir);
}

TEST(volume_assemble_reports_a_missing_file_on_disk)
{
    // Detection saw the name in a listing; the file can still vanish before the
    // user confirms.
    const auto dir = ziptest::fresh_dir("volimport_missing");
    { std::ofstream f(dir / "a.7z.001", std::ios::binary); f << "x"; }

    const auto got = assemble_volume_set(
        make_set(ui::VolumeStyle::NumericSuffix, {"a.7z.001", "a.7z.002"}), dir);

    CHECK_FALSE(got.ok());
    CHECK(!got.error.empty());

    ziptest::cleanup_dir(dir);
}
