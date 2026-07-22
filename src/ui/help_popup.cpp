#include "ui/help_popup.h"

#include <algorithm>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"

namespace ui {

void open_help(HelpPopupState& s)   { s.open = true; }
void close_help(HelpPopupState& s)  { s.open = false; s.scroll = 0.0f; }
void toggle_help(HelpPopupState& s) { s.open ? close_help(s) : open_help(s); }

int help_line_count(const std::vector<HelpGroup>& groups)
{
    int lines = 0;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (i > 0) ++lines;                                  // spacer
        lines += 1;                                           // group title
        lines += static_cast<int>(groups[i].entries.size());  // entries
    }
    return lines;
}

float clamp_help_scroll(float scroll, float content_h, float viewport_h)
{
    const float max_scroll = std::max(0.0f, content_h - viewport_h);
    return std::clamp(scroll, 0.0f, max_scroll);
}

namespace {
constexpr float HELP_LINE_STEP = 24.0f;
constexpr float HELP_PAGE_STEP = HELP_LINE_STEP * 8.0f;
}

bool handle_help_key(HelpPopupState& s, SDL_Keycode key)
{
    if (!s.open) return false;
    switch (key) {
        case SDLK_ESCAPE:
        case SDLK_Q:        close_help(s); break;
        case SDLK_UP:       s.scroll = std::max(0.0f, s.scroll - HELP_LINE_STEP); break;
        case SDLK_DOWN:     s.scroll += HELP_LINE_STEP; break;
        case SDLK_PAGEUP:   s.scroll = std::max(0.0f, s.scroll - HELP_PAGE_STEP); break;
        case SDLK_PAGEDOWN: s.scroll += HELP_PAGE_STEP; break;
        default: break;
    }
    return true;
}

void handle_help_wheel(HelpPopupState& s, float wheel_y)
{
    if (!s.open) return;
    s.scroll = std::max(0.0f, s.scroll - wheel_y * HELP_LINE_STEP);
}

void draw_help_popup(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H,
                     const std::vector<HelpGroup>& groups, HelpPopupState& s)
{
    if (!s.open) return;
    using namespace gfx::theme;

    // Prepend the global help group
    std::vector<HelpGroup> all_groups = {
        {.title = "Global", .entries = {{.key = "F1", .description = "Help"}, {.key = "F2", .description = "Settings"}}}
    };
    all_groups.insert(all_groups.end(), groups.begin(), groups.end());

    // Veil the whole window, matching consent_dialog's modal style.
    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});

    constexpr float LINE_H = 24.0f;
    constexpr float PAD    = 24.0f;
    const float pw = std::min(680.0f, W - 80.0f);
    const float ph = std::min(H - 80.0f, 520.0f);
    const float px = (W - pw) / 2.0f;
    const float py = (H - ph) / 2.0f;
    r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
    r.draw_round_rect({px, py, pw, ph}, RADIUS, BORDER, /*filled*/ false);

    r.draw_text(font, px + PAD, py + PAD, "Keyboard Shortcuts", TEXT);
    r.draw_text(font, px + PAD, py + ph - PAD - LINE_H,
               "[Esc/Q] Close   [Up/Down] Scroll", TEXT_FAINT);

    const float content_top    = py + PAD + LINE_H + 8.0f;
    const float content_bottom = py + ph - PAD - LINE_H - 8.0f;
    const float content_h      = static_cast<float>(help_line_count(all_groups)) * LINE_H;
    s.scroll = clamp_help_scroll(s.scroll, content_h, content_bottom - content_top);

    // Clip to the content band so a line scrolled to the edge is cut cleanly
    // instead of its glyphs bleeding down into the footer hint below (the
    // per-line y checks alone gate on a line's *top*, so a partially-visible
    // last line would otherwise spill past content_bottom).
    const SDL_Rect content_clip{
        static_cast<int>(px), static_cast<int>(content_top),
        static_cast<int>(pw), static_cast<int>(content_bottom - content_top)};
    SDL_SetRenderClipRect(r.sdl(), &content_clip);

    float y = content_top - s.scroll;
    for (size_t gi = 0; gi < all_groups.size(); ++gi) {
        if (gi > 0) y += LINE_H;
        if (y >= content_top - LINE_H && y <= content_bottom)
            r.draw_text(font, px + PAD, y, all_groups[gi].title, ACCENT);
        y += LINE_H;
        for (const auto& e : all_groups[gi].entries) {
            if (y >= content_top - LINE_H && y <= content_bottom) {
                const std::string line = "  [" + e.key + "]  " + e.description;
                r.draw_text(font, px + PAD, y, line, TEXT_DIM);
            }
            y += LINE_H;
        }
    }
    SDL_SetRenderClipRect(r.sdl(), nullptr);
}

} // namespace ui
