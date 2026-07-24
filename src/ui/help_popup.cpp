#include "ui/help_popup.h"

#include <algorithm>
#include <cmath>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/help_layout.h"

namespace ui {

void open_help(HelpPopupState& s)   { s.open = true; }
void close_help(HelpPopupState& s)  { s.open = false; s.scroll_line = 0; }
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

bool handle_help_key(HelpPopupState& s, SDL_Keycode key)
{
    if (!s.open) return false;
    switch (key) {
        case SDLK_ESCAPE:
        case SDLK_Q:        close_help(s); break;
        case SDLK_UP:       s.scroll_line = std::max(0, s.scroll_line - 1); break;
        case SDLK_DOWN:     s.scroll_line += 1; break;
        case SDLK_PAGEUP:   s.scroll_line = std::max(0, s.scroll_line - 8); break;
        case SDLK_PAGEDOWN: s.scroll_line += 8; break;
        default: break;
    }
    return true;
}

void handle_help_wheel(HelpPopupState& s, float wheel_y)
{
    if (!s.open) return;
    s.scroll_line = std::max(0, s.scroll_line - static_cast<int>(wheel_y));
}

namespace {

struct HelpPanelDims {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    int   columns = 0;
    int   longest = 0;
    float col_width = 0.0f;
};

HelpPanelDims compute_help_panel_dims(float W, float H, int total_lines,
                                       const std::vector<HelpColumn>& cols)
{
    using namespace gfx::theme;
    constexpr float LINE_H = 24.0f;
    constexpr float PAD = 24.0f;

    const float max_pw = W - 80.0f;
    const bool use_two_columns = (max_pw >= 720.0f) && (total_lines > 20);
    const float pw = use_two_columns ? std::min(720.0f, max_pw) : std::min(680.0f, max_pw);

    int longest = 0;
    for (const auto& c : cols) longest = std::max(longest, c.lines);

    const float chrome_h = 2.0f * (PAD + LINE_H + 8.0f);
    const float max_ph = H - 80.0f;
    const float wanted_ph = chrome_h + static_cast<float>(longest) * LINE_H;
    const float ph = std::min(max_ph, wanted_ph);

    const float px = (W - pw) / 2.0f;
    const float py = (H - ph) / 2.0f;
    const float col_width = (pw - 2.0f * PAD) / static_cast<float>(cols.size());

    return {px, py, pw, ph, static_cast<int>(cols.size()), longest, col_width};
}

}  // namespace

void draw_help_popup(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H,
                     const std::vector<HelpGroup>& groups, HelpPopupState& s)
{
    if (!s.open) return;
    using namespace gfx::theme;

    std::vector<HelpGroup> all_groups = {
        {.title = "Global", .entries = {{.key = "F1", .description = "Help"}, {.key = "F2", .description = "Settings"}}}
    };
    all_groups.insert(all_groups.end(), groups.begin(), groups.end());

    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});

    constexpr float LINE_H = 24.0f;
    constexpr float PAD    = 24.0f;

    const int total_lines = help_line_count(all_groups);
    const bool use_two_columns = (W - 80.0f >= 720.0f) && (total_lines > 20);
    const int target_columns = use_two_columns ? 2 : 1;
    const int lines_per_column = (total_lines + target_columns - 1) / target_columns;

    const auto cols = pack_help_columns(all_groups, lines_per_column, target_columns);
    const auto dims = compute_help_panel_dims(W, H, total_lines, cols);

    r.draw_round_rect({dims.x, dims.y, dims.w, dims.h}, RADIUS, SURFACE);
    r.draw_round_rect({dims.x, dims.y, dims.w, dims.h}, RADIUS, BORDER, /*filled*/ false);

    r.draw_text(font, dims.x + PAD, dims.y + PAD, "Keyboard Shortcuts", TEXT);
    r.draw_text(font, dims.x + PAD, dims.y + dims.h - PAD - LINE_H,
               "[Esc/Q] Close   [Up/Down] Scroll", TEXT_FAINT);

    const float chrome_h = 2.0f * (PAD + LINE_H + 8.0f);
    const float content_top = dims.y + PAD + LINE_H + 8.0f;
    const float raw_viewport = dims.h - chrome_h;
    const int visible_lines = help_visible_lines(raw_viewport, LINE_H);
    const float band_h = static_cast<float>(visible_lines) * LINE_H;
    const float content_bottom = content_top + band_h;

    s.scroll_line = clamp_help_line(s.scroll_line, dims.longest, visible_lines);

    const SDL_Rect content_clip{
        static_cast<int>(std::lround(dims.x)), static_cast<int>(std::lround(content_top)),
        static_cast<int>(std::lround(dims.w)), static_cast<int>(std::lround(band_h))};
    SDL_SetRenderClipRect(r.sdl(), &content_clip);
    const auto draw_column_content = [&](const HelpColumn& col) {
        const float col_x = dims.x + PAD + static_cast<float>(std::distance(cols.data(), &col)) * dims.col_width;
        float y = content_top - static_cast<float>(s.scroll_line) * LINE_H;
        for (size_t gidx : col.group_indices) {
            const auto& grp = all_groups[gidx];
            if (gidx > 0 && col.group_indices.front() != gidx) {
                y += LINE_H;
            }
            if (y >= content_top - LINE_H && y <= content_bottom)
                r.draw_text(font, col_x, y, grp.title, ACCENT);
            y += LINE_H;
            for (const auto& e : grp.entries) {
                if (y >= content_top - LINE_H && y <= content_bottom) {
                    const std::string line = "  [" + e.key + "]  " + e.description;
                    r.draw_text(font, col_x, y, line, TEXT_DIM);
                }
                y += LINE_H;
            }
        }
    };
    for (const auto& col : cols) {
        draw_column_content(col);
    }
    SDL_SetRenderClipRect(r.sdl(), nullptr);

    if (dims.longest > visible_lines) {
        const float affordance_y_top    = content_top - 4.0f;
        const float affordance_y_bottom = content_bottom + 4.0f;
        const float affordance_x = dims.x + dims.w - 12.0f;

        if (s.scroll_line > 0) {
            r.draw_text(font, affordance_x, affordance_y_top, "▲", TEXT_FAINT);
        }
        if (s.scroll_line < dims.longest - visible_lines) {
            r.draw_text(font, affordance_x, affordance_y_bottom, "▼", TEXT_FAINT);
        }
    }
}

} // namespace ui
