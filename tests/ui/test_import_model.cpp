#include "test_framework.h"
#include "ui/import_model.h"

#include <vector>

// Pure import queue/summary/batch models (Phase 50, Task 5).
// Tests: reorder within queued span only; clear_finished removes exactly
// finished states; footer summary exact strings; BatchCommitPolicy state machine.

namespace {

// Helper to build a simple ImportTaskInfo for testing.
ui::ImportTaskInfo make_task(uint64_t id, ui::ImportTaskState state,
                              const std::string& name = "task")
{
    ui::ImportTaskInfo t;
    t.id = id;
    t.state = state;
    t.display_name = name;
    return t;
}

} // namespace

TEST(reorder_import_task_moves_within_queued_span)
{
    // Build: Running(1) / Queued(2) / Queued(3) / Done(4)
    std::vector<ui::ImportTaskInfo> tasks = {
        make_task(1, ui::ImportTaskState::Running, "r"),
        make_task(2, ui::ImportTaskState::Queued, "q1"),
        make_task(3, ui::ImportTaskState::Queued, "q2"),
        make_task(4, ui::ImportTaskState::Done, "d"),
    };

    // Reorder task 2 down by +1 → should swap with 3, never move past running or done
    const bool moved = ui::reorder_import_task(tasks, 2, 1);
    REQUIRE(moved);
    CHECK_EQ(tasks[1].id, static_cast<uint64_t>(3));
    CHECK_EQ(tasks[2].id, static_cast<uint64_t>(2));
    CHECK_EQ(tasks[0].id, static_cast<uint64_t>(1));  // Running unchanged
    CHECK_EQ(tasks[3].id, static_cast<uint64_t>(4));  // Done unchanged
}

TEST(reorder_import_task_never_crosses_boundaries)
{
    // Build: Queued(1) / Running(2) / Queued(3) / Done(4)
    std::vector<ui::ImportTaskInfo> tasks = {
        make_task(1, ui::ImportTaskState::Queued, "q1"),
        make_task(2, ui::ImportTaskState::Running, "r"),
        make_task(3, ui::ImportTaskState::Queued, "q2"),
        make_task(4, ui::ImportTaskState::Done, "d"),
    };

    // Try to move queued task 1 into the running section → no-op
    const bool moved = ui::reorder_import_task(tasks, 1, 1);
    CHECK_FALSE(moved);
    CHECK_EQ(tasks[0].id, static_cast<uint64_t>(1));

    // Try to move queued task 3 past done → no-op
    const bool moved2 = ui::reorder_import_task(tasks, 3, 1);
    CHECK_FALSE(moved2);
    CHECK_EQ(tasks[2].id, static_cast<uint64_t>(3));

    // Try to move running task 2 → no-op (not queued)
    const bool moved3 = ui::reorder_import_task(tasks, 2, -1);
    CHECK_FALSE(moved3);
    CHECK_EQ(tasks[1].id, static_cast<uint64_t>(2));
}

TEST(reorder_import_task_returns_false_when_id_not_found)
{
    std::vector<ui::ImportTaskInfo> tasks = {
        make_task(1, ui::ImportTaskState::Queued, "q1"),
    };

    const bool moved = ui::reorder_import_task(tasks, 999, 1);
    CHECK_FALSE(moved);
}

TEST(clear_finished_imports_removes_done_failed_cancelled)
{
    std::vector<ui::ImportTaskInfo> tasks = {
        make_task(1, ui::ImportTaskState::Running, "r"),
        make_task(2, ui::ImportTaskState::Done, "d"),
        make_task(3, ui::ImportTaskState::Queued, "q"),
        make_task(4, ui::ImportTaskState::Failed, "f"),
        make_task(5, ui::ImportTaskState::Cancelled, "c"),
    };

    const int removed = ui::clear_finished_imports(tasks);
    CHECK_EQ(removed, 3);
    REQUIRE(tasks.size() == static_cast<size_t>(2));
    CHECK_EQ(tasks[0].id, static_cast<uint64_t>(1));  // Running stays
    CHECK_EQ(tasks[1].id, static_cast<uint64_t>(3));  // Queued stays
}

TEST(clear_finished_imports_returns_zero_when_empty)
{
    std::vector<ui::ImportTaskInfo> tasks = {
        make_task(1, ui::ImportTaskState::Running, "r"),
        make_task(2, ui::ImportTaskState::Queued, "q"),
    };

    const int removed = ui::clear_finished_imports(tasks);
    CHECK_EQ(removed, 0);
    REQUIRE(tasks.size() == static_cast<size_t>(2));
}

