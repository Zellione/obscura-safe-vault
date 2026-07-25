// Phase 53: the depth-first walker that turns nested archives into sub-galleries.
//
// Driven entirely through injected hooks, so the recursion is exercised without
// miniz or libarchive. "Archives" here are a tiny in-test format: a buffer whose
// first byte names a fake archive id, resolved against a table of entry lists.

#include "test_framework.h"
#include "ui/recursive_import.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using ui::ArchiveKind;
using ui::RecursiveHooks;
using ui::RecursiveTally;
using ui::walk_archive;

namespace {

struct FakeEntry {
    std::string name;
    bool        is_dir = false;
    std::string child_archive_id;   // non-empty => this entry IS an archive
};

struct FakeWorld {
    std::map<std::string, std::vector<FakeEntry>> archives;   // id -> entries
    std::vector<std::string>                      created;    // galleries created
    std::vector<std::string>                      placed;     // "gallery|filename"
    std::vector<std::string>                      unreadable; // archive ids that fail to list
};

// A fake archive buffer is real ZIP magic followed by its id. The magic is not
// decoration: walk_archive re-checks every nested candidate with
// detect_archive_kind, so a payload without it is correctly refused as "the
// extension lied".
constexpr std::string_view ZIP_MAGIC = "PK\x03\x04";

std::vector<uint8_t> buf_for(const std::string& id)
{
    std::string s(ZIP_MAGIC);
    s += id;
    return {s.begin(), s.end()};
}

std::string id_of(std::span<const uint8_t> b)
{
    std::string s(b.begin(), b.end());
    if (s.starts_with(ZIP_MAGIC)) s.erase(0, ZIP_MAGIC.size());
    return s;
}

RecursiveHooks hooks_for(FakeWorld& w)
{
    RecursiveHooks h;
    h.list_entries = [&w](std::span<const uint8_t> bytes, ArchiveKind,
                          std::vector<ui::ZipEntry>& out) {
        const std::string id = id_of(bytes);
        if (std::ranges::find(w.unreadable, id) != w.unreadable.end()) return false;
        const auto it = w.archives.find(id);
        if (it == w.archives.end()) return false;
        for (const FakeEntry& e : it->second) out.push_back({e.name, e.is_dir});
        return true;
    };
    h.extract_entry = [&w](std::span<const uint8_t> bytes, ArchiveKind, std::size_t idx,
                           crypto::SecureBytes& out) {
        const auto it = w.archives.find(id_of(bytes));
        if (it == w.archives.end() || idx >= it->second.size()) return false;
        const FakeEntry& e = it->second[idx];
        // An archive entry yields the child's id as its bytes; media yields a
        // few bytes so the budget sees non-zero expansion.
        const std::string payload = e.child_archive_id.empty()
                                        ? std::string("media")
                                        : std::string(ZIP_MAGIC) + e.child_archive_id;
        if (!out.resize(payload.size())) return false;
        std::memcpy(out.data(), payload.data(), payload.size());
        return true;
    };
    h.create_gallery = [&w](std::string_view g) { w.created.emplace_back(g); return true; };
    h.place_media    = [&w](std::string_view g, std::string_view f, std::span<const uint8_t>) {
        w.placed.push_back(std::string(g) + "|" + std::string(f));
        return true;
    };
    h.cancelled = [] { return false; };
    return h;
}

bool placed_contains(const FakeWorld& w, std::string_view what)
{
    return std::ranges::find(w.placed, std::string(what)) != w.placed.end();
}

} // namespace

TEST(walk_archive_places_flat_media)
{
    FakeWorld w;
    w.archives["root"] = {{.name = "a.jpg", .is_dir = false, .child_archive_id = ""}, {.name = "b.png", .is_dir = false, .child_archive_id = ""}};

    const auto t = walk_archive(buf_for("root"), ArchiveKind::Zip, "Dest", hooks_for(w));

    CHECK_EQ(t.media_placed, 2);
    CHECK_EQ(t.nested_archives, 0);
    CHECK(placed_contains(w, "Dest|a.jpg"));
}

TEST(walk_archive_descends_into_a_nested_archive_as_a_sub_gallery)
{
    FakeWorld w;
    w.archives["root"]  = {{.name = "a.jpg", .is_dir = false, .child_archive_id = ""}, {.name = "bonus.zip", .is_dir = false, .child_archive_id = "inner"}};
    w.archives["inner"] = {{.name = "c.jpg", .is_dir = false, .child_archive_id = ""}};

    const auto t = walk_archive(buf_for("root"), ArchiveKind::Zip, "Dest", hooks_for(w));

    CHECK_EQ(t.nested_archives, 1);
    CHECK_EQ(t.media_placed, 2);
    // Extension stripped: bonus.zip -> gallery "bonus".
    CHECK(placed_contains(w, "Dest/bonus|c.jpg"));
}

