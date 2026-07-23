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
constexpr float OY  = 150;   // list top
constexpr float PAD = 9;     // vertical padding inside a row

// List geometry shared by render() so layout is consistent.
struct ListGeom {
    float row_h;
    int   visible;
    int   first;
};

ListGeom compute_geom(float ph, float win_h, int count, int sel)
{
    const float row_h   = ph + 2 * PAD;
    const float bottom  = win_h - 24.0f;
    const int   visible = std::max(1, static_cast<int>((bottom - OY) / row_h));
    int         first   = 0;
    if (count > visible) first = std::clamp(sel - visible / 2, 0, count - visible);
    return {row_h, visible, first};
}

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

} // namespace

ImportStatusScreen::ImportStatusScreen(gfx::Window& win, gfx::FontAtlas& font,
                                       ImportQueue& queue, Nav back)
    : win_(win), font_(font), queue_(queue), back_(std::move(back))
{
}

void ImportStatusScreen::handle_event(const SDL_Event& e)
{
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (e.key.key) {
                case SDLK_UP:
                    if ((e.key.mod & SDL_KMOD_CTRL) != 0) {
                        // Ctrl+Up to reorder upward
                        if (!rows_.empty() && sel_ >= 0 && sel_ < static_cast<int>(rows_.size())) {
                            (void)queue_.reorder(rows_[sel_].id, -1);
                        }
                    } else {
                        // Regular Up to move selection
                        if (!rows_.empty()) sel_ = std::max(0, sel_ - 1);
                    }
                    break;
                case SDLK_DOWN:
                    if ((e.key.mod & SDL_KMOD_CTRL) != 0) {
                        // Ctrl+Down to reorder downward
                        if (!rows_.empty() && sel_ >= 0 && sel_ < static_cast<int>(rows_.size())) {
                            (void)queue_.reorder(rows_[sel_].id, 1);
                        }
                    } else {
                        // Regular Down to move selection
                        if (!rows_.empty()) sel_ = std::min(static_cast<int>(rows_.size()) - 1, sel_ + 1);
                    }
                    break;
                case SDLK_DELETE:
                    if (!rows_.empty() && sel_ >= 0 && sel_ < static_cast<int>(rows_.size())) {
                        (void)queue_.cancel(rows_[sel_].id);
                    }
                    break;
                case SDLK_C:
                    queue_.clear_finished();
                    break;
                case SDLK_ESCAPE:
                case SDLK_Q:
                    request(back_.kind, back_.path, back_.index);
                    break;
                case SDLK_I:
                    // Shift+I to close the import status screen (toggle feel)
                    if ((e.key.mod & SDL_KMOD_SHIFT) != 0) {
                        request(back_.kind, back_.path, back_.index);
                    }
                    break;
                default: break;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (e.wheel.y > 0) {
                if (!rows_.empty()) sel_ = std::max(0, sel_ - 1);
            } else if (e.wheel.y < 0) {
                if (!rows_.empty()) sel_ = std::min(static_cast<int>(rows_.size()) - 1, sel_ + 1);
            }
            break;
        default: break;
    }
}

void ImportStatusScreen::update(double dt)
{
    (void)dt;
    const auto snapshot = queue_.snapshot();
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
}

void ImportStatusScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());
    const float ph = font_.pixel_height();

    // Header band: "Imports" + "[F1] Help"
    r.draw_text(font_, OX, 40, "Imports", TEXT_DIM);
    r.draw_text(font_, OX, 84, "[F1] Help", TEXT_FAINT);

    // Empty state
    if (rows_.empty()) {
        r.draw_text(font_, OX, OY, "No imports", TEXT_DIM);
        const std::string hint = "Use Shift+I from the gallery to enqueue imports";
        r.draw_text(font_, OX, OY + ph + 24, fit_text(font_, hint, W - 2 * OX), TEXT_FAINT);
        return;
    }

    // Render the scrollable list
    const auto g = compute_geom(ph, H, static_cast<int>(rows_.size()), sel_);

    for (int i = g.first; i < g.first + g.visible && i < static_cast<int>(rows_.size()); ++i) {
        const float    y    = OY + static_cast<float>(i - g.first) * g.row_h;
        const SDL_FRect row{OX, y, W - 2 * OX, g.row_h - 4};
        const bool     sel  = (i == sel_);

        // Draw selection glow and background
        if (sel) r.draw_selection_glow(row, RADIUS, ACCENT);
        r.draw_round_rect(row, RADIUS, sel ? SURFACE_HI : SURFACE);
        r.draw_round_rect(row, RADIUS, sel ? ACCENT : BORDER, /*filled*/ false);

        // Row content based on state
        const ImportTaskInfo& task = rows_[i];
        const float ty = y + (g.row_h - 4 - ph) * 0.5f;

        if (task.state == ImportTaskState::Running) {
            // Running: show progress bar
            const float bar_y = y + (g.row_h - 4 - 12) * 0.5f;
            const float bar_w = W - 2 * OX - 24;
            const float progress = task.total > 0 ? static_cast<float>(task.done) / static_cast<float>(task.total) : 0.0f;

            // Background bar
            SDL_FRect bar_bg{OX + 14, bar_y, bar_w, 12};
            r.draw_round_rect(bar_bg, 2, OK);

            // Filled bar
            SDL_FRect bar_fill{OX + 14, bar_y, bar_w * progress, 12};
            r.draw_round_rect(bar_fill, 2, ACCENT);

            // Text: name and "done/total"
            const std::string prog_text = std::format("{}/{}", task.done, task.total);
            const float name_width = W - OX - 14 - 100 - 20;
            const std::string display = fit_text(font_, task.display_name, name_width);
            r.draw_text(font_, OX + 14, ty, display, TEXT);
            r.draw_text(font_, W - OX - 14 - static_cast<float>(font_.measure(prog_text)), ty, prog_text, TEXT_DIM);
        } else {
            // Queued/Finished: just text
            const std::string text = format_row_text(task);
            const std::string display = fit_text(font_, text, W - 2 * OX - 28);

            gfx::Color text_color = TEXT;
            if (task.state == ImportTaskState::Failed) {
                text_color = DANGER;
            } else if (task.state == ImportTaskState::Done) {
                text_color = OK;
            }

            r.draw_text(font_, OX + 14, ty, display, text_color);
        }
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
