#include "ui/import_status_screen.h"

#include <algorithm>
#include <format>
#include <string>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/import_queue.h"
#include "ui/import_status_row.h"
#include "ui/text_metrics.h"
#include "ui/widgets.h"

namespace ui {

namespace {
constexpr float OX  = 40;    // left margin
constexpr float OY  = 150;   // list top (after optional lane-failure banner)
constexpr float PAD = 9;     // vertical padding inside a row
constexpr float BANNER_H = 48.0f;  // lane-failure banner height

// Line 1 of every row: the route. Elided to the row's usable width so a long
// archive name never runs under the row border.
void draw_route_line(gfx::Renderer& r, gfx::FontAtlas& font, const ImportTaskInfo& task,
                     float y, float w)
{
    using namespace gfx::theme;
    const std::string route = fit_text(font, format_task_route(task), w - 2 * OX - 28);
    r.draw_text(font, OX + 14, y, route, TEXT);
}

// Line 2 for a Running task: the progress bar plus its done/total text. The bar
// owns this band alone — the Phase 56 fix for the bar being drawn over the text.
void draw_progress_line(gfx::Renderer& r, gfx::FontAtlas& font, const ImportTaskInfo& task,
                        float y, float w, float pitch)
{
    using namespace gfx::theme;
    constexpr float BAR_H = 12.0f;

    const std::string prog = format_task_status(task);
    const auto  prog_w  = static_cast<float>(font.measure(prog));
    const float bar_w   = w - 2 * OX - 28 - prog_w - 16.0f;
    const float bar_y   = y + (pitch - BAR_H) * 0.5f;
    const float progress = task.total > 0
        ? static_cast<float>(task.done) / static_cast<float>(task.total) : 0.0f;

    r.draw_round_rect({OX + 14, bar_y, std::max(0.0f, bar_w), BAR_H}, 2, OK);
    r.draw_round_rect({OX + 14, bar_y, std::max(0.0f, bar_w) * progress, BAR_H}, 2, ACCENT);
    r.draw_text(font, w - OX - 14 - prog_w, y, prog, TEXT_DIM);
}

// Line 2 for every other state: the outcome, coloured by state.
void draw_status_line(gfx::Renderer& r, gfx::FontAtlas& font, const ImportTaskInfo& task,
                      float y, float w)
{
    using namespace gfx::theme;
    gfx::Color c = TEXT_DIM;
    if (task.state == ImportTaskState::Failed)    c = DANGER;
    else if (task.state == ImportTaskState::Done) c = OK;

    r.draw_text(font, OX + 14, y, fit_text(font, format_task_status(task), w - 2 * OX - 28), c);
}

// Geometry shared by every row of one frame's list rendering; bundled so the
// per-row drawer needs the whole layout, not eleven loose scalars.
struct RowLayout {
    float list_top = 0;
    float bottom   = 0;
    float row_h    = 0;
    float w        = 0;
    int   first    = 0;
    int   sel      = 0;
    float scroll   = 0;
    float pitch    = 0;
};

// Draw a single row of the import list
void draw_row(gfx::Renderer& r, gfx::FontAtlas& font, const RowLayout& lay, int i,
              const ImportTaskInfo& task)
{
    using namespace gfx::theme;
    const float y = lay.list_top + static_cast<float>(i - lay.first) * lay.row_h - lay.scroll;
    if (y + lay.row_h < lay.list_top || y > lay.bottom) return;  // cull off-screen rows

    const SDL_FRect row{OX, y, lay.w - 2 * OX, lay.row_h - 4};
    const bool      sel_row = (i == lay.sel);

    // Draw selection glow and background
    if (sel_row) r.draw_selection_glow(row, RADIUS, ACCENT);
    r.draw_round_rect(row, RADIUS, sel_row ? SURFACE_HI : SURFACE);
    r.draw_round_rect(row, RADIUS, sel_row ? ACCENT : BORDER, /*filled*/ false);

    const float line1 = y + PAD;
    const float line2 = line1 + lay.pitch;

    draw_route_line(r, font, task, line1, lay.w);
    if (task.state == ImportTaskState::Running)
        draw_progress_line(r, font, task, line2, lay.w, lay.pitch);
    else
        draw_status_line(r, font, task, line2, lay.w);
}

} // namespace

ImportStatusScreen::ImportStatusScreen(gfx::Window& win, gfx::FontAtlas& font,
                                       ImportQueue& queue, Nav back)
    : win_(win), font_(font), queue_(queue), back_(std::move(back))
{
}

int ImportStatusScreen::sel_index() const
{
    if (rows_.empty()) return -1;
    const int i = index_of_task(rows_, sel_id_);
    return i >= 0 ? i : 0;   // selection vanished (cleared/cancelled): fall to the top row
}

void ImportStatusScreen::move_selection(int delta)
{
    const int cur = sel_index();
    if (cur < 0) return;
    const int next = std::clamp(cur + delta, 0, static_cast<int>(rows_.size()) - 1);
    sel_id_ = rows_[static_cast<size_t>(next)].id;
}

// Ctrl+Up/Down: reorder the selected QUEUED row (the queue rejects other
// states). sel_id_ is deliberately NOT touched — the selection is the task, not
// the slot, so focus follows the move and the chord can be repeated.
void ImportStatusScreen::reorder_selected(int delta)
{
    const int cur = sel_index();
    if (cur < 0) return;
    (void)queue_.reorder(rows_[static_cast<size_t>(cur)].id, delta);
}

void ImportStatusScreen::handle_key(const SDL_KeyboardEvent& key)
{
    const bool ctrl = (key.mod & SDL_KMOD_CTRL) != 0;
    switch (key.key) {
        case SDLK_UP:
            if (ctrl) reorder_selected(-1);
            else      move_selection(-1);
            break;
        case SDLK_DOWN:
            if (ctrl) reorder_selected(1);
            else      move_selection(1);
            break;
        case SDLK_DELETE: {
            const int sel = sel_index();
            if (sel >= 0) (void)queue_.cancel(rows_[static_cast<size_t>(sel)].id);
            break;
        }
        case SDLK_C:
            queue_.clear_finished();
            break;
        case SDLK_ESCAPE:
        case SDLK_Q:
            request(back_.kind, back_.path, back_.index);
            break;
        case SDLK_I:
            // Shift+I closes the screen again (toggle feel).
            if ((key.mod & SDL_KMOD_SHIFT) != 0) request(back_.kind, back_.path, back_.index);
            break;
        default: break;
    }
}

void ImportStatusScreen::handle_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_KEY_DOWN) {
        handle_key(e.key);
    } else if (e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.y != 0) {
        // Wheel moves the selection; render() keeps it visible.
        move_selection(e.wheel.y > 0 ? -1 : 1);
    }
}

