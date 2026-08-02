#include "ui/duplicates_screen.h"

#include <algorithm>
#include <format>
#include <string>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/text_metrics.h"
#include "ui/widgets.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float OX  = 40;    // left margin
constexpr float OY  = 150;   // list top
constexpr float PAD = 9;     // vertical padding inside a row
}

DuplicatesScreen::DuplicatesScreen(gfx::Window& win, gfx::FontAtlas& font,
                                   vault::Vault& vault, gfx::TextureCache& cache, Nav back)
    : win_(win), font_(font), vault_(vault), cache_(cache), back_(std::move(back))
{
}

void DuplicatesScreen::handle_key(const SDL_KeyboardEvent& key)
{
    switch (state_) {
        case State::Choose:
            switch (key.key) {
                case SDLK_UP:
                    choose_sel_ = std::max(0, choose_sel_ - 1);
                    mark_dirty();
                    break;
                case SDLK_DOWN:
                    choose_sel_ = std::min(1, choose_sel_ + 1);
                    mark_dirty();
                    break;
                case SDLK_RETURN:
                case SDLK_SPACE:
                    start_scan(choose_sel_ == 1);
                    break;
                case SDLK_ESCAPE:
                    leave();
                    break;
                default:
                    break;
            }
            break;

        case State::Scanning:
            if (key.key == SDLK_ESCAPE) {
                job_.cancel();
                mark_dirty();
            }
            break;

        case State::Review:
            // Task 7/8 stubs
            if (key.key == SDLK_ESCAPE) leave();
            break;

        case State::Done:
            if (key.key == SDLK_ESCAPE) leave();
            break;

        default:
            break;
    }
}

void DuplicatesScreen::handle_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_KEY_DOWN) {
        handle_key(e.key);
    }
}

void DuplicatesScreen::start_scan(bool perceptual)
{
    auto items = collect_scan_items(vault_);
    job_.start(vault_, std::move(items), perceptual);
    state_ = State::Scanning;
    mark_dirty();
}

void DuplicatesScreen::update(double dt)
{
    (void)dt;

    if (state_ != State::Scanning) return;

    if (!job_.active()) {
        // Scan finished
        if (auto outcome = job_.take_outcome()) {
            if (outcome->cancelled) {
                leave();
            } else {
                review_ = DupReview(std::move(outcome->groups));
                skipped_ = outcome->skipped;
                if (outcome->groups.empty() && outcome->skipped == 0) {
                    done_summary_ = "No duplicates found";
                    state_ = State::Done;
                } else {
                    state_ = State::Review;
                }
                mark_dirty();
            }
        }
    } else {
        // Still scanning - mark dirty if progress changed
        mark_dirty();
    }
}

void DuplicatesScreen::leave()
{
    request(back_.kind, back_.path, back_.index);
}

void DuplicatesScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto  W  = static_cast<float>(win_.width());
    const auto  H  = static_cast<float>(win_.height());
    const float ph = font_.pixel_height();

    // Header band: "Duplicates" + "[F1] Help"
    r.draw_text(font_, OX, 40, "Duplicates", TEXT_DIM);
    r.draw_text(font_, OX, 84, "[F1] Help", TEXT_FAINT);

    if (state_ == State::Choose) {
        // Two selectable rows: "Exact duplicates" / "Exact + visually similar"
        constexpr float ROW_H = 60.0f;
        constexpr float RADIUS = 10.0f;

        const std::string row1_text = "Exact duplicates";
        const std::string row2_text = "Exact + visually similar";

        for (int i = 0; i < 2; ++i) {
            const float y = OY + static_cast<float>(i) * (ROW_H + 12.0f);
            const bool sel = (i == choose_sel_);
            const std::string& text = (i == 0) ? row1_text : row2_text;

            const SDL_FRect row{OX, y, W - 2 * OX, ROW_H};

            // Draw selection glow and background
            if (sel) r.draw_selection_glow(row, RADIUS, ACCENT);
            r.draw_round_rect(row, RADIUS, sel ? SURFACE_HI : SURFACE);
            r.draw_round_rect(row, RADIUS, sel ? ACCENT : BORDER, /*filled*/ false);

            // Text
            r.draw_text(font_, OX + 20, y + (ROW_H - ph) * 0.5f, text,
                       sel ? TEXT : TEXT_DIM);
        }

        // Hint at bottom
        const std::string hint = "Up/Down to select, Enter to start, Esc to back";
        r.draw_text(font_, OX, H - 40, hint, TEXT_FAINT);
    } else if (state_ == State::Scanning) {
        // Progress bar like import_status_screen
        constexpr float BAR_H = 12.0f;
        constexpr float BAR_Y = OY + 20.0f;
        const float BAR_W = W - 2 * OX - 40;

        const size_t done = job_.progress_done();
        const size_t total = job_.progress_total();
        const float progress = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;

        const std::string current = job_.current_name();
        const std::string status = total > 0
            ? std::format("{} / {} items", done, total)
            : "Initializing...";

        // Background bar
        r.draw_round_rect({OX + 20, BAR_Y, BAR_W, BAR_H}, 2, OK);
        // Progress bar
        r.draw_round_rect({OX + 20, BAR_Y, BAR_W * progress, BAR_H}, 2, ACCENT);

        // Status text
        r.draw_text(font_, OX + 20, BAR_Y + BAR_H + 16, "Scanning...", TEXT_DIM);
        r.draw_text(font_, OX + 20, BAR_Y + BAR_H + 40, fit_text(font_, current, BAR_W), TEXT);
        r.draw_text(font_, OX + 20, BAR_Y + BAR_H + 64, status, TEXT_FAINT);

        const std::string cancel_hint = "Esc to cancel";
        r.draw_text(font_, OX, H - 40, cancel_hint, TEXT_FAINT);
    } else if (state_ == State::Review) {
        // Task 7/8 stubs: placeholder
        const std::string placeholder = std::format("Review: {} groups, {} skipped",
                                                    review_.groups().size(), skipped_);
        r.draw_text(font_, OX, OY, placeholder, TEXT_DIM);
        r.draw_text(font_, OX, H - 40, "Esc to back", TEXT_FAINT);
    } else if (state_ == State::Done) {
        // Task 7/8 stubs: done placeholder
        r.draw_text(font_, OX, OY, done_summary_, TEXT_DIM);
        r.draw_text(font_, OX, H - 40, "Esc to back", TEXT_FAINT);
    }
}

std::vector<HelpGroup> DuplicatesScreen::help_groups() const
{
    return {
        {"Scan", {
            {"Up/Down", "Select scan mode"},
            {"Enter", "Start scan"},
            {"Esc", "Cancel"},
        }},
    };
}

void DuplicatesScreen::on_vault_changed()
{
    stale_ = true;
    mark_dirty();
}

} // namespace ui
