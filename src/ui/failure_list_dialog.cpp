#include "ui/failure_list_dialog.h"

#include <format>
#include <algorithm>

#include "gfx/renderer.h"
#include "gfx/theme.h"
#include "ui/widgets.h"
#include "ui/text_metrics.h"

namespace ui {

std::string transfer_failure_reason(vault::VaultResult code,
                                    vault::TransferFailure::Stage stage)
{
    using vault::VaultResult;
    using Stage = vault::TransferFailure::Stage;

    // Phase 67 spec table: ASCII-only reasons (font atlas bakes 32–126).
    switch (code) {
        case VaultResult::InvalidArg:
            return "name not allowed in destination";
        case VaultResult::AlreadyExists:
            return "name already exists at destination";
        case VaultResult::AuthFailed:
            return "source data corrupt or unreadable";
        case VaultResult::IoError:
            if (stage == Stage::Read)
                return "could not read source (possibly out of memory)";
            else
                return "destination write failed (disk full?)";
        case VaultResult::NotFound:
            return "item not found in source";
        case VaultResult::Locked:
            return "vault locked";
        default:
            return "failed";
    }
}

std::string transfer_failure_line(const vault::TransferFailure& f)
{
    return f.path + " - " + transfer_failure_reason(f.code, f.stage);
}

void FailureListDialog::open(std::vector<vault::TransferFailure> failures, int failed_total)
{
    lines_.clear();
    for (const auto& f : failures) {
        lines_.push_back(transfer_failure_line(f));
    }

    // If there are more failures than we're showing, append a truncation notice.
    if (failed_total > static_cast<int>(failures.size())) {
        lines_.push_back(std::format("...and {} more",
                                     failed_total - static_cast<int>(failures.size())));
    }

    scroll_       = 0;
    failed_total_ = failed_total;
    active_       = true;
}

void FailureListDialog::close()
{
    active_ = false;
}

bool FailureListDialog::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // consume other events while active

    const SDL_Keycode key = e.key.key;

    // Close on Esc or Enter
    if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        close();
        return true;
    }

    // Scroll up
    if (key == SDLK_UP) {
        scroll_ = std::max(0, scroll_ - 1);
        return true;
    }

    // Scroll down
    if (key == SDLK_DOWN) {
        scroll_ = std::min(scroll_ + 1,
                          static_cast<int>(lines_.size()) - 1);
        return true;
    }

    // Page up
    if (key == SDLK_PAGEUP) {
        scroll_ = std::max(0, scroll_ - 10);
        return true;
    }

    // Page down
    if (key == SDLK_PAGEDOWN) {
        scroll_ = std::min(scroll_ + 10,
                          static_cast<int>(lines_.size()) - 1);
        return true;
    }

    return true;   // consume input while active
}

void FailureListDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const
{
    if (!active_) return;

    using namespace gfx::theme;

    // Veil the whole window.
    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});

    // Centered 0.7W × 0.7H modal with rounded corners.
    const float mw = W * 0.7f;
    const float mh = H * 0.7f;
    const float mx = (W - mw) / 2;
    const float my = (H - mh) / 2;

    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    const float ix = mx + 20;
    const float iy = my + 20;

    // Title: "{N} item(s) failed"
    const std::string title = std::format("{} item(s) failed", failed_total_);
    r.draw_text(font, ix, iy, title, TEXT);

    // Scrollable content area.
    const float content_top    = iy + 36;
    const float content_bottom = my + mh - 50;   // leave room for footer hint
    const float content_h      = content_bottom - content_top;
    const float line_height    = line_pitch(font.pixel_height());
    const int visible_lines    = static_cast<int>(content_h / line_height);
    const int max_scroll       = std::max(0, static_cast<int>(lines_.size()) - visible_lines);

    // Clamp scroll to valid range.
    int actual_scroll = std::min(scroll_, max_scroll);

    for (int i = 0; i < visible_lines && (actual_scroll + i) < static_cast<int>(lines_.size()); ++i) {
        const float y = content_top + static_cast<float>(i) * line_height;
        const std::string& line = lines_[actual_scroll + i];
        // Apply middle elision to fit the line into available width.
        const std::string elided = fit_text(font, line, mw - 40);
        r.draw_text(font, ix, y, elided, TEXT);
    }

    // Footer hint.
    const std::string hint = "[Esc] Close   [Up/Down] Scroll";
    const std::string elided_hint = fit_text(font, hint, mw - 40);
    r.draw_text(font, ix, my + mh - 30, elided_hint, TEXT_FAINT);
}

} // namespace ui
