#include "test_framework.h"

#include "ui/gallery_session_state.h"

using ui::GallerySessionState;
using ui::GalleryView;
using ui::StripSide;

TEST(gallery_session_state_defaults)
{
    GallerySessionState s;
    CHECK(s.view == GalleryView::Grid);
    CHECK(s.strip_side == StripSide::Bottom);
    CHECK(s.last_media_path.empty());
    CHECK_EQ(s.video_resume_seconds, 0.0);
}

TEST(gallery_session_state_reset_clears_every_field)
{
    GallerySessionState s;
    s.view                 = GalleryView::List;
    s.strip_side           = StripSide::Left;
    s.last_media_path      = "gallery/video.mp4";
    s.video_resume_seconds = 42.5;

    s.reset();

    CHECK(s.view == GalleryView::Grid);
    CHECK(s.strip_side == StripSide::Bottom);
    CHECK(s.last_media_path.empty());
    CHECK_EQ(s.video_resume_seconds, 0.0);
}
