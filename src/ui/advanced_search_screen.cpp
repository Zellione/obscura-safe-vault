#include "ui/advanced_search_screen.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/nav_model.h"
#include "vault/index.h"

namespace ui {

namespace {

constexpr float PAD      = 32.0f;
constexpr float TOP      = 110.0f;
constexpr float LINE     = 30.0f;

std::string trim(std::string_view s)
{
    const auto a = s.find_first_not_of(" \t\n\r");
    if (a == std::string_view::npos) return {};
    return std::string(s.substr(a, s.find_last_not_of(" \t\n\r") - a + 1));
}

SearchScope scope_of(int idx)
{
    switch (idx) {
        case 0:  return SearchScope::Images;
        case 1:  return SearchScope::Galleries;
        default: return SearchScope::Both;
    }
}

const char* scope_label(SearchScope s)
{
    switch (s) {
        case SearchScope::Images:    return "Images";
        case SearchScope::Galleries: return "Galleries";
        default:                     return "Both";
    }
}

const char* join_label(Combinator c) { return c == Combinator::And ? "AND" : "OR"; }

} // namespace

AdvancedSearchScreen::AdvancedSearchScreen(gfx::Window& win, gfx::FontAtlas& font,
                                           vault::Vault& vault)
    : win_(win), font_(font), vault_(vault)
{
}

void AdvancedSearchScreen::on_enter()
{
    reload_saved();
    rerun();
    SDL_StartTextInput(win_.sdl_window());
}

void AdvancedSearchScreen::on_exit()
{
    SDL_StopTextInput(win_.sdl_window());
}

void AdvancedSearchScreen::reload_saved()
{
    saved_      = vault_.list_saved_searches();
    vocabulary_ = vault_.all_tags();
    if (saved_sel_ >= static_cast<int>(saved_.size())) saved_sel_ = 0;
}

void AdvancedSearchScreen::rerun()
{
    results_ = vault_.run_search(query_);
    if (result_sel_ >= static_cast<int>(results_.size())) result_sel_ = 0;
    if (result_sel_ < 0) result_sel_ = 0;
}

std::string* AdvancedSearchScreen::active_buffer()
{
    if (saving_) return &save_buf_;
    switch (focus_) {
        case Focus::Name:    return &name_buf_;
        case Focus::Include: return &inc_buf_;
        case Focus::Exclude: return &exc_buf_;
        case Focus::Group:   return &grp_buf_;
        default:             return nullptr;
    }
}

void AdvancedSearchScreen::refresh_suggestions()
{
    std::string* buf = active_buffer();
    const bool tag_field = !saving_ &&
        (focus_ == Focus::Include || focus_ == Focus::Exclude || focus_ == Focus::Group);
    if (tag_field && buf && !buf->empty()) {
        suggestions_ = tag_suggestions(*buf, vocabulary_);
        sugg_sel_    = suggestions_.empty() ? -1 : 0;
    } else {
        suggestions_.clear();
        sugg_sel_ = -1;
    }
}

void AdvancedSearchScreen::cycle_focus(int dir)
{
    constexpr int N = 8;
    int idx = (static_cast<int>(focus_) + dir % N + N) % N;
    focus_ = static_cast<Focus>(idx);
    refresh_suggestions();
}

void AdvancedSearchScreen::handle_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_TEXT_INPUT)      handle_text(e.text.text);
    else if (e.type == SDL_EVENT_KEY_DOWN)   handle_key(e.key);
}

void AdvancedSearchScreen::handle_text(const char* text)
{
    if (std::string* buf = active_buffer()) {
        *buf += text;
        if (!saving_ && focus_ == Focus::Name) { query_.name_query = name_buf_; rerun(); }
        refresh_suggestions();
    }
}

void AdvancedSearchScreen::backspace()
{
    std::string* buf = active_buffer();
    if (!buf) return;

    if (!buf->empty()) {
        buf->pop_back();
        if (!saving_ && focus_ == Focus::Name) { query_.name_query = name_buf_; rerun(); }
        refresh_suggestions();
        return;
    }
    if (saving_) return;

    // Empty buffer: a Backspace removes the last committed chip of the field.
    switch (focus_) {
        case Focus::Include:
            if (!query_.include.empty()) { query_.include.pop_back(); rerun(); }
            break;
        case Focus::Exclude:
            if (!query_.exclude.empty()) { query_.exclude.pop_back(); rerun(); }
            break;
        case Focus::Group:
            if (!query_.groups.empty() && group_sel_ < static_cast<int>(query_.groups.size())) {
                auto& g = query_.groups[group_sel_];
                if (!g.tags.empty())      g.tags.pop_back();
                else { query_.groups.erase(query_.groups.begin() + group_sel_);
                       group_sel_ = std::max(0, group_sel_ - 1); }
                rerun();
            }
            break;
        default: break;
    }
}

