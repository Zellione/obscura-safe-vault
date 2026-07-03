#include "ui/saved_search_panel.h"

#include <algorithm>
#include <format>
#include <string_view>

#include "gfx/renderer.h"
#include "gfx/theme.h"
#include "gfx/text.h"

namespace ui {

namespace {

constexpr float TOP  = 110.0f;
constexpr float LINE = 30.0f;

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
    : saved(saved_ref),
      search_(search),
      font_(font),
      status_(status_ref)
{
}

void SavedSearchPanel::handle_key(const SDL_KeyboardEvent& key)
{
    const int last = static_cast<int>(saved.size()) - 1;
    if (key.key == SDLK_DOWN)                                 cur_saved_ = std::min(cur_saved_ + 1, last);
    else if (key.key == SDLK_UP)                             cur_saved_ = std::max(cur_saved_ - 1, 0);
    else if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) {
        AdvancedQuery q;
        if (load_focused(q)) {
            // Caller (AdvancedSearchScreen) will update its query_ and rerun()
        }
    }
    else if (key.key == SDLK_DELETE)                        delete_focused();
}

void SavedSearchPanel::handle_text_input(const char* text)
{
    if (!saving_) return;
    save_buf_.append(text);
}

bool SavedSearchPanel::load_focused(AdvancedQuery& out_query)
{
    if (cur_saved_ < 0 || cur_saved_ >= static_cast<int>(saved.size())) return false;
    if (!deserialize_query(saved[cur_saved_].query, out_query)) {
        status_ = "Could not load search.";
        return false;
    }
    status_ = std::format("Loaded '{}'.", saved[cur_saved_].name);
    return true;
}

void SavedSearchPanel::delete_focused()
{
    if (cur_saved_ < 0 || cur_saved_ >= static_cast<int>(saved.size())) return;
    const std::string name = saved[cur_saved_].name;
    if (search_.delete_saved_search(name) == vault::VaultResult::Ok) {
        status_ = std::format("Deleted '{}'.", name);
        // Caller (AdvancedSearchScreen) will call reload_saved() to refresh saved_
        cur_saved_ = std::min(cur_saved_, std::max(0, static_cast<int>(saved.size()) - 1));
    } else {
        status_ = "Delete failed.";
    }
}

void SavedSearchPanel::begin_naming()
{
    saving_ = true;
    save_buf_.clear();
}

void SavedSearchPanel::finalize_save(const AdvancedQuery& query)
{
    const std::string name = trim(save_buf_);
    saving_ = false;
    if (name.empty()) {
        status_ = "Save cancelled (empty name).";
        return;
    }
    if (search_.save_search(name, query) == vault::VaultResult::Ok) {
        status_ = std::format("Saved '{}'.", name);
        // Caller (AdvancedSearchScreen) will call reload_saved() to refresh saved_
    } else {
        status_ = "Save failed.";
    }
}

void SavedSearchPanel::render(gfx::Renderer& r, float x)
{
    using namespace gfx::theme;
    const bool hot = !saving_;
    if (hot) r.draw_text(font_, x - 16, TOP, ">", ACCENT);
    r.draw_text(font_, x, TOP, "Saved searches", TEXT_DIM);

    float y = TOP + LINE;
    for (int i = 0; i < static_cast<int>(saved.size()); ++i) {
        const bool sel = (i == cur_saved_ && hot);
        r.draw_text(font_, x, y, std::format("{} {}", sel ? ">" : " ", saved[i].name),
                    sel ? TEXT : TEXT_DIM);
        y += LINE * 0.9f;
    }
    if (saved.empty()) r.draw_text(font_, x, y, "(none — Ctrl+S to save)", TEXT_FAINT);
}

int SavedSearchPanel::get_cursor() const
{
    return cur_saved_;
}

void SavedSearchPanel::set_cursor(int cur)
{
    cur_saved_ = cur;
}

std::string* SavedSearchPanel::active_buffer()
{
    if (saving_) return &save_buf_;
    return nullptr;
}

} // namespace ui
