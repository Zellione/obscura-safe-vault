#include "test_framework.h"

#include "ui/dual_session_state.h"

using ui::DualSessionState;

TEST(dual_session_state_defaults)
{
    DualSessionState s;
    CHECK(!s.split_active);
    CHECK(!s.has_config);          // Phase 78: no pane configs saved yet
    CHECK_EQ(s.active_pane, 0);
    CHECK(s.pane[0].path.empty());
    CHECK_EQ(s.pane[1].selected, 0);
    CHECK(s.pane[0].selected_tiles.empty());
}

TEST(dual_session_state_reset_clears_every_field)
{
    DualSessionState s;
    s.split_active            = true;
    s.has_config              = true;  // Phase 78: mark configs saved
    s.active_pane             = 1;
    s.pane[0].path            = "a/b";
    s.pane[0].selected        = 4;
    s.pane[0].scroll = 123.0f;
    s.pane[0].detail_open     = true;
    s.pane[1].selected_tiles  = {1, 2, 3};

    s.reset();

    CHECK(!s.split_active);
    CHECK(!s.has_config);              // Phase 78: reset clears has_config
    CHECK_EQ(s.active_pane, 0);
    CHECK(s.pane[0].path.empty());
    CHECK_EQ(s.pane[0].selected, 0);
    CHECK_EQ(s.pane[0].scroll, 0.0f);
    CHECK(!s.pane[0].detail_open);
    CHECK(s.pane[1].selected_tiles.empty());
}
