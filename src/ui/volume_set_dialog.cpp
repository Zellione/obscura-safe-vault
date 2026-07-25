#include "ui/volume_set_dialog.h"

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <format>
#include <string>

namespace ui {
namespace {

// Long sets are common (7z happily emits 100+ parts); show the first few and
// elide the rest rather than growing the panel past the window.
constexpr size_t MAX_LISTED = 8;

} // namespace

void VolumeSetDialog::open(VolumeSetSummary summary)
{
    active_  = true;
    summary_ = std::move(summary);
}

void VolumeSetDialog::close() { active_ = false; }

VolumeSetDialog::Result VolumeSetDialog::handle_key(SDL_Keycode key)
{
    using enum Result;
    if (!active_) {
        return Pending;
    }
    switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_Y:
            // A gap is a refusal, not a warning to click past: an incomplete
            // set yields a corrupt gallery, not a partial one. Stay open so the
            // user can read WHICH volume is missing.
            if (!summary_.can_import) {
                return Pending;
            }
            close();
            return Confirmed;
        case SDLK_ESCAPE:
        case SDLK_N:
            close();
            return Cancelled;
        default:
            return Pending;
    }
}

void VolumeSetDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const
{
    if (!active_) {
        return;
    }
    using namespace gfx::theme;

    const size_t listed = std::min(summary_.volume_lines.size(), MAX_LISTED);
    const bool   elided = summary_.volume_lines.size() > listed;

    const float pw = 620;
    const float ph = 150.0F + (static_cast<float>(listed) * 24.0F) + (elided ? 24.0F : 0.0F) +
                     (summary_.warning.empty() ? 0.0F : 34.0F);
    const float px = (W - pw) / 2;
    const float py = (H - ph) / 2;

    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});
    r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
    // The border says at a glance whether this is actionable at all.
    r.draw_round_rect({px, py, pw, ph}, RADIUS, summary_.can_import ? ACCENT : DANGER,
                      /*filled*/ false);

    r.draw_text(font, px + 24, py + 24, fit_text(font, summary_.heading, pw - 48), TEXT);

    float y = py + 62;
    for (size_t i = 0; i < listed; ++i) {
        r.draw_text(font, px + 40, y, fit_text(font, summary_.volume_lines[i], pw - 80), TEXT_DIM);
        y += 24;
    }
    if (elided) {
        r.draw_text(font, px + 40, y,
                    std::format("... and {} more", summary_.volume_lines.size() - listed),
                    TEXT_DIM);
        y += 24;
    }

    if (!summary_.warning.empty()) {
        r.draw_text(font, px + 24, y + 6, fit_text(font, summary_.warning, pw - 48), DANGER);
    }

    // Only offer the key that actually does something.
    const std::string keys =
        summary_.can_import ? "Enter - import all volumes     Esc - cancel" : "Esc - cancel";
    r.draw_text(font, px + 24, py + ph - 34, keys, TEXT_DIM);
}

} // namespace ui
