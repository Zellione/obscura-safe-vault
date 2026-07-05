#include "ui/quick_switch.h"

#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "platform/vault_registry.h"
#include "ui/widgets.h"

namespace ui {

namespace {
// Clamp a list selection to [0, count-1] (0 when the list is empty).
int clamp_index(int sel, int count) noexcept
{
    if (count <= 0 || sel < 0) return 0;
    return sel > count - 1 ? count - 1 : sel;
}
} // namespace

QuickSwitch::QuickSwitch(platform::VaultRegistry& registry, std::string active_path)
    : registry_(registry), active_path_(std::move(active_path)) {}

void QuickSwitch::open()
{
    vaults_ = registry_.list();
    sel_    = 0;
    active_ = true;
}

void QuickSwitch::choose()
{
    if (sel_ < 0 || sel_ >= static_cast<int>(vaults_.size())) { close(); return; }
    const auto path = vaults_[static_cast<size_t>(sel_)].string();
    close();
    if (path == active_path_) return;   // already active — no-op
    chosen_     = path;
    has_choice_ = true;
}

bool QuickSwitch::handle_event(const SDL_Event& e)
{
    if (!active_) return false;
    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // modal swallows other events

    const auto n = static_cast<int>(vaults_.size());
    switch (e.key.key) {
        case SDLK_ESCAPE:                     close();                         break;
        case SDLK_UP:                         sel_ = clamp_index(sel_ - 1, n); break;
        case SDLK_DOWN:                       sel_ = clamp_index(sel_ + 1, n); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:                   choose();                        break;
        default: break;
    }
    return true;
}

bool QuickSwitch::consume_choice(std::string& path_out)
{
    if (!has_choice_) return false;
    path_out    = std::move(chosen_);
    has_choice_ = false;
    return true;
}

void QuickSwitch::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const
{
    if (!active_) return;
    using namespace gfx::theme;

    const float mw = W * 0.5f;
    const float mh = H * 0.6f;
    const float mx = (W - mw) / 2;
    const float my = (H - mh) / 2;
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    const float ix = mx + 20;
    r.draw_text(font, ix, my + 20, "Switch vault", TEXT);
    r.draw_text(font, ix, my + 56, "[Up/Down] choose  [Enter] open  [Esc] cancel", TEXT_FAINT);

    for (size_t i = 0; i < vaults_.size(); ++i) {
        const float ry = my + 96 + static_cast<float>(i) * 34.0f;
        const bool  on = (static_cast<int>(i) == sel_);
        if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
        std::string label = vaults_[i].filename().string();
        if (vaults_[i].string() == active_path_) label += "  (current)";
        r.draw_text(font, ix + 8, ry + 4, fit_text(font, label, mw - 56),
                    on ? TEXT : TEXT_DIM);
    }
}

} // namespace ui
