#include "ui/import_status_screen.h"

#include <algorithm>
#include <format>
#include <string>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/import_queue.h"
#include "ui/widgets.h"

namespace ui {

namespace {
constexpr float OX  = 40;    // left margin
constexpr float OY  = 150;   // list top (after optional lane-failure banner)
constexpr float PAD = 9;     // vertical padding inside a row
constexpr float BANNER_H = 48.0f;  // lane-failure banner height

// Format a single import task row's display text based on its state.
std::string format_row_text(const ImportTaskInfo& task)
{
    using enum ImportTaskState;
    switch (task.state) {
        case Running:
            return std::format("{} — {}/{}", task.display_name, task.done, task.total);
        case Queued:
            return std::format("#{} {} → {}",
                             task.id & 0xFF, task.display_name,
                             task.dest_gallery.empty() ? "root" : task.dest_gallery);
        case Done:
            return std::format("✓ {} imported, {} skipped", task.imported, task.skipped);
        case Failed:
            return task.error.empty() ? std::string("✗ Import failed")
                                      : std::format("✗ {}", task.error);
        case Cancelled:
            return std::string("− Cancelled");
    }
    return std::string();
}

// Helper to draw a running (progress bar) row
void draw_running_row(gfx::Renderer& r, gfx::FontAtlas& font, const ImportTaskInfo& task,
                      float y, float W, float row_h, float ph)
{
    using namespace gfx::theme;

    const float bar_y = y + (row_h - 4 - 12) * 0.5f;
    const float bar_w = W - 2 * OX - 24;
    const float progress = task.total > 0 ? static_cast<float>(task.done) / static_cast<float>(task.total) : 0.0f;
    const float ty = y + (row_h - 4 - ph) * 0.5f;

    // Background bar
    SDL_FRect bar_bg{OX + 14, bar_y, bar_w, 12};
    r.draw_round_rect(bar_bg, 2, OK);

    // Filled bar
    SDL_FRect bar_fill{OX + 14, bar_y, bar_w * progress, 12};
    r.draw_round_rect(bar_fill, 2, ACCENT);

    // Text: name and "done/total"
    const std::string prog_text = std::format("{}/{}", task.done, task.total);
    const float name_width = W - OX - 14 - 100 - 20;
    const std::string display = fit_text(font, task.display_name, name_width);
    r.draw_text(font, OX + 14, ty, display, TEXT);
    r.draw_text(font, W - OX - 14 - static_cast<float>(font.measure(prog_text)), ty, prog_text, TEXT_DIM);
}

// Helper to draw a queued or finished row
void draw_queued_finished_row(gfx::Renderer& r, gfx::FontAtlas& font, const ImportTaskInfo& task,
                              float y, float row_h, float ph, float W)
{
    using namespace gfx::theme;

    const float ty = y + (row_h - 4 - ph) * 0.5f;
    const std::string text = format_row_text(task);
    const std::string display = fit_text(font, text, W - 2 * OX - 28);

    gfx::Color text_color = TEXT;
    if (task.state == ImportTaskState::Failed) {
        text_color = DANGER;
    } else if (task.state == ImportTaskState::Done) {
        text_color = OK;
    }

    r.draw_text(font, OX + 14, ty, display, text_color);
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
    const float     ph      = font.pixel_height();

    // Draw selection glow and background
    if (sel_row) r.draw_selection_glow(row, RADIUS, ACCENT);
    r.draw_round_rect(row, RADIUS, sel_row ? SURFACE_HI : SURFACE);
    r.draw_round_rect(row, RADIUS, sel_row ? ACCENT : BORDER, /*filled*/ false);

    if (task.state == ImportTaskState::Running) {
        draw_running_row(r, font, task, y, lay.w, lay.row_h, ph);
    } else {
        draw_queued_finished_row(r, font, task, y, lay.row_h, ph, lay.w);
    }
}

} // namespace

ImportStatusScreen::ImportStatusScreen(gfx::Window& win, gfx::FontAtlas& font,
                                       ImportQueue& queue, Nav back)
    : win_(win), font_(font), queue_(queue), back_(std::move(back))
{
}

void ImportStatusScreen::move_selection(int delta)
{
    if (rows_.empty()) return;
    sel_ = std::clamp(sel_ + delta, 0, static_cast<int>(rows_.size()) - 1);
}

// Ctrl+Up/Down: reorder the selected QUEUED row (the queue rejects other states).
void ImportStatusScreen::reorder_selected(int delta)
{
    if (rows_.empty() || sel_ < 0 || sel_ >= static_cast<int>(rows_.size())) return;
    (void)queue_.reorder(rows_[sel_].id, delta);
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
        case SDLK_DELETE:
            if (!rows_.empty() && sel_ >= 0 && sel_ < static_cast<int>(rows_.size()))
                (void)queue_.cancel(rows_[sel_].id);
            break;
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
        // Clamp selection after refresh
        if (!rows_.empty()) {
            sel_ = std::clamp(sel_, 0, static_cast<int>(rows_.size()) - 1);
        } else {
            sel_ = 0;
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
    const float row_h = ph + 2 * PAD;
    const int  visible = std::max(1, static_cast<int>((bottom - list_top) / row_h));
    int        first   = 0;
    const auto count   = static_cast<int>(rows_.size());
    if (count > visible) first = std::clamp(sel_ - visible / 2, 0, count - visible);

    // Clamp scroll position
    const float content_h  = row_h * static_cast<float>(count);
    const float max_scroll = std::max(0.0f, content_h - (bottom - list_top));
    scroll_ = std::clamp(scroll_, 0.0f, max_scroll);

    // Render rows
    const RowLayout lay{.list_top = list_top, .bottom = bottom, .row_h = row_h,
                        .w = W, .first = first, .sel = sel_, .scroll = scroll_};
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
