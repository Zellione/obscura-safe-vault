#include "test_framework.h"

#include "ui/cover_cache.h"
#include "ui/gallery_cover.h"
#include "vault/index.h"

using ui::CoverCache;
using ui::CoverSpan;
using vault::IndexNode;

namespace {
// An image node with a stored thumbnail at (offset, length).
IndexNode img(std::string name, uint64_t off, uint64_t len)
{
    IndexNode n = IndexNode::image(std::move(name));
    n.meta.thumb_offset = off;
    n.meta.thumb_length = len;
    return n;
}

}  // namespace

TEST(cover_cache_matches_resolve_covers)
{
    // gallery with 2 sub-galleries each holding a thumbed image
    IndexNode s1 = IndexNode::gallery("s1");
    s1.children.push_back(img("a.jpg", 11, 1));
    IndexNode s2 = IndexNode::gallery("s2");
    s2.children.push_back(img("b.jpg", 22, 2));

    IndexNode g = IndexNode::gallery("root");
    g.children.push_back(std::move(s1));
    g.children.push_back(std::move(s2));

    CoverCache cc;
    const auto cached = cc.get(g);
    const auto direct = ui::resolve_covers(g);
    CHECK_EQ(cached.size(), direct.size());
    for (size_t i = 0; i < direct.size(); ++i) CHECK(cached[i] == direct[i]);
}

TEST(cover_cache_memoises_until_clear)
{
    // gallery with one sub-gallery holding a thumbed image
    IndexNode s1 = IndexNode::gallery("s1");
    s1.children.push_back(img("a.jpg", 100, 20));

    IndexNode g = IndexNode::gallery("root");
    g.children.push_back(std::move(s1));

    CoverCache cc;
    const auto before = std::vector(cc.get(g).begin(), cc.get(g).end());
    CHECK_EQ(before.size(), 1);

    // add a second sub-gallery holding a thumbed image
    IndexNode s2 = IndexNode::gallery("s2");
    s2.children.push_back(img("b.jpg", 200, 30));
    g.children.push_back(std::move(s2));
    CHECK_EQ(cc.get(g).size(), before.size());   // still the memoised answer

    cc.clear();
    CHECK(cc.get(g).size() != before.size());    // recomputed after clear
}
