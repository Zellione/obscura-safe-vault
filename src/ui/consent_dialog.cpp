#include "ui/consent_dialog.h"

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"

namespace ui {

void ConsentDialog::open(std::string detail)
{
    active_ = true;
    detail_ = std::move(detail);
}

void ConsentDialog::close() { active_ = false; }

ConsentDialog::Result ConsentDialog::handle_key(SDL_Keycode key)
{
    using enum Result;
    if (!active_) return Pending;
    switch (key) {
        case SDLK_Y:                 // confirming requires a deliberate, distinct key
            close();
            return Confirmed;
        case SDLK_RETURN:            // Enter triggers the default action: Cancel
        case SDLK_KP_ENTER:
        case SDLK_ESCAPE:
        case SDLK_N:
            close();
            return Cancelled;
        default:
            return Pending;
    }
}

void ConsentDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const
{
    if (!active_) return;
    using namespace gfx::theme;

    // Veil the whole window so the modal clearly owns input focus. A solid dark
    // fill keeps this deterministic regardless of the current render blend mode.
    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});

    const float pw = 560;
    const float ph = 230;
    const float px = (W - pw) / 2;
    const float py = (H - ph) / 2;
    r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
    r.draw_round_rect({px, py, pw, ph}, RADIUS, DANGER, /*filled*/ false);

    auto centered = [&](const std::string& s, float y, gfx::Color c) {
        const float tw = static_cast<float>(font.measure(s));
        r.draw_text(font, px + (pw - tw) / 2, y, s, c);
    };

    centered(detail_, py + 28, TEXT);
    centered("Exported files are written DECRYPTED to disk,", py + 84, DANGER);
    centered("outside the vault's protection.", py + 114, DANGER);

    // Cancel is the default action (Esc / Enter); confirming needs a deliberate Y.
    centered("[Esc/Enter] Cancel        [Y] Export anyway", py + ph - 50, TEXT_DIM);
}

} // namespace ui
