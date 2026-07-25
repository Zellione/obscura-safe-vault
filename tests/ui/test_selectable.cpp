// Phase 53: the one rule for what can go in a selection.
//
// Previously spelled out inline in three places. The risk being guarded here is
// drift: if Ctrl+A ever selects a type that Space refuses, a mass operation
// receives a node it cannot process — which is how export used to be handed a
// video it would reject with InvalidArg.

#include "test_framework.h"
#include "ui/selectable.h"
#include "vault/index.h"

#include <vector>

namespace {

vault::IndexNode make(vault::IndexNode::Type t)
{
    vault::IndexNode n;
    n.type = t;
    return n;
}

} // namespace

TEST(selectable_accepts_images_videos_and_galleries)
{
    const auto img = make(vault::IndexNode::Type::Image);
    const auto vid = make(vault::IndexNode::Type::Video);
    const auto gal = make(vault::IndexNode::Type::Gallery);

    CHECK(ui::is_selectable(img));
    CHECK(ui::is_selectable(vid));
    CHECK(ui::is_selectable(gal));
}

TEST(selectable_indices_returns_positions_in_order)
{
    const auto a = make(vault::IndexNode::Type::Image);
    const auto b = make(vault::IndexNode::Type::Video);
    const auto c = make(vault::IndexNode::Type::Gallery);
    const std::vector<const vault::IndexNode*> kids{&a, &b, &c};

    const auto got = ui::selectable_indices(kids);
    REQUIRE(got.size() == 3);
    CHECK_EQ(got[0], 0);
    CHECK_EQ(got[1], 1);
    CHECK_EQ(got[2], 2);
}

TEST(selectable_indices_is_empty_for_an_empty_listing)
{
    const std::vector<const vault::IndexNode*> kids;
    CHECK(ui::selectable_indices(kids).empty());
}

TEST(selectable_indices_skips_null_children)
{
    // children_ is a vector of borrowed pointers; a null must not crash the
    // Ctrl+A path.
    const auto a = make(vault::IndexNode::Type::Image);
    const std::vector<const vault::IndexNode*> kids{&a, nullptr};

    const auto got = ui::selectable_indices(kids);
    REQUIRE(got.size() == 1);
    CHECK_EQ(got[0], 0);
}
