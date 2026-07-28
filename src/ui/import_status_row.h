#pragma once

// Phase 56: what one Import Status row says and how tall it is. Pure — no SDL,
// no renderer — so every state's wording is unit-tested.
//
// A row is TWO lines: the route (`source → destination`) above, the status
// (progress bar or outcome) below. Before this, the two shared one vertically
// centred band, so the progress bar was drawn straight through the text, and a
// finished row replaced its route with an outcome — losing what was imported
// and where it went at exactly the moment the user wants to check.

#include <string>

namespace ui {

struct ImportTaskInfo;   // ui/import_model.h

// Line 1: "<display name> → <destination>", with the vault root spelled "root"
// rather than left blank. Present in every state.
[[nodiscard]] std::string format_task_route(const ImportTaskInfo& task);

// Line 2 for every state EXCEPT Running (whose second line is the progress bar,
// drawn by the screen; this returns the accompanying done/total text).
[[nodiscard]] std::string format_task_status(const ImportTaskInfo& task);

// Total row height: two whole text pitches plus `pad` above and below.
[[nodiscard]] float import_row_height(float font_px, float pad);

} // namespace ui
