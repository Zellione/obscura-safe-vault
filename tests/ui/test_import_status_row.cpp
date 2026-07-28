// Phase 56: Import Status rows are two lines — route above, status below. The
// route line is present in EVERY state (a finished import used to lose its
// source and destination entirely), and the status line owns its own band so
// the progress bar can never be drawn on top of text.

#include "test_framework.h"

#include "ui/import_model.h"
#include "ui/import_status_row.h"
#include "ui/text_metrics.h"

namespace {
ui::ImportTaskInfo task(ui::ImportTaskState state)
{
    ui::ImportTaskInfo t;
    t.id           = 7;
    t.display_name = "holiday.zip";
    t.dest_gallery = "Trips/2026";
    t.state        = state;
    return t;
}
} // namespace

TEST(route_line_shows_source_and_destination_while_queued)
{
    CHECK_EQ(ui::format_task_route(task(ui::ImportTaskState::Queued)),
             std::string("holiday.zip → Trips/2026"));
}

TEST(route_line_survives_completion)
{
    CHECK_EQ(ui::format_task_route(task(ui::ImportTaskState::Done)),
             std::string("holiday.zip → Trips/2026"));
    CHECK_EQ(ui::format_task_route(task(ui::ImportTaskState::Failed)),
             std::string("holiday.zip → Trips/2026"));
    CHECK_EQ(ui::format_task_route(task(ui::ImportTaskState::Cancelled)),
             std::string("holiday.zip → Trips/2026"));
}

TEST(route_line_names_the_root_gallery_explicitly)
{
    ui::ImportTaskInfo t = task(ui::ImportTaskState::Running);
    t.dest_gallery.clear();
    CHECK_EQ(ui::format_task_route(t), std::string("holiday.zip → root"));
}

TEST(status_line_reports_the_outcome_per_state)
{
    ui::ImportTaskInfo done = task(ui::ImportTaskState::Done);
    done.imported = 12;
    done.skipped  = 1;
    CHECK_EQ(ui::format_task_status(done), std::string("✓ 12 imported, 1 skipped"));

    ui::ImportTaskInfo failed = task(ui::ImportTaskState::Failed);
    failed.error = "vault write failed";
    CHECK_EQ(ui::format_task_status(failed), std::string("✗ vault write failed"));

    CHECK_EQ(ui::format_task_status(task(ui::ImportTaskState::Cancelled)),
             std::string("− Cancelled"));
}

TEST(status_line_falls_back_when_a_failure_has_no_message)
{
    CHECK_EQ(ui::format_task_status(task(ui::ImportTaskState::Failed)),
             std::string("✗ Import failed"));
}

TEST(status_line_shows_queue_position_when_queued)
{
    CHECK_EQ(ui::format_task_status(task(ui::ImportTaskState::Queued)),
             std::string("Queued #7"));
}

TEST(status_line_shows_progress_while_running)
{
    ui::ImportTaskInfo t = task(ui::ImportTaskState::Running);
    t.done  = 3;
    t.total = 10;
    CHECK_EQ(ui::format_task_status(t), ui::format_task_progress(3, 10, false));
}

TEST(row_is_two_full_pitches_plus_padding)
{
    CHECK_EQ(ui::import_row_height(28.0f, 9.0f), 2.0f * ui::line_pitch(28.0f) + 18.0f);
}

TEST(route_and_status_bands_do_not_intersect)
{
    constexpr float FONT_PX = 28.0f;
    constexpr float PAD     = 9.0f;
    const float pitch = ui::line_pitch(FONT_PX);

    const float route_top    = PAD;
    const float route_bottom = route_top + FONT_PX;
    const float status_top   = PAD + pitch;

    CHECK(route_bottom <= status_top);
    CHECK(status_top + FONT_PX <= ui::import_row_height(FONT_PX, PAD));
}
