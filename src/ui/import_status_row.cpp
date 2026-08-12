#include "ui/import_status_row.h"

#include <algorithm>
#include <format>

#include "ui/import_model.h"
#include "ui/text_metrics.h"

namespace ui {

std::string format_task_route(const ImportTaskInfo& task)
{
    // "->" not "→", and words not ✓/✗ below: the bundled Noto Sans subset has
    // neither arrow nor check/ballot glyphs, so all three drew nothing (Phase 83).
    return std::format("{} -> {}", task.display_name,
                       task.dest_gallery.empty() ? "root" : task.dest_gallery);
}

std::string format_task_status(const ImportTaskInfo& task)
{
    using enum ImportTaskState;
    switch (task.state) {
        case Running:
            return format_task_progress(task.done, task.total, task.expanding);
        case Queued:
            return std::format("Queued #{}", task.id & 0xFF);
        case Done:
            return std::format("Done — {} imported, {} skipped", task.imported, task.skipped);
        case Failed:
            return task.error.empty() ? std::string("Failed — import error")
                                      : std::format("Failed — {}", task.error);
        case Cancelled:
            return std::string("Cancelled");
    }
    return std::string();
}

float import_row_height(float font_px, float pad)
{
    return 2.0f * line_pitch(font_px) + 2.0f * std::max(0.0f, pad);
}

} // namespace ui
