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
    s.record("A/B", 3);

    s.reset();

    CHECK(s.view == GalleryView::Grid);
    CHECK(s.strip_side == StripSide::Bottom);
    CHECK(s.last_media_path.empty());
    CHECK_EQ(s.video_resume_seconds, 0.0);
    CHECK_EQ(s.recall("A/B"), 0);
}

// --- record/recall (Phase 40 Part 2: session-scoped gallery position memory) ---

TEST(gallery_session_state_recall_defaults_to_zero_for_unknown_path)
{
    GallerySessionState s;
    CHECK_EQ(s.recall(""), 0);
    CHECK_EQ(s.recall("A/B/C"), 0);
}

TEST(gallery_session_state_record_then_recall_round_trips)
{
    GallerySessionState s;
    s.record("A/B", 4);
    CHECK_EQ(s.recall("A/B"), 4);
}

TEST(gallery_session_state_record_overwrites_on_repeat_visit)
{
    GallerySessionState s;
    s.record("A", 2);
    CHECK_EQ(s.recall("A"), 2);
    s.record("A", 7);
    CHECK_EQ(s.recall("A"), 7);
}

TEST(gallery_session_state_record_tracks_independent_paths)
{
    GallerySessionState s;
    s.record("A", 1);
    s.record("A/B", 5);
    s.record("", 9);   // root gallery

    CHECK_EQ(s.recall("A"), 1);
    CHECK_EQ(s.recall("A/B"), 5);
    CHECK_EQ(s.recall(""), 9);
}
