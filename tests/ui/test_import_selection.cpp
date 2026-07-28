// Phase 56: Import Status selection is by task ID, not row position. With a
// positional selection, Ctrl+Up moved the row out from under the cursor, so the
// chord could not be repeated — and a completing import silently reordered the
// snapshot under the user's feet.

#include "test_framework.h"

#include <vector>

#include "ui/import_model.h"

namespace {
std::vector<ui::ImportTaskInfo> queue_of(std::initializer_list<uint64_t> ids)
{
    std::vector<ui::ImportTaskInfo> v;
    for (uint64_t id : ids) {
        ui::ImportTaskInfo t;
        t.id    = id;
        t.state = ui::ImportTaskState::Queued;
        v.push_back(t);
    }
    return v;
}
} // namespace

TEST(index_of_task_finds_a_row_by_id)
{
    const auto rows = queue_of({10, 11, 12});
    CHECK_EQ(ui::index_of_task(rows, 10), 0);
    CHECK_EQ(ui::index_of_task(rows, 12), 2);
}

TEST(index_of_task_reports_absence_rather_than_guessing)
{
    const auto rows = queue_of({10, 11});
    CHECK_EQ(ui::index_of_task(rows, 99), -1);
    CHECK_EQ(ui::index_of_task({}, 10), -1);
}

TEST(a_reordered_task_keeps_its_index_lookup)
{
    auto rows = queue_of({10, 11, 12});
    REQUIRE(ui::reorder_import_task(rows, 12, -1));
    CHECK_EQ(ui::index_of_task(rows, 12), 1);       // followed the move
    REQUIRE(ui::reorder_import_task(rows, 12, -1));
    CHECK_EQ(ui::index_of_task(rows, 12), 0);       // and again, from its new home
}