void AdvancedSearchScreen::commit_text()
{
    if (saving_) { confirm_save(); return; }

    // The accepted text is the highlighted suggestion, else the typed buffer.
    auto accepted = [&](const std::string& buf) {
        if (sugg_sel_ >= 0 && sugg_sel_ < static_cast<int>(suggestions_.size()))
            return suggestions_[sugg_sel_];
        return trim(buf);
    };

    switch (focus_) {
        case Focus::Include: {
            if (std::string t = accepted(inc_buf_); !t.empty())
                query_.include.push_back(WeightedTag{std::move(t), inc_weight_});
            inc_buf_.clear(); inc_weight_ = 1; refresh_suggestions(); rerun();
            break;
        }
        case Focus::Exclude: {
            if (std::string t = accepted(exc_buf_); !t.empty())
                query_.exclude.push_back(std::move(t));
            exc_buf_.clear(); refresh_suggestions(); rerun();
            break;
        }
        case Focus::Group: {
            std::string t = accepted(grp_buf_);
            if (t.empty()) {
                // Enter on an empty group field starts a new (OR) group.
                query_.groups.push_back(TagGroup{"group " + std::to_string(query_.groups.size() + 1),
                                                 Combinator::Or, {}});
                group_sel_ = static_cast<int>(query_.groups.size()) - 1;
            } else {
                if (query_.groups.empty()) {
                    query_.groups.push_back(TagGroup{"group 1", Combinator::Or, {}});
                    group_sel_ = 0;
                }
                query_.groups[group_sel_].tags.push_back(std::move(t));
            }
            grp_buf_.clear(); refresh_suggestions(); rerun();
            break;
        }
        default: break;
    }
}

void AdvancedSearchScreen::open_result()
{
    if (result_sel_ < 0 || result_sel_ >= static_cast<int>(results_.size())) return;
    const vault::SearchHit& hit = results_[result_sel_];

    if (hit.is_gallery) { request(NavKind::ToGallery, hit.path, 0); return; }

    // Media hit: open the normal viewer over its containing gallery, positioned
    // on this item, so prev/next iterate that gallery.
    const auto segs = split_path(hit.path);
    std::string parent;
    if (segs.size() > 1) parent = join_path(std::span(segs.data(), segs.size() - 1));
    const auto siblings = vault_.list(parent);
    for (int i = 0; i < static_cast<int>(siblings.size()); ++i) {
        if (siblings[i]->name == segs.back()) { request(NavKind::ToViewer, parent, i); return; }
    }
}

void AdvancedSearchScreen::load_saved()
{
    if (saved_sel_ < 0 || saved_sel_ >= static_cast<int>(saved_.size())) return;
    AdvancedQuery q;
    if (!deserialize_query(saved_[saved_sel_].query, q)) { status_ = "Could not load search."; return; }
    query_     = std::move(q);
    name_buf_  = query_.name_query;
    scope_idx_ = static_cast<int>(query_.scope);
    group_sel_ = 0;
    status_    = "Loaded '" + saved_[saved_sel_].name + "'.";
    rerun();
}

void AdvancedSearchScreen::delete_saved()
{
    if (saved_sel_ < 0 || saved_sel_ >= static_cast<int>(saved_.size())) return;
    const std::string name = saved_[saved_sel_].name;
    if (vault_.delete_saved_search(name) == vault::VaultResult::Ok) {
        status_ = "Deleted '" + name + "'.";
        reload_saved();
        saved_sel_ = std::min(saved_sel_, std::max(0, static_cast<int>(saved_.size()) - 1));
    } else {
        status_ = "Delete failed.";
    }
}

void AdvancedSearchScreen::begin_save()
{
    saving_ = true;
    save_buf_.clear();
    suggestions_.clear();
    sugg_sel_ = -1;
}

void AdvancedSearchScreen::confirm_save()
{
    const std::string name = trim(save_buf_);
    saving_ = false;
    if (name.empty()) { status_ = "Save cancelled (empty name)."; return; }
    if (vault_.save_search(name, query_) == vault::VaultResult::Ok) {
        status_ = "Saved '" + name + "'.";
        reload_saved();
    } else {
        status_ = "Save failed.";
    }
}

