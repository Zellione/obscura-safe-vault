#pragma once

#include <string_view>

namespace gfx { class Renderer; class FontAtlas; }

namespace ui {

// One background-op progress modal's content. `count_line` is the caller-formatted
// "done / total unit" text (or a "reading…" placeholder); `done`/`total` drive the
// bar fill (total == 0 → no fill yet).
struct OpProgressModal {
    std::string_view title;
    std::string_view count_line;
    std::string_view hint = "Esc to cancel";
    int              done  = 0;
    int              total = 0;
};

// Draw a centred, full-window-veiling progress modal for a background operation
// (import / export / delete / move / copy) — Phase 25. Shared by every screen that
// hosts a worker-thread job so the "N / M" bar + cancel hint look identical.
void draw_op_progress(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H,
                      const OpProgressModal& m);

} // namespace ui
