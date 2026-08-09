#include "test_framework.h"

#include "ui/favorite_batch.h"
#include "vault/index.h"

#include <vector>

// Phase 68: the batch favorite rule — if ANY selected node is not yet a
// favorite the batch favorites everything, else it unfavorites everything.
// (Mirrors the familiar "select-all checkbox" convention.)

namespace {

vault::IndexNode node(bool fav)
{
    vault::IndexNode n;
    n.type = vault::IndexNode::Type::Image;
    n.favorite = fav;
    return n;
}

}  // namespace

TEST(batch_favorite_target_true_when_any_unfavorited)
{
    const auto a = node(true);
    const auto b = node(false);
    const std::vector<const vault::IndexNode*> sel{&a, &b};
    CHECK_TRUE(ui::batch_favorite_target(sel));
}

TEST(batch_favorite_target_false_when_all_favorited)
{
    const auto a = node(true);
    const auto b = node(true);
    const std::vector<const vault::IndexNode*> sel{&a, &b};
    CHECK_TRUE(!ui::batch_favorite_target(sel));
}

TEST(batch_favorite_target_false_on_empty)
{
    const std::vector<const vault::IndexNode*> sel;
    CHECK_TRUE(!ui::batch_favorite_target(sel));
}
