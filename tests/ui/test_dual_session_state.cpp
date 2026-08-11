#include "test_framework.h"

#include "ui/dual_session_state.h"

using ui::DualSessionState;
using ui::GalleryView;

TEST(dual_session_state_defaults)
{
    DualSessionState s;
    CHECK(!s.split_active);
    CHECK_EQ(s.active_pane, 0);
    CHECK(s.pane[0].path.empty());
    CHECK_EQ(s.pane[1].selected, 0);
    CHECK(s.pane[0].selected_tiles.empty());
}

TEST(dual_session_state_reset_clears_every_field)
{
    DualSessionState s;
    s.split_active            = true;
    s.active_pane             = 1;
    s.pane[0].path            = "a/b";
    s.pane[0].selected        = 4;
    s.pane[0].scroll          = 123.0f;
    s.pane[0].view            = GalleryView::List;
    s.pane[0].detail_open     = true;
    s.pane[1].selected_tiles  = {1, 2, 3};

    s.reset();

    CHECK(!s.split_active);
    CHECK_EQ(s.active_pane, 0);
    CHECK(s.pane[0].path.empty());
    CHECK_EQ(s.pane[0].selected, 0);
    CHECK_EQ(s.pane[0].scroll, 0.0f);
    CHECK(s.pane[0].view == GalleryView::GridM);
    CHECK(!s.pane[0].detail_open);
    CHECK(s.pane[1].selected_tiles.empty());
}
