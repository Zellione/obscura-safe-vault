#include "ui/help_popup.h"

#include <algorithm>

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

} // namespace ui