TEST(walk_archive_recurses_several_levels_deep)
{
    FakeWorld w;
    w.archives["l0"] = {{.name = "one.zip", .is_dir = false, .child_archive_id = "l1"}};
    w.archives["l1"] = {{.name = "two.zip", .is_dir = false, .child_archive_id = "l2"}};
    w.archives["l2"] = {{.name = "deep.jpg", .is_dir = false, .child_archive_id = ""}};

    const auto t = walk_archive(buf_for("l0"), ArchiveKind::Zip, "D", hooks_for(w));

    CHECK_EQ(t.nested_archives, 2);
    CHECK(placed_contains(w, "D/one/two|deep.jpg"));
}

TEST(walk_archive_stops_at_the_depth_cap_without_failing_the_job)
{
    FakeWorld w;
    w.archives["l0"] = {{.name = "sibling.jpg", .is_dir = false, .child_archive_id = ""}, {.name = "one.zip", .is_dir = false, .child_archive_id = "l1"}};
    w.archives["l1"] = {{.name = "two.zip", .is_dir = false, .child_archive_id = "l2"}};
    w.archives["l2"] = {{.name = "deep.jpg", .is_dir = false, .child_archive_id = ""}};

    ui::RecursionLimits lim;
    lim.max_depth = 1;   // may enter one level only
    const auto t = walk_archive(buf_for("l0"), ArchiveKind::Zip, "D", hooks_for(w), lim);

    CHECK(t.depth_capped > 0);
    // The sibling media next to the too-deep archive still imported.
    CHECK(placed_contains(w, "D|sibling.jpg"));
}

TEST(walk_archive_does_not_descend_into_a_cbz)
{
    // A comic archive is a flat run of pages; an archive inside one is not a
    // chapter.
    FakeWorld w;
    w.archives["root"]  = {{.name = "001.jpg", .is_dir = false, .child_archive_id = ""}, {.name = "bonus.zip", .is_dir = false, .child_archive_id = "inner"}};
    w.archives["inner"] = {{.name = "c.jpg", .is_dir = false, .child_archive_id = ""}};

    const auto t = walk_archive(buf_for("root"), ArchiveKind::Cbz, "D", hooks_for(w));

    CHECK_EQ(t.nested_archives, 0);
    CHECK_FALSE(placed_contains(w, "D/bonus|c.jpg"));
}

TEST(walk_archive_tallies_an_unreadable_nested_archive_and_carries_on)
{
    FakeWorld w;
    w.archives["root"]  = {{.name = "ok.jpg", .is_dir = false, .child_archive_id = ""}, {.name = "broken.zip", .is_dir = false, .child_archive_id = "bad"}};
    w.archives["bad"]   = {};
    w.unreadable        = {"bad"};

    const auto t = walk_archive(buf_for("root"), ArchiveKind::Zip, "D", hooks_for(w));

    CHECK_EQ(t.unreadable, 1);
    CHECK(placed_contains(w, "D|ok.jpg"));   // siblings unaffected
}

TEST(walk_archive_skips_non_media_non_archive_entries)
{
    FakeWorld w;
    w.archives["root"] = {{.name = "a.jpg", .is_dir = false, .child_archive_id = ""}, {.name = "readme.txt", .is_dir = false, .child_archive_id = ""}};

    const auto t = walk_archive(buf_for("root"), ArchiveKind::Zip, "D", hooks_for(w));

    CHECK_EQ(t.skipped_unsupported, 1);
    CHECK_EQ(t.media_placed, 1);
}

TEST(walk_archive_stops_when_cancelled)
{
    FakeWorld w;
    w.archives["root"] = {{.name = "a.jpg", .is_dir = false, .child_archive_id = ""}, {.name = "b.jpg", .is_dir = false, .child_archive_id = ""}, {.name = "c.jpg", .is_dir = false, .child_archive_id = ""}};

    RecursiveHooks h = hooks_for(w);
    h.cancelled      = [&w] { return w.placed.size() >= 1; };

    const auto t = walk_archive(buf_for("root"), ArchiveKind::Zip, "D", h);
    CHECK(t.media_placed < 3);
}

TEST(walk_archive_disambiguates_two_nested_archives_with_the_same_name)
{
    // Two sibling directories each holding "bonus.zip" collapse onto the same
    // sub-gallery name unless suffixed.
    FakeWorld w;
    w.archives["root"]  = {{.name = "x/bonus.zip", .is_dir = false, .child_archive_id = "i1"},
                           {.name = "y/bonus.zip", .is_dir = false, .child_archive_id = "i2"}};
    w.archives["i1"]    = {{.name = "one.jpg", .is_dir = false, .child_archive_id = ""}};
    w.archives["i2"]    = {{.name = "two.jpg", .is_dir = false, .child_archive_id = ""}};

    const auto t = walk_archive(buf_for("root"), ArchiveKind::Zip, "D", hooks_for(w));

    CHECK_EQ(t.nested_archives, 2);
    CHECK_EQ(t.media_placed, 2);
}
