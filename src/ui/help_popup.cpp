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

    // Phase 51 fix: Decide whether to use two columns upfront, then size the popup
    // accordingly. This avoids a circular dependency: column count depends on width,
    // and width should depend on the column decision (not vice versa).
    //
    // Strategy:
    // 1. Measure total content (lines).
    // 2. Decide on column count based on window width and content height.
    // 3. Set panel width based on that decision.
    // 4. Pack columns with a BALANCED per-column budget, not the viewport height.
    // 5. Size the popup from the longest column, not total lines.
    const int total_lines = help_line_count(all_groups);
    const float max_pw = W - 80.0f;

    // Two columns are warranted when:
    // - The window is wide enough to support two 340px columns comfortably.
    // - The content is tall enough to benefit from the split.
    const bool use_two_columns = (max_pw >= 720.0f) && (total_lines > 20);
    const int target_columns = use_two_columns ? 2 : 1;
    const float pw = use_two_columns ? std::min(720.0f, max_pw) : std::min(680.0f, max_pw);

    // Calculate the per-column budget (balanced split). When two columns are used,
    // each column should hold roughly half the content, so columns are equally tall
    // rather than one overflow-laden and one mostly empty.
    const int lines_per_column = (total_lines + target_columns - 1) / target_columns;

    // Pack columns with the balanced budget, not the full viewport height.
    const auto cols = pack_help_columns(all_groups, lines_per_column, target_columns);

    // Find the longest column to determine popup height.
    int longest = 0;
    for (const auto& c : cols) longest = std::max(longest, c.lines);

    // Size the popup from the longest column, not total lines. This allows the
    // two-column layout to be approximately half as tall as a single-column layout
    // with the same content.
    const float chrome_h = 2.0f * (PAD + LINE_H + 8.0f);   // header + footer bands
    const float max_ph = H - 80.0f;
    const float wanted_ph = chrome_h + static_cast<float>(longest) * LINE_H;
    const float ph = std::min(max_ph, wanted_ph);

    const float px = (W - pw) / 2.0f;
    const float py = (H - ph) / 2.0f;
    r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
    r.draw_round_rect({px, py, pw, ph}, RADIUS, BORDER, /*filled*/ false);

    r.draw_text(font, px + PAD, py + PAD, "Keyboard Shortcuts", TEXT);
    r.draw_text(font, px + PAD, py + ph - PAD - LINE_H,
               "[Esc/Q] Close   [Up/Down] Scroll", TEXT_FAINT);

    const float content_top = py + PAD + LINE_H + 8.0f;
    const float raw_viewport = ph - chrome_h;
    const int visible_lines = help_visible_lines(raw_viewport, LINE_H);
    const float band_h = static_cast<float>(visible_lines) * LINE_H;
    const float content_bottom = content_top + band_h;

    // Clamp scroll to the actual content height (longest column).
    s.scroll_line = clamp_help_line(s.scroll_line, longest, visible_lines);

    // Clip to the content band using lround for exact pixel alignment
    const SDL_Rect content_clip{
        static_cast<int>(std::lround(px)), static_cast<int>(std::lround(content_top)),
        static_cast<int>(std::lround(pw)), static_cast<int>(std::lround(band_h))};
    SDL_SetRenderClipRect(r.sdl(), &content_clip);

    // Draw each column
    const float col_width = (pw - 2.0f * PAD) / static_cast<float>(cols.size());
    for (size_t col_idx = 0; col_idx < cols.size(); ++col_idx) {
        const auto& col = cols[col_idx];
        const float col_x = px + PAD + static_cast<float>(col_idx) * col_width;

        float y = content_top - static_cast<float>(s.scroll_line) * LINE_H;
        for (size_t gidx : col.group_indices) {
            const auto& grp = all_groups[gidx];

            // Add spacer before group if not first in column
            if (gidx > 0 && col.group_indices.front() != gidx) {
                y += LINE_H;
            }

            // Draw group title
            if (y >= content_top - LINE_H && y <= content_bottom)
                r.draw_text(font, col_x, y, grp.title, ACCENT);
            y += LINE_H;

            // Draw entries
            for (const auto& e : grp.entries) {
                if (y >= content_top - LINE_H && y <= content_bottom) {
                    const std::string line = "  [" + e.key + "]  " + e.description;
                    r.draw_text(font, col_x, y, line, TEXT_DIM);
                }
                y += LINE_H;
            }
        }
    }
    SDL_SetRenderClipRect(r.sdl(), nullptr);

    // Draw scroll affordance when content exceeds visible lines
    if (longest > visible_lines) {
        const float affordance_y_top    = content_top - 4.0f;
        const float affordance_y_bottom = content_bottom + 4.0f;
        const float affordance_x = px + pw - 12.0f;

        if (s.scroll_line > 0) {
            r.draw_text(font, affordance_x, affordance_y_top, "▲", TEXT_FAINT);
        }
        if (s.scroll_line < longest - visible_lines) {
            r.draw_text(font, affordance_x, affordance_y_bottom, "▼", TEXT_FAINT);
        }
    }
}

} // namespace ui