void AdvancedSearchScreen::handle_key(const SDL_KeyboardEvent& key)
{
    // Save-name entry is a small inline modal: Enter confirms, Esc cancels.
    if (saving_) {
        if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) confirm_save();
        else if (key.key == SDLK_ESCAPE)                        { saving_ = false; status_ = "Save cancelled."; }
        else if (key.key == SDLK_BACKSPACE)                     backspace();
        return;
    }

    const bool ctrl = (key.mod & SDL_KMOD_CTRL) != 0;
    if (ctrl && key.key == SDLK_S) { begin_save(); return; }   // Ctrl+S: save current query

    switch (key.key) {
        case SDLK_ESCAPE:    request(NavKind::ToGallery, "", 0); return;
        case SDLK_TAB:       cycle_focus((key.mod & SDL_KMOD_SHIFT) ? -1 : 1); return;
        case SDLK_BACKSPACE: backspace(); return;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  break;   // handled per-focus below
        default: break;
    }

    using enum Focus;
    switch (focus_) {
        case Name:
            break;  // typing handled via text input; arrows ignored
        case Scope:
            if (key.key == SDLK_LEFT)  { scope_idx_ = (scope_idx_ + 2) % 3; query_.scope = scope_of(scope_idx_); rerun(); }
            if (key.key == SDLK_RIGHT) { scope_idx_ = (scope_idx_ + 1) % 3; query_.scope = scope_of(scope_idx_); rerun(); }
            break;
        case GroupJoin:
            if (key.key == SDLK_LEFT || key.key == SDLK_RIGHT) {
                query_.group_join = query_.group_join == Combinator::And ? Combinator::Or : Combinator::And;
                rerun();
            }
            break;
        case Include:
        case Exclude:
        case Group:
            if (key.key == SDLK_DOWN && !suggestions_.empty())
                sugg_sel_ = (sugg_sel_ + 1) % static_cast<int>(suggestions_.size());
            else if (key.key == SDLK_UP && !suggestions_.empty())
                sugg_sel_ = (sugg_sel_ - 1 + static_cast<int>(suggestions_.size())) % static_cast<int>(suggestions_.size());
            else if ((key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER))
                commit_text();
            else if (focus_ == Group && key.key == SDLK_LEFT && !query_.groups.empty())
                group_sel_ = (group_sel_ - 1 + static_cast<int>(query_.groups.size())) % static_cast<int>(query_.groups.size());
            else if (focus_ == Group && key.key == SDLK_RIGHT && !query_.groups.empty())
                group_sel_ = (group_sel_ + 1) % static_cast<int>(query_.groups.size());
            else if (focus_ == Group && key.key == SDLK_DELETE && !query_.groups.empty()) {
                auto& g = query_.groups[group_sel_];
                g.combinator = g.combinator == Combinator::And ? Combinator::Or : Combinator::And;
                rerun();
            } else if (focus_ == Include && key.key == SDLK_EQUALS) { ++inc_weight_; }
            else if (focus_ == Include && key.key == SDLK_MINUS && inc_weight_ > 1) { --inc_weight_; }
            break;
        case Results:
            if (key.key == SDLK_DOWN && !results_.empty())
                result_sel_ = std::min(result_sel_ + 1, static_cast<int>(results_.size()) - 1);
            else if (key.key == SDLK_UP)
                result_sel_ = std::max(result_sel_ - 1, 0);
            else if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER)
                open_result();
            break;
        case Saved:
            if (key.key == SDLK_DOWN && !saved_.empty())
                saved_sel_ = std::min(saved_sel_ + 1, static_cast<int>(saved_.size()) - 1);
            else if (key.key == SDLK_UP)
                saved_sel_ = std::max(saved_sel_ - 1, 0);
            else if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER)
                load_saved();
            else if (key.key == SDLK_DELETE)
                delete_saved();
            break;
    }
}

// --- rendering ------------------------------------------------------------

void AdvancedSearchScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    r.draw_text(font_, PAD, 36, "Advanced Search", TEXT);
    r.draw_text(font_, PAD, 74,
                "[Tab] Field  [Enter] Add/Open  [Bksp] Remove  [Ctrl+S] Save  [Esc] Back",
                TEXT_FAINT);

    auto focused = [&](Focus f) { return focus_ == f && !saving_; };
    auto label = [&](float x, float y, Focus f, const std::string& s, gfx::Color c) {
        if (focused(f)) r.draw_text(font_, x - 16, y, ">", ACCENT);
        r.draw_text(font_, x, y, s, c);
    };

    // --- left column: query builder ---
    const float colW = (W - 2 * PAD) / 3.0f - 16;
    float y = TOP;

    label(PAD, y, Focus::Name, "Name: " + (name_buf_.empty() ? std::string("(any)") : name_buf_),
          focused(Focus::Name) ? TEXT : TEXT_DIM); y += LINE;
    label(PAD, y, Focus::Scope, std::string("Scope: ") + scope_label(query_.scope), TEXT_DIM); y += LINE;

    // Include chips.
    {
        std::string s = "Include: ";
        for (const auto& wt : query_.include) s += wt.tag + "(" + std::to_string(wt.weight) + ") ";
        if (focused(Focus::Include)) s += "[" + inc_buf_ + "_ w" + std::to_string(inc_weight_) + "]";
        label(PAD, y, Focus::Include, s, TEXT_DIM); y += LINE;
    }
    // Exclude chips.
    {
        std::string s = "Exclude: ";
        for (const auto& t : query_.exclude) s += t + " ";
        if (focused(Focus::Exclude)) s += "[" + exc_buf_ + "_]";
        label(PAD, y, Focus::Exclude, s, TEXT_DIM); y += LINE;
    }
    // Groups.
    label(PAD, y, Focus::Group, "Groups:", TEXT_DIM); y += LINE;
    for (int i = 0; i < static_cast<int>(query_.groups.size()); ++i) {
        const auto& g = query_.groups[i];
        std::string s = std::string("  ") + (i == group_sel_ && focused(Focus::Group) ? "* " : "- ")
                      + join_label(g.combinator) + ": ";
        for (const auto& t : g.tags) s += t + " ";
        r.draw_text(font_, PAD + 8, y, s, i == group_sel_ ? TEXT : TEXT_FAINT); y += LINE * 0.9f;
    }
    if (focused(Focus::Group)) { r.draw_text(font_, PAD + 8, y, "  [" + grp_buf_ + "_]  Enter=add, empty Enter=new group, Del=AND/OR", TEXT_FAINT); y += LINE; }
    label(PAD, y, Focus::GroupJoin, std::string("Join groups: ") + join_label(query_.group_join), TEXT_DIM);
    y += LINE;

    // Autocomplete dropdown beneath the builder.
    if (!suggestions_.empty()) {
        for (int i = 0; i < static_cast<int>(suggestions_.size()) && i < 6; ++i) {
            const bool sel = (i == sugg_sel_);
            r.draw_text(font_, PAD + 16, y, (sel ? "> " : "  ") + suggestions_[i], sel ? ACCENT : TEXT_FAINT);
            y += LINE * 0.85f;
        }
    }

    // --- middle column: results ---
    const float mx = PAD + colW + 24;
    float ry = TOP;
    label(mx, 74, Focus::Results, "Results (" + std::to_string(results_.size()) + ")", TEXT_DIM);
    const int max_rows = static_cast<int>((H - TOP - 40) / (LINE * 0.9f));
    const int first = std::max(0, result_sel_ - max_rows / 2);
    for (int i = first; i < static_cast<int>(results_.size()) && i < first + max_rows; ++i) {
        const bool sel = (i == result_sel_ && focused(Focus::Results));
        const auto& hit = results_[i];
        const std::string s = (hit.is_gallery ? "[D] " : "    ") + hit.path;
        if (sel) { const SDL_FRect row{mx - 6, ry - 4, colW + 12, LINE * 0.9f};
                   r.draw_round_rect(row, RADIUS_SMALL, SURFACE_HI); }
        r.draw_text(font_, mx, ry, s, sel ? TEXT : TEXT_DIM);
        ry += LINE * 0.9f;
    }
    if (results_.empty()) r.draw_text(font_, mx, ry, "(no matches)", TEXT_FAINT);

    // --- right column: saved searches ---
    const float sx = mx + colW + 24;
    float sy = TOP;
    label(sx, 74, Focus::Saved, "Saved searches", TEXT_DIM);
    for (int i = 0; i < static_cast<int>(saved_.size()); ++i) {
        const bool sel = (i == saved_sel_ && focused(Focus::Saved));
        r.draw_text(font_, sx, sy, (sel ? "> " : "  ") + saved_[i].name, sel ? TEXT : TEXT_DIM);
        sy += LINE * 0.9f;
    }
    if (saved_.empty()) r.draw_text(font_, sx, sy, "(none — Ctrl+S to save)", TEXT_FAINT);

    if (saving_) {
        r.draw_rect({0, 0, W, H}, {0, 0, 0, 180}, /*filled*/ true);
        const SDL_FRect box{W / 2 - 220, H / 2 - 40, 440, 80};
        r.draw_round_rect(box, RADIUS, SURFACE);
        r.draw_round_rect(box, RADIUS, ACCENT, /*filled*/ false);
        r.draw_text(font_, box.x + 16, box.y + 14, "Save search as:", TEXT_DIM);
        r.draw_text(font_, box.x + 16, box.y + 44, save_buf_ + "_", TEXT);
    }

    if (!status_.empty()) r.draw_text(font_, PAD, H - 24, status_, TEXT_FAINT);
}

} // namespace ui
