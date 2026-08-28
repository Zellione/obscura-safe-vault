#include "ui/saved_search_panel.h"

#include <algorithm>
#include <format>
#include <string_view>

#include "gfx/renderer.h"
#include "gfx/theme.h"
#include "gfx/text.h"
#include "ui/grid_layout.h"
#include "ui/list_layout.h"
#include "ui/text_metrics.h"
#include "ui/widgets.h"

namespace ui {

namespace {

constexpr float TOP = 110.0f;

std::string trim(std::string_view s)
{
    const auto a = s.find_first_not_of(" \t\n\r");
    if (a == std::string_view::npos) return {};
    return std::string(s.substr(a, s.find_last_not_of(" \t\n\r") - a + 1));
}

} // namespace

SavedSearchPanel::SavedSearchPanel(vault::VaultSearch& search, gfx::FontAtlas& font,
                                   std::string& status_ref,
                                   std::vector<vault::SavedSearch>& saved_ref)
    : search_(search),
      font_(font),
      status_(status_ref),
      saved_(saved_ref)
{
}

SavedSearchPanel::Action SavedSearchPanel::handle_key(const SDL_KeyboardEvent& key)
{
    using enum Action;
    const int last = static_cast<int>(saved_.size()) - 1;
    if (key.key == SDLK_DOWN) {
        cur_saved_ = std::min(cur_saved_ + 1, last);
        // Keep the focused row visible when navigating.
        const float LINE = line_pitch(font_.pixel_height());
        const float row_top = TOP + (static_cast<float>(cur_saved_) + 1.0f) * LINE;  // +1 for header
        const float row_bottom = row_top + LINE;
        scroll_ = ui::ensure_visible(scroll_, row_top, row_bottom, TOP + LINE, last_max_h_);
        return None;
    }
    if (key.key == SDLK_UP) {
        cur_saved_ = std::max(cur_saved_ - 1, 0);
        // Keep the focused row visible when navigating.
        const float LINE = line_pitch(font_.pixel_height());
        const float row_top = TOP + (static_cast<float>(cur_saved_) + 1.0f) * LINE;  // +1 for header
        const float row_bottom = row_top + LINE;
        scroll_ = ui::ensure_visible(scroll_, row_top, row_bottom, TOP + LINE, last_max_h_);
        return None;
    }
    if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) {
        if (cur_saved_ >= 0 && cur_saved_ < static_cast<int>(saved_.size())) {
            return Loaded;  // Caller will invoke load_focused() and rerun()
        }
        return None;
    }
    if (key.key == SDLK_DELETE) {
        delete_focused();
        return Deleted;  // Caller will reload_saved()
    }
    return None;
}

void SavedSearchPanel::handle_wheel(float wheel_y, float max_h)
{
    const float LINE = line_pitch(font_.pixel_height());
    const auto count = static_cast<int>(saved_.size());

    // Apply wheel motion: adjust scroll by LINE * wheel_y (inverted).
    scroll_ -= wheel_y * LINE * 2.0f;

    // Clamp scroll to valid range.
    scroll_ = ui::list_clamp_scroll(scroll_, count, LINE, TOP + LINE, max_h);

    last_max_h_ = max_h;  // Cache for handle_key
}

bool SavedSearchPanel::load_focused(AdvancedQuery& out_query)
{
    if (cur_saved_ < 0 || cur_saved_ >= static_cast<int>(saved_.size())) return false;
    if (!deserialize_query(saved_[cur_saved_].query.as_span(), out_query)) {
        status_ = "Could not load search.";
        return false;
    }
    status_ = std::format("Loaded '{}'.", saved_[cur_saved_].name.view());
    return true;
}

void SavedSearchPanel::delete_focused()
{
    if (cur_saved_ < 0 || cur_saved_ >= static_cast<int>(saved_.size())) return;
    const auto name = saved_[cur_saved_].name.view();
    if (search_.delete_saved_search(name) == vault::VaultResult::Ok) {
        status_ = std::format("Deleted '{}'.", name);
        // Caller (AdvancedSearchScreen) will call reload_saved() to refresh saved_
        cur_saved_ = std::min(cur_saved_, std::max(0, static_cast<int>(saved_.size()) - 1));
    } else {
        status_ = "Delete failed.";
    }
}

void SavedSearchPanel::begin_naming()
{
    saving_ = true;
    save_buf_.clear();
}

bool SavedSearchPanel::finalize_save(const AdvancedQuery& query)
{
    const std::string name = trim(save_buf_.str());
    saving_ = false;
    if (name.empty()) {
        status_ = "Save cancelled (empty name).";
        return false;
    }
    if (search_.save_search(name, query) == vault::VaultResult::Ok) {
        status_ = std::format("Saved '{}'.", name);
        return true;  // Caller will call reload_saved() to refresh saved_
    }
    status_ = "Save failed.";
    return false;
}

void SavedSearchPanel::render(gfx::Renderer& r, float x, float max_w, float max_h, bool hot)
{
    using namespace gfx::theme;
    const float LINE = line_pitch(font_.pixel_height());

    // Cache the window height for keyboard navigation (Phase 68 Part 3).
    last_max_h_ = max_h;

    // Header (fixed, not scrolled).
    if (hot) r.draw_text(font_, x - 16, TOP, ">", ACCENT);
    r.draw_text(font_, x, TOP, "Saved searches", TEXT_DIM);

    const float content_top = TOP + LINE;
    const float content_bottom = max_h;

    // Apply scroll to render rows.
    for (int i = 0; i < static_cast<int>(saved_.size()); ++i) {
        const float row_y = content_top + static_cast<float>(i) * LINE - scroll_;

        // Skip rows entirely outside the viewport.
        if (row_y + LINE < content_top || row_y >= content_bottom) continue;

        const bool sel = (i == cur_saved_ && hot);
        r.draw_text(
            font_, x, row_y,
            fit_text(font_, std::format("{} {}", sel ? ">" : " ", saved_[i].name.view()), max_w),
            sel ? TEXT : TEXT_DIM);
    }

    // Empty list message (if scrolled to top).
    if (saved_.empty()) {
        const float msg_y = content_top - scroll_;
        if (msg_y + LINE >= content_top && msg_y < content_bottom) {
            r.draw_text(font_, x, msg_y, "(none — Ctrl+S to save)", TEXT_FAINT);
        }
    }
}

int SavedSearchPanel::get_cursor() const
{
    return cur_saved_;
}

void SavedSearchPanel::set_cursor(int cur)
{
    cur_saved_ = cur;
}

ITextInput* SavedSearchPanel::active_buffer()
{
    if (saving_) return &save_buf_;
    return nullptr;
}

} // namespace ui