void ImportStatusScreen::update(double dt)
{
    (void)dt;
    const auto snapshot = queue_.snapshot();
    const bool lane_failed = queue_.lane_failed();

    // Mark dirty if snapshot changed or lane failure status flipped
    if (!(snapshot == rows_)) {
        rows_ = snapshot;
        if (sel_id_ == 0 || index_of_task(rows_, sel_id_) < 0) {
            sel_id_ = rows_.empty() ? 0 : rows_.front().id;
        }
        mark_dirty();
    }

    if (lane_failed != last_lane_failed_) {
        last_lane_failed_ = lane_failed;
        mark_dirty();
    }
}

void ImportStatusScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto  W  = static_cast<float>(win_.width());
    const auto  H  = static_cast<float>(win_.height());
    const float ph = font_.pixel_height();

    // Header band: "Imports" + "[F1] Help"
    r.draw_text(font_, OX, 40, "Imports", TEXT_DIM);
    r.draw_text(font_, OX, 84, "[F1] Help", TEXT_FAINT);

    // Lane-failure banner (Phase 50)
    float list_top = OY;
    if (queue_.lane_failed()) {
        const SDL_FRect banner{0, 110, W, BANNER_H};
        r.draw_rect(banner, DANGER);  // full-width error banner
        const std::string error_msg = "Vault write failed — imports halted. Committed items are safe.";
        r.draw_text(font_, OX, 110 + (BANNER_H - ph) * 0.5f,
                   fit_text(font_, error_msg, W - 2 * OX), TEXT);
        list_top = 110 + BANNER_H;
    }

    // Empty state
    if (rows_.empty()) {
        r.draw_text(font_, OX, list_top, "No imports", TEXT_DIM);
        const std::string hint = "Use Shift+I from the gallery to enqueue imports";
        r.draw_text(font_, OX, list_top + ph + 24, fit_text(font_, hint, W - 2 * OX), TEXT_FAINT);
        return;
    }

    // Compute scroll geometry
    const float bottom = H - 24.0f;  // reserve footer band
    const float pitch = line_pitch(ph);
    const float row_h = import_row_height(ph, PAD);
    const int  visible = std::max(1, static_cast<int>((bottom - list_top) / row_h));
    int        first   = 0;
    const auto count   = static_cast<int>(rows_.size());
    const int  sel     = sel_index();
    if (count > visible) first = std::clamp(sel - visible / 2, 0, count - visible);

    // Clamp scroll position
    const float content_h  = row_h * static_cast<float>(count);
    const float max_scroll = std::max(0.0f, content_h - (bottom - list_top));
    scroll_ = std::clamp(scroll_, 0.0f, max_scroll);

    // Render rows
    const RowLayout lay{.list_top = list_top, .bottom = bottom, .row_h = row_h,
                        .w = W, .first = first, .sel = sel, .scroll = scroll_, .pitch = pitch};
    for (int i = first; i < first + visible && i < count; ++i) {
        draw_row(r, font_, lay, i, rows_[static_cast<size_t>(i)]);
    }
}

std::vector<HelpGroup> ImportStatusScreen::help_groups() const
{
    return {{"Imports", {
        {"Up/Down", "Move selection"},
        {"Del", "Cancel task"},
        {"Ctrl+Up/Down", "Reorder queued"},
        {"C", "Clear finished"},
        {"Esc/Q/Shift+I", "Back"},
    }}};
}

} // namespace ui
