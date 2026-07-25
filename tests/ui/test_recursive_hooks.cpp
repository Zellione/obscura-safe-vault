// Phase 53: recursion driven by the REAL archive backends, on REAL nested
// archives — a zip containing a zip containing images.
//
// test_walk_archive covers the recursion logic with fakes. This covers the
// adapter: that miniz actually lists and extracts a nested archive's bytes,
// that a nested buffer is recognised by its magic, and that the resulting media
// lands in the right sub-gallery of a real vault.

#include "test_framework.h"
#include "ui/recursive_exec.h"
#include "ui/recursive_hooks.h"
#include "ui/recursive_import.h"
#include "ui/media_sink.h"
#include "ui/zip_test_helpers.h"

#include <fstream>

#include <string>
#include <vector>

using ziptest::fake_jpeg;
using ziptest::make_archive;

namespace {

std::vector<uint8_t> read_bytes(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(recursive_hooks_import_a_zip_nested_inside_a_zip)
{
    const auto dir = ziptest::fresh_dir("rechooks_nested");

    // inner.zip holds two images...
    const auto inner_path = make_archive({{"one.jpg", fake_jpeg(1)},
                                          {"two.jpg", fake_jpeg(2)}},
                                         dir / "inner.zip");
    const auto inner_bytes = read_bytes(inner_path);
    REQUIRE(!inner_bytes.empty());

    // ...and outer.zip holds a loose image plus inner.zip itself.
    const auto outer_path = make_archive({{"top.jpg", fake_jpeg(3)},
                                          {"bonus.zip", inner_bytes}},
                                         dir / "outer.zip");
    const auto outer_bytes = read_bytes(outer_path);
    REQUIRE(!outer_bytes.empty());

    vault::Vault v;
    ziptest::make_vault(v, dir / "v.osv");
    REQUIRE(v.create_gallery("Dest") == vault::VaultResult::Ok);

    ui::DirectVaultSink sink(v, "Dest", "");
    const auto hooks = ui::make_recursive_hooks(sink, "Dest");
    const auto tally = ui::walk_archive(outer_bytes, ui::ArchiveKind::Zip, "Dest", hooks);

    CHECK_EQ(tally.nested_archives, 1);
    CHECK_EQ(tally.media_placed, 3);

    // The loose image at the top level.
    const auto top = v.list("Dest");
    bool saw_top = false;
    bool saw_sub = false;
    for (const auto* n : top) {
        if (n->name == "top.jpg" && n->is_image()) saw_top = true;
        if (n->name == "bonus" && n->is_gallery()) saw_sub = true;
    }
    CHECK(saw_top);
    CHECK(saw_sub);

    // The nested archive became a sub-gallery holding its own images.
    const auto sub = v.list("Dest/bonus");
    CHECK_EQ(sub.size(), static_cast<size_t>(2));

    ziptest::cleanup_dir(dir);
}

TEST(recursive_hooks_do_not_descend_into_an_entry_whose_magic_disagrees)
{
    // A text file named .zip must be skipped, not handed to miniz.
    const auto dir = ziptest::fresh_dir("rechooks_liar");
    const std::string lie = "this is not a zip at all";
    const auto outer_path = make_archive(
        {{"ok.jpg", fake_jpeg(5)},
         {"liar.zip", std::vector<uint8_t>(lie.begin(), lie.end())}},
        dir / "outer.zip");
    const auto outer_bytes = read_bytes(outer_path);

    vault::Vault v;
    ziptest::make_vault(v, dir / "v.osv");
    REQUIRE(v.create_gallery("D") == vault::VaultResult::Ok);

    ui::DirectVaultSink sink(v, "D", "");
    const auto hooks = ui::make_recursive_hooks(sink, "D");
    const auto tally = ui::walk_archive(outer_bytes, ui::ArchiveKind::Zip, "D", hooks);

    CHECK_EQ(tally.nested_archives, 0);
    CHECK_EQ(tally.not_an_archive, 1);
    CHECK_EQ(tally.media_placed, 1);

    ziptest::cleanup_dir(dir);
}

TEST(recursive_hooks_import_three_levels_of_nesting)
{
    const auto dir = ziptest::fresh_dir("rechooks_deep");

    const auto l2 = read_bytes(make_archive({{"deep.jpg", fake_jpeg(7)}}, dir / "l2.zip"));
    const auto l1 = read_bytes(make_archive({{"two.zip", l2}}, dir / "l1.zip"));
    const auto l0 = read_bytes(make_archive({{"one.zip", l1}}, dir / "l0.zip"));

    vault::Vault v;
    ziptest::make_vault(v, dir / "v.osv");
    REQUIRE(v.create_gallery("D") == vault::VaultResult::Ok);

    ui::DirectVaultSink sink(v, "D", "");
    const auto hooks = ui::make_recursive_hooks(sink, "D");
    const auto tally = ui::walk_archive(l0, ui::ArchiveKind::Zip, "D", hooks);

    CHECK_EQ(tally.nested_archives, 2);
    CHECK_EQ(tally.media_placed, 1);

    const auto deep = v.list("D/one/two");
    REQUIRE(deep.size() == 1);
    CHECK_EQ(deep[0]->name, std::string("deep.jpg"));

    ziptest::cleanup_dir(dir);
}

// --- the executor: what the queue actually calls ----------------------------

TEST(import_archive_recursive_creates_nested_sub_galleries_in_a_vault)
{
    const auto dir = ziptest::fresh_dir("recexec");

    const auto inner = read_bytes(make_archive({{"one.jpg", fake_jpeg(11)}}, dir / "inner.zip"));
    const auto outer_path =
        make_archive({{"top.jpg", fake_jpeg(12)}, {"bonus.zip", inner}}, dir / "outer.zip");

    vault::Vault v;
    ziptest::make_vault(v, dir / "v.osv");

    // Same shape as import_zip's own vault overload: the sink base is
    // base_gallery + new_gallery_name.
    ui::DirectVaultSink sink(v, "", "Album");
    const auto out = ui::import_archive_recursive(sink, outer_path, "Album", "Album");

    CHECK(out.ok);
    CHECK_EQ(out.imported, 2);

    const auto top = v.list("Album");
    bool saw_sub = false;
    for (const auto* n : top) {
        if (n->name == "bonus" && n->is_gallery()) saw_sub = true;
    }
    CHECK(saw_sub);
    CHECK_EQ(v.list("Album/bonus").size(), static_cast<size_t>(1));

    ziptest::cleanup_dir(dir);
}

TEST(import_archive_recursive_rejects_a_file_that_is_not_an_archive)
{
    const auto dir = ziptest::fresh_dir("recexec_bad");
    const auto bad = dir / "notreally.zip";
    { std::ofstream f(bad, std::ios::binary); f << "plain text, not a zip"; }

    vault::Vault v;
    ziptest::make_vault(v, dir / "v.osv");
    ui::DirectVaultSink sink(v, "", "X");

    const auto out = ui::import_archive_recursive(sink, bad, "X", "X");
    CHECK_FALSE(out.ok);
    CHECK(!out.error.empty());

    ziptest::cleanup_dir(dir);
}

TEST(recursive_hooks_tag_a_nested_gallery_from_its_own_meta_json)
{
    // A nested archive carries the metadata of the archive it came from, not
    // its parent's — otherwise every sub-gallery would inherit the outer tags
    // and lose its own.
    const auto dir = ziptest::fresh_dir("rechooks_meta");

    const std::string inner_meta = R"({"tags":[{"type":"artist","name":"inner_artist"}]})";
    const auto        inner      = read_bytes(make_archive(
        {{"one.jpg", fake_jpeg(61)},
                {"meta.json", {inner_meta.begin(), inner_meta.end()}}},
        dir / "inner.zip"));

    const std::string outer_meta = R"({"tags":[{"type":"artist","name":"outer_artist"}]})";
    const auto        outer      = read_bytes(make_archive(
        {{"top.jpg", fake_jpeg(62)},
                {"bonus.zip", inner},
                {"meta.json", {outer_meta.begin(), outer_meta.end()}}},
        dir / "outer.zip"));

    vault::Vault v;
    ziptest::make_vault(v, dir / "v.osv");
    REQUIRE(v.create_gallery("D") == vault::VaultResult::Ok);

    ui::DirectVaultSink sink(v, "D", "");
    const auto hooks = ui::make_recursive_hooks(sink, "D");
    const auto tally = ui::walk_archive(outer, ui::ArchiveKind::Zip, "D", hooks);
    CHECK_EQ(tally.nested_archives, 1);

    const vault::IndexNode* sub = nullptr;
    for (const auto* n : v.list("D")) {
        if (n->name == "bonus" && n->is_gallery()) sub = n;
    }
    REQUIRE(sub != nullptr);

    bool has_inner = false;
    for (const std::string& t : sub->tags) {
        if (t.find("inner_artist") != std::string::npos) has_inner = true;
    }
    CHECK(has_inner);

    ziptest::cleanup_dir(dir);
}
