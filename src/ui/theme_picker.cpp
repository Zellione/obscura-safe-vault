#include "ui/theme_picker.h"

#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"

namespace ui {

namespace {
int clamp_index(int sel) noexcept
{
    if (sel < 0) return 0;
    return sel > gfx::THEME_COUNT - 1 ? gfx::THEME_COUNT - 1 : sel;
}
} // namespace

void ThemePicker::open()
{
    sel_    = static_cast<int>(std::to_underlying(gfx::active_theme_id()));
    active_ = true;
}

void ThemePicker::select(int sel)
{
    sel_ = clamp_index(sel);
    const auto id = static_cast<gfx::ThemeId>(sel_);
    gfx::set_theme(id);     // live apply…
    (void)pref_.save(id);   // …and persist immediately (best-effort)
}

bool ThemePicker::handle_event(const SDL_Event& e)
{
    if (!active_) return false;
    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // modal swallows other events

    switch (e.key.key) {
        case SDLK_UP:       select(sel_ - 1); break;
        case SDLK_DOWN:     select(sel_ + 1); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_ESCAPE:   close();          break;
        default: break;
    }
    return true;
}

void ThemePicker::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const
{
    if (!active_) return;
    using namespace gfx::theme;

    const float mw = W * 0.42f;
    const float mh = H * 0.5f;
    const float mx = (W - mw) / 2;
    const float my = (H - mh) / 2;
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    const float ix = mx + 20;
    r.draw_text(font, ix, my + 20, "Theme", TEXT);
    r.draw_text(font, ix, my + 56, "[Up/Down] preview  [Enter/Esc] done", TEXT_FAINT);

    for (int i = 0; i < gfx::THEME_COUNT; ++i) {
        const float ry = my + 96 + static_cast<float>(i) * 34.0f;
        const bool  on = (i == sel_);
        if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
        const auto id = static_cast<gfx::ThemeId>(i);
        // A swatch of the preset's accent so each row previews its palette.
        r.draw_round_rect({ix + 8, ry + 7, 16, 16}, RADIUS_SMALL,
                          gfx::theme_preset(id).accent);
        r.draw_text(font, ix + 34, ry + 4, gfx::theme_name(id), on ? TEXT : TEXT_DIM);
    }
}

} // namespace ui
