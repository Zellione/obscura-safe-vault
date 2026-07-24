#include "ui/import_model.h"

#include <algorithm>
#include <format>
#include <ranges>

namespace ui {

bool reorder_import_task(std::vector<ImportTaskInfo>& tasks,
                        uint64_t id, int delta)
{
    // Find the task with the given ID
    auto it = std::ranges::find_if(tasks,
                                   [id](const ImportTaskInfo& t) { return t.id == id; });

    if (it == tasks.end() || it->state != ImportTaskState::Queued) {
        return false;  // Task not found or not queued
    }

    size_t current_idx = std::distance(tasks.begin(), it);
    int new_idx = static_cast<int>(current_idx) + delta;

    // Bounds check: must stay within queued span
    if (new_idx < 0 || new_idx >= static_cast<int>(tasks.size())) {
        return false;
    }

    // Check that new position is also queued (don't cross into running/finished)
    if (tasks[new_idx].state != ImportTaskState::Queued) {
        return false;
    }

    // Swap the tasks
    std::swap(tasks[current_idx], tasks[new_idx]);
    return true;
}

int clear_finished_imports(std::vector<ImportTaskInfo>& tasks)
{
    const auto initial_size = tasks.size();

    // Erase all finished tasks
    std::erase_if(tasks,
                 [](const ImportTaskInfo& t) {
                     return import_task_finished(t.state);
                 });

    return static_cast<int>(initial_size - tasks.size());
}

std::string footer_import_summary(const std::vector<ImportTaskInfo>& tasks,
                                 bool lane_failed,
                                 std::string_view lane_error)
{
    // If lane has failed, show the error message (this wins over everything)
    if (lane_failed) {
        if (!lane_error.empty()) {
            return std::string("Import failed: ") + std::string(lane_error);
        } else {
            return "Import failed";
        }
    }

    // Find running and queued counts
    int running_count = 0;
    int queued_count = 0;
    int running_idx = -1;

    for (size_t i = 0; i < tasks.size(); ++i) {
        if (tasks[i].state == ImportTaskState::Running) {
            if (running_idx == -1) {
                running_idx = static_cast<int>(i);  // Capture FIRST running task
            }
            ++running_count;
        } else if (tasks[i].state == ImportTaskState::Queued) {
            ++queued_count;
        }
    }

    // Nothing running or queued
    if (running_count == 0 && queued_count == 0) {
        return "";
    }

    // If we have running tasks, show the first one with progress
    if (running_count > 0) {
        const auto& running_task = tasks[running_idx];
        std::string summary = std::format("Importing {} {}/{}", running_task.display_name,
                                         running_task.done, running_task.total);

        if (queued_count > 0) {
            summary += std::format(" · {} queued", queued_count);
        }

        return summary;
    }

    // No running, but have queued tasks
    if (queued_count == 1) {
        return "1 import queued";
    } else {
        return std::format("Importing {} 0/0 · {} queued", tasks[0].display_name,
                          queued_count - 1);
    }
}

std::string import_lock_confirm_text(int pending_tasks)
{
    if (pending_tasks == 1) {
        return "1 import pending — finish current file, discard the rest, and lock?";
    } else {
        return std::format("{} imports pending — finish current file, discard the rest, and lock?", pending_tasks);
    }
}

} // namespace ui
