// Phase 37: pure sort/cycle/label logic for persisted per-gallery sort order.
// SDL/Vault&-free — operates on vault::IndexNode/vault::SortKey directly.

#include "test_framework.h"
#include "ui/gallery_sort.h"

#include <string>
#include <vector>

using vault::IndexNode;
using vault::SortKey;

namespace {

IndexNode make_gallery(std::string name)
{
    return IndexNode::gallery(std::move(name));
}

IndexNode make_image(std::string name, uint64_t created_ts, uint64_t orig_size)
{
    IndexNode n = IndexNode::image(std::move(name));
    n.meta.created_ts = created_ts;
    n.meta.orig_size = orig_size;
    return n;
}

std::vector<std::string> names(const std::vector<const IndexNode*>& v)
{
    std::vector<std::string> out;
    out.reserve(v.size());
    for (const auto* n : v) out.push_back(n->name);
    return out;
}

} // namespace

TEST(gallery_sort_insertion_is_a_true_noop)
{
    IndexNode a = make_image("z.jpg", 3, 30);
    IndexNode b = make_image("a.jpg", 1, 10);
    IndexNode c = make_image("m.jpg", 2, 20);
    std::vector<const IndexNode*> in{&a, &b, &c};   // deliberately unsorted

    const auto out = ui::sort_children(in, SortKey::Insertion);
    CHECK(names(out) == std::vector<std::string>({"z.jpg", "a.jpg", "m.jpg"}));
}

TEST(gallery_sort_name_natural_beats_lexical)
{
    IndexNode i1 = make_image("image1.jpg", 0, 0);
    IndexNode i2 = make_image("image2.jpg", 0, 0);
    IndexNode i10 = make_image("image10.jpg", 0, 0);
    std::vector<const IndexNode*> in{&i1, &i10, &i2};   // lexicographic order

    const auto asc = ui::sort_children(in, SortKey::NameAsc);
    CHECK(names(asc) == std::vector<std::string>({"image1.jpg", "image2.jpg", "image10.jpg"}));

    const auto desc = ui::sort_children(in, SortKey::NameDesc);
    CHECK(names(desc) == std::vector<std::string>({"image10.jpg", "image2.jpg", "image1.jpg"}));
}

TEST(gallery_sort_date_ascending_and_descending)
{
    IndexNode old_img = make_image("old.jpg", 100, 0);
    IndexNode new_img = make_image("new.jpg", 300, 0);
    IndexNode mid_img = make_image("mid.jpg", 200, 0);
    std::vector<const IndexNode*> in{&new_img, &old_img, &mid_img};

    const auto asc = ui::sort_children(in, SortKey::DateAsc);
    CHECK(names(asc) == std::vector<std::string>({"old.jpg", "mid.jpg", "new.jpg"}));

    const auto desc = ui::sort_children(in, SortKey::DateDesc);
    CHECK(names(desc) == std::vector<std::string>({"new.jpg", "mid.jpg", "old.jpg"}));
}

TEST(gallery_sort_size_ascending_and_descending)
{
    IndexNode small = make_image("small.jpg", 0, 100);
    IndexNode big = make_image("big.jpg", 0, 900);
    IndexNode mid = make_image("mid.jpg", 0, 500);
    std::vector<const IndexNode*> in{&big, &small, &mid};

    const auto asc = ui::sort_children(in, SortKey::SizeAsc);
    CHECK(names(asc) == std::vector<std::string>({"small.jpg", "mid.jpg", "big.jpg"}));

    const auto desc = ui::sort_children(in, SortKey::SizeDesc);
    CHECK(names(desc) == std::vector<std::string>({"big.jpg", "mid.jpg", "small.jpg"}));
}

TEST(gallery_sort_folders_always_precede_media_regardless_of_key)
{
    IndexNode img = make_image("a_image.jpg", 5, 5);      // would sort first alphabetically
    IndexNode gal = make_gallery("z_gallery");             // would sort last alphabetically
    std::vector<const IndexNode*> in{&img, &gal};

    const auto out = ui::sort_children(in, SortKey::NameAsc);
    REQUIRE(out.size() == 2);
    CHECK_TRUE(out[0]->is_gallery());
    CHECK_EQ(out[0]->name, std::string("z_gallery"));
    CHECK_EQ(out[1]->name, std::string("a_image.jpg"));
}

TEST(gallery_sort_ties_are_stable)
{
    // Two images with identical created_ts under DateAsc: input order preserved.
    IndexNode first = make_image("first.jpg", 42, 0);
    IndexNode second = make_image("second.jpg", 42, 0);
    std::vector<const IndexNode*> in{&first, &second};

    const auto out = ui::sort_children(in, SortKey::DateAsc);
    CHECK(names(out) == std::vector<std::string>({"first.jpg", "second.jpg"}));
}

TEST(gallery_sort_next_sort_key_cycles_through_all_seven_and_wraps)
{
    using enum SortKey;
    CHECK(ui::next_sort_key(Default) == NameAsc);
    CHECK(ui::next_sort_key(NameAsc) == NameDesc);
    CHECK(ui::next_sort_key(NameDesc) == DateAsc);
    CHECK(ui::next_sort_key(DateAsc) == DateDesc);
    CHECK(ui::next_sort_key(DateDesc) == SizeAsc);
    CHECK(ui::next_sort_key(SizeAsc) == SizeDesc);
    CHECK(ui::next_sort_key(SizeDesc) == Default);   // wraps
}

TEST(gallery_sort_key_label_empty_for_default_nonempty_otherwise)
{
    CHECK_TRUE(ui::sort_key_label(SortKey::Default).empty());
    CHECK_FALSE(ui::sort_key_label(SortKey::NameAsc).empty());
    CHECK_FALSE(ui::sort_key_label(SortKey::NameDesc).empty());
    CHECK_FALSE(ui::sort_key_label(SortKey::DateAsc).empty());
    CHECK_FALSE(ui::sort_key_label(SortKey::DateDesc).empty());
    CHECK_FALSE(ui::sort_key_label(SortKey::SizeAsc).empty());
    CHECK_FALSE(ui::sort_key_label(SortKey::SizeDesc).empty());
}