TEST(footer_import_summary_running_only)
{
    std::vector<ui::ImportTaskInfo> tasks = {
        make_task(1, ui::ImportTaskState::Running, "archive.zip"),
    };
    tasks[0].done = 128;
    tasks[0].total = 450;

    const std::string summary = ui::footer_import_summary(tasks, false);
    CHECK_EQ(summary, std::string("Importing archive.zip 128/450"));
}

TEST(footer_import_summary_running_plus_queued)
{
    std::vector<ui::ImportTaskInfo> tasks = {
        make_task(1, ui::ImportTaskState::Running, "archive.zip"),
        make_task(2, ui::ImportTaskState::Queued, "a"),
        make_task(3, ui::ImportTaskState::Queued, "b"),
    };
    tasks[0].done = 128;
    tasks[0].total = 450;

    const std::string summary = ui::footer_import_summary(tasks, false);
    CHECK_EQ(summary, std::string("Importing archive.zip 128/450 · 2 queued"));
}

TEST(footer_import_summary_empty)
{
    std::vector<ui::ImportTaskInfo> tasks = {};

    const std::string summary = ui::footer_import_summary(tasks, false);
    CHECK_EQ(summary, std::string(""));
}

TEST(footer_import_summary_queued_only)
{
    std::vector<ui::ImportTaskInfo> tasks = {
        make_task(1, ui::ImportTaskState::Queued, "first_import"),
        make_task(2, ui::ImportTaskState::Queued, "second_import"),
        make_task(3, ui::ImportTaskState::Queued, "third_import"),
    };

    const std::string summary = ui::footer_import_summary(tasks, false);
    CHECK_EQ(summary, std::string("Importing first_import 0/0 · 2 queued"));
}

TEST(footer_import_summary_lane_failed_wins)
{
    std::vector<ui::ImportTaskInfo> tasks = {
        make_task(1, ui::ImportTaskState::Running, "archive.zip"),
    };
    tasks[0].done = 10;
    tasks[0].total = 100;

    const std::string summary = ui::footer_import_summary(tasks, true);
    CHECK_EQ(summary, std::string("Import failed: (no details)"));
}

TEST(footer_import_summary_single_import_queued)
{
    std::vector<ui::ImportTaskInfo> tasks = {
        make_task(1, ui::ImportTaskState::Queued, "single_import"),
    };

    const std::string summary = ui::footer_import_summary(tasks, false);
    CHECK_EQ(summary, std::string("1 import queued"));
}

TEST(batch_commit_policy_not_due_at_zero)
{
    ui::BatchCommitPolicy policy;
    CHECK_FALSE(policy.due());
}

TEST(batch_commit_policy_due_at_32_files)
{
    ui::BatchCommitPolicy policy;
    policy.note_attached(32);
    CHECK(policy.due());
}

TEST(batch_commit_policy_due_at_1_file_plus_2_seconds)
{
    ui::BatchCommitPolicy policy;
    policy.note_attached(1);
    policy.tick(2.0);
    CHECK(policy.due());
}

TEST(batch_commit_policy_tick_without_files_never_accrues)
{
    ui::BatchCommitPolicy policy;
    policy.tick(5.0);  // Should not accrue since no files
    CHECK_FALSE(policy.due());
    CHECK_EQ(policy.secs_since_commit, 0.0);
}

TEST(batch_commit_policy_reset)
{
    ui::BatchCommitPolicy policy;
    policy.note_attached(10);
    policy.tick(1.5);
    policy.reset();
    CHECK_FALSE(policy.due());
    CHECK_EQ(policy.files_since_commit, 0);
    CHECK_EQ(policy.secs_since_commit, 0.0);
}

TEST(batch_commit_policy_partial_accumulation)
{
    ui::BatchCommitPolicy policy;
    policy.note_attached(20);  // 20/32 files, not due
    CHECK_FALSE(policy.due());
    policy.tick(1.0);          // 1/2 seconds, not due
    CHECK_FALSE(policy.due());
    policy.tick(1.0);          // 2/2 seconds and 20 > 0 files → due
    CHECK(policy.due());
}

TEST(import_task_info_equality)
{
    ui::ImportTaskInfo a;
    a.id = 1;
    a.kind = ui::ImportTaskKind::Files;
    a.display_name = "test";
    a.dest_gallery = "root";
    a.state = ui::ImportTaskState::Queued;
    a.done = 0;
    a.total = 100;
    a.imported = 0;
    a.skipped = 0;
    a.error = "";

    ui::ImportTaskInfo b = a;

    CHECK(a == b);

    b.id = 2;
    CHECK_FALSE(a == b);
}
