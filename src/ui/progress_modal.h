#pragma once

#include <string_view>

namespace gfx { class Renderer; class FontAtlas; }

namespace ui {

// Draw a centred, full-window-veiling progress modal for a background operation
// (import / export / delete / move / copy) — Phase 25. Shared by every screen that
// hosts a worker-thread job so the "N / M" bar + cancel hint look identical.
// `count_line` is the caller-formatted "done / total unit" text (or a "reading…"
// placeholder); `done`/`total` drive the bar fill (total == 0 → no fill yet).
void draw_op_progress(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H,
                      std::string_view title, std::string_view count_line,
                      int done, int total, std::string_view hint);

} // namespace ui
