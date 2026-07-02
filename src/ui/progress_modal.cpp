#include "ui/progress_modal.h"

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"

namespace ui {

void draw_op_progress(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H,
                      const OpProgressModal& m)
{
    using namespace gfx::theme;
    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});   // full veil

    const float mw = 520;
    const float mh = 150;
    const float mx = (W - mw) / 2;
    const float my = (H - mh) / 2;
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    r.draw_text(font, mx + 20, my + 20, m.title, TEXT);
    r.draw_text(font, mx + 20, my + 56, m.count_line, TEXT_DIM);

    // Progress bar: outline plus a fill proportional to done/total.
    const SDL_FRect bar{mx + 20, my + 90, mw - 40, 14};
    r.draw_round_rect(bar, 4, ACCENT, /*filled*/ false);
    if (m.total > 0 && m.done > 0) {
        const float frac = static_cast<float>(m.done) / static_cast<float>(m.total);
        r.draw_round_rect({bar.x, bar.y, bar.w * frac, bar.h}, 4, ACCENT);
    }

    r.draw_text(font, mx + 20, my + mh - 28, m.hint, TEXT_FAINT);
}

} // namespace ui
