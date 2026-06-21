#include "ui/advanced_search_screen.h"

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/nav_model.h"
#include "vault/index.h"

namespace ui {

namespace {

constexpr float PAD  = 32.0f;
constexpr float TOP  = 110.0f;
constexpr float LINE = 30.0f;

std::string trim(std::string_view s)
{
    const auto a = s.find_first_not_of(" \t\n\r");
    if (a == std::string_view::npos) return {};
    return std::string(s.substr(a, s.find_last_not_of(" \t\n\r") - a + 1));
}

const char* scope_label(SearchScope s)
{
    using enum SearchScope;
    switch (s) {
        case Images:    return "Images";
        case Galleries: return "Galleries";
        default:        return "Both";
    }
}

const char* join_label(Combinator c) { return c == Combinator::And ? "AND" : "OR"; }

// Draw the group rows; returns the new baseline y. Free function so it stays out
// of the screen's method count.
float draw_groups(gfx::Renderer& r, gfx::FontAtlas& font, const std::vector<TagGroup>& groups,
                  int cur_group, bool focused, float x, float y)
{
    using namespace gfx::theme;
    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
        const bool  hot = (i == cur_group && focused);
        std::string s   = std::format("  {} {}: ", hot ? "*" : "-", join_label(groups[i].combinator));
        for (const auto& t : groups[i].tags) s += t + " ";
        r.draw_text(font, x + 8, y, s, hot ? TEXT : TEXT_FAINT);
        y += LINE * 0.9f;
    }
    return y;
}

// Draw the autocomplete dropdown (capped at 6 rows). Free function (see above).
void draw_dropdown(gfx::Renderer& r, gfx::FontAtlas& font, const std::vector<std::string>& sugg,
                   int sel, float x, float y)
{
    using namespace gfx::theme;
    for (int i = 0; i < static_cast<int>(sugg.size()) && i < 6; ++i) {
        const bool s = (i == sel);
        r.draw_text(font, x + 16, y, std::format("{} {}", s ? ">" : " ", sugg[i]), s ? ACCENT : TEXT_FAINT);
        y += LINE * 0.85f;
    }
}

} // namespace

AdvancedSearchScreen::AdvancedSearchScreen(gfx::Window& win, gfx::FontAtlas& font,
                                           vault::Vault& vault)
    : win_(win), font_(font), vault_(vault), search_(vault_)
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

// --- data flow --------------------------------------------------------------

void AdvancedSearchScreen::reload_saved()
{
    saved_      = search_.list_saved_searches();
    vocabulary_ = search_.all_tags();
    if (cur_.saved >= static_cast<int>(saved_.size())) cur_.saved = 0;
}

void AdvancedSearchScreen::rerun()
{
    results_ = search_.run_search(query_);
    cur_.result = std::clamp(cur_.result, 0, std::max(0, static_cast<int>(results_.size()) - 1));
}

std::string* AdvancedSearchScreen::active_buffer()
{
    if (saving_) return &save_buf_;
    using enum Focus;
    switch (focus_) {
        case Name:    return &edit_.name;
        case Include: return &edit_.include;
        case Exclude: return &edit_.exclude;
        case Group:   return &edit_.group;
        default:      return nullptr;
    }
}

void AdvancedSearchScreen::refresh_suggestions()
{
    using enum Focus;
    const std::string* buf = active_buffer();
    const bool tag_field = !saving_ && (focus_ == Include || focus_ == Exclude || focus_ == Group);
    if (tag_field && buf && !buf->empty()) {
        suggestions_ = tag_suggestions(*buf, vocabulary_);
        cur_.sugg    = suggestions_.empty() ? -1 : 0;
    } else {
        suggestions_.clear();
        cur_.sugg = -1;
    }
}

std::string AdvancedSearchScreen::accepted(const std::string& buf) const
{
    if (cur_.sugg >= 0 && cur_.sugg < static_cast<int>(suggestions_.size()))
        return suggestions_[cur_.sugg];
    return trim(buf);
}

// --- event handling ---------------------------------------------------------

void AdvancedSearchScreen::handle_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_TEXT_INPUT)    handle_text(e.text.text);
    else if (e.type == SDL_EVENT_KEY_DOWN) handle_key(e.key);
}

void AdvancedSearchScreen::handle_text(const char* text)
{
    if (std::string* buf = active_buffer()) {
        *buf += text;
        if (!saving_ && focus_ == Focus::Name) { query_.name_query = edit_.name; rerun(); }
        refresh_suggestions();
    }
}

void AdvancedSearchScreen::handle_key(const SDL_KeyboardEvent& key)
{
    if (saving_) { handle_save_key(key); return; }
    if ((key.mod & SDL_KMOD_CTRL) != 0 && key.key == SDLK_S) { begin_save(); return; }

    switch (key.key) {
        case SDLK_ESCAPE:    request(NavKind::ToGallery, "", 0); return;
        case SDLK_TAB:       cycle_focus((key.mod & SDL_KMOD_SHIFT) ? -1 : 1); return;
        case SDLK_BACKSPACE: backspace(); return;
        default:             dispatch_focus_key(key); return;
    }
}

void AdvancedSearchScreen::dispatch_focus_key(const SDL_KeyboardEvent& key)
{
    using enum Focus;
    switch (focus_) {
        case Scope:
        case GroupJoin: handle_axis_key(key); break;
        case Include:
        case Exclude:
        case Group:     handle_tag_field_key(key); break;
        case Results:   handle_results_key(key); break;
        case Saved:     handle_saved_key(key); break;
        case Name:      break;   // typing handled via text input
    }
}

void AdvancedSearchScreen::handle_save_key(const SDL_KeyboardEvent& key)
{
    if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) confirm_save();
    else if (key.key == SDLK_ESCAPE)    { saving_ = false; status_ = "Save cancelled."; }
    else if (key.key == SDLK_BACKSPACE) backspace();
}

void AdvancedSearchScreen::handle_axis_key(const SDL_KeyboardEvent& key)
{
    using enum Combinator;
    int dir = 0;
    if (key.key == SDLK_LEFT)       dir = -1;
    else if (key.key == SDLK_RIGHT) dir = 1;
    else                            return;
    if (focus_ == Focus::Scope) {
        cycle_scope(dir);
    } else {  // Focus::GroupJoin — a two-way toggle
        query_.group_join = query_.group_join == And ? Or : And;
        rerun();
    }
}

void AdvancedSearchScreen::handle_tag_field_key(const SDL_KeyboardEvent& key)
{
    switch (key.key) {
        case SDLK_DOWN:     move_suggestion(1);  break;
        case SDLK_UP:       move_suggestion(-1); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: commit_text();       break;
        default:
            if (focus_ == Focus::Group)        handle_group_nav_key(key);
            else if (focus_ == Focus::Include) handle_weight_key(key);
            break;
    }
}

void AdvancedSearchScreen::handle_group_nav_key(const SDL_KeyboardEvent& key)
{
    using enum Combinator;
    if (query_.groups.empty()) return;
    const auto n = static_cast<int>(query_.groups.size());
    cur_.group = std::clamp(cur_.group, 0, n - 1);
    switch (key.key) {
        case SDLK_LEFT:  cur_.group = (cur_.group - 1 + n) % n; break;
        case SDLK_RIGHT: cur_.group = (cur_.group + 1) % n;     break;
        case SDLK_DELETE: {
            TagGroup& g  = query_.groups[cur_.group];
            g.combinator = g.combinator == And ? Or : And;
            rerun();
            break;
        }
        default: break;
    }
}

void AdvancedSearchScreen::handle_weight_key(const SDL_KeyboardEvent& key)
{
    if (key.key == SDLK_EQUALS)                          ++edit_.weight;
    else if (key.key == SDLK_MINUS && edit_.weight > 1)  --edit_.weight;
}

void AdvancedSearchScreen::handle_results_key(const SDL_KeyboardEvent& key)
{
    const int last = static_cast<int>(results_.size()) - 1;
    if (key.key == SDLK_DOWN)                                 cur_.result = std::min(cur_.result + 1, last);
    else if (key.key == SDLK_UP)                             cur_.result = std::max(cur_.result - 1, 0);
    else if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) open_result();
}

void AdvancedSearchScreen::handle_saved_key(const SDL_KeyboardEvent& key)
{
    const int last = static_cast<int>(saved_.size()) - 1;
    if (key.key == SDLK_DOWN)                                 cur_.saved = std::min(cur_.saved + 1, last);
    else if (key.key == SDLK_UP)                             cur_.saved = std::max(cur_.saved - 1, 0);
    else if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) load_saved();
    else if (key.key == SDLK_DELETE)                        delete_saved();
}

void AdvancedSearchScreen::cycle_focus(int dir)
{
    constexpr int N = 8;
    const int idx = (static_cast<int>(std::to_underlying(focus_)) + dir % N + N) % N;
    focus_ = static_cast<Focus>(idx);
    refresh_suggestions();
}

void AdvancedSearchScreen::cycle_scope(int dir)
{
    const int next = (static_cast<int>(std::to_underlying(query_.scope)) + dir + 3) % 3;
    query_.scope   = static_cast<SearchScope>(next);
    rerun();
}

void AdvancedSearchScreen::move_suggestion(int dir)
{
    if (suggestions_.empty()) return;
    const auto n = static_cast<int>(suggestions_.size());
    cur_.sugg    = (cur_.sugg + dir + n) % n;
}

void AdvancedSearchScreen::commit_text()
{
    using enum Focus;
    switch (focus_) {
        case Include:
            if (std::string t = accepted(edit_.include); !t.empty())
                query_.include.emplace_back(std::move(t), edit_.weight);
            edit_.include.clear(); edit_.weight = 1; refresh_suggestions(); rerun();
            break;
        case Exclude:
            if (std::string t = accepted(edit_.exclude); !t.empty())
                query_.exclude.push_back(std::move(t));
            edit_.exclude.clear(); refresh_suggestions(); rerun();
            break;
        case Group:
            commit_group_text(accepted(edit_.group));
            edit_.group.clear(); refresh_suggestions(); rerun();
            break;
        default: break;
    }
}

void AdvancedSearchScreen::commit_group_text(std::string tag)
{
    if (tag.empty()) {   // Enter on an empty group field starts a new (OR) group.
        query_.groups.emplace_back(std::format("group {}", query_.groups.size() + 1),
                                   Combinator::Or, std::vector<std::string>{});
        cur_.group = static_cast<int>(query_.groups.size()) - 1;
        return;
    }
    if (query_.groups.empty()) {
        query_.groups.emplace_back("group 1", Combinator::Or, std::vector<std::string>{});
        cur_.group = 0;
    }
    cur_.group = std::clamp(cur_.group, 0, static_cast<int>(query_.groups.size()) - 1);
    query_.groups[cur_.group].tags.push_back(std::move(tag));
}

void AdvancedSearchScreen::backspace()
{
    using enum Focus;
    std::string* buf = active_buffer();
    if (!buf) return;

    if (!buf->empty()) {
        buf->pop_back();
        if (!saving_ && focus_ == Name) { query_.name_query = edit_.name; rerun(); }
        refresh_suggestions();
        return;
    }
    if (saving_) return;

    // Empty buffer: a Backspace removes the last committed chip of the field.
    switch (focus_) {
        case Include:
            if (!query_.include.empty()) { query_.include.pop_back(); rerun(); }
            break;
        case Exclude:
            if (!query_.exclude.empty()) { query_.exclude.pop_back(); rerun(); }
            break;
        case Group: {
            if (query_.groups.empty()) break;
            cur_.group = std::clamp(cur_.group, 0, static_cast<int>(query_.groups.size()) - 1);
            if (TagGroup& g = query_.groups[cur_.group]; !g.tags.empty()) g.tags.pop_back();
            else { query_.groups.erase(query_.groups.begin() + cur_.group);
                   cur_.group = std::max(0, cur_.group - 1); }
            rerun();
            break;
        }
        default: break;
    }
}

void AdvancedSearchScreen::open_result()
{
    if (cur_.result < 0 || cur_.result >= static_cast<int>(results_.size())) return;
    const vault::SearchHit& hit = results_[cur_.result];

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
    if (cur_.saved < 0 || cur_.saved >= static_cast<int>(saved_.size())) return;
    AdvancedQuery q;
    if (!deserialize_query(saved_[cur_.saved].query, q)) { status_ = "Could not load search."; return; }
    query_     = std::move(q);
    edit_.name = query_.name_query;
    cur_.group = 0;
    status_    = std::format("Loaded '{}'.", saved_[cur_.saved].name);
    rerun();
}

void AdvancedSearchScreen::delete_saved()
{
    if (cur_.saved < 0 || cur_.saved >= static_cast<int>(saved_.size())) return;
    const std::string name = saved_[cur_.saved].name;
    if (search_.delete_saved_search(name) == vault::VaultResult::Ok) {
        status_ = std::format("Deleted '{}'.", name);
        reload_saved();
        cur_.saved = std::min(cur_.saved, std::max(0, static_cast<int>(saved_.size()) - 1));
    } else {
        status_ = "Delete failed.";
    }
}

void AdvancedSearchScreen::begin_save()
{
    saving_ = true;
    save_buf_.clear();
    suggestions_.clear();
    cur_.sugg = -1;
}

void AdvancedSearchScreen::confirm_save()
{
    const std::string name = trim(save_buf_);
    saving_ = false;
    if (name.empty()) { status_ = "Save cancelled (empty name)."; return; }
    if (search_.save_search(name, query_) == vault::VaultResult::Ok) {
        status_ = std::format("Saved '{}'.", name);
        reload_saved();
    } else {
        status_ = "Save failed.";
    }
}

// --- rendering --------------------------------------------------------------

void AdvancedSearchScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    r.draw_text(font_, PAD, 36, "Advanced Search", TEXT);
    r.draw_text(font_, PAD, 74,
                "[Tab] Field  [Enter] Add/Open  [Bksp] Remove  [Ctrl+S] Save  [Esc] Back",
                TEXT_FAINT);

    const float colW = (W - 2 * PAD) / 3.0f - 16;
    render_builder(r, PAD, TOP);
    const float mx = PAD + colW + 24;
    render_results(r, mx, colW);
    render_saved(r, mx + colW + 24);

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

void AdvancedSearchScreen::render_builder(gfx::Renderer& r, float x, float top)
{
    using namespace gfx::theme;
    auto focused = [&](Focus f) { return focus_ == f && !saving_; };
    auto label   = [&](float ly, Focus f, std::string_view s) {
        if (focused(f)) r.draw_text(font_, x - 16, ly, ">", ACCENT);
        r.draw_text(font_, x, ly, s, focused(f) ? TEXT : TEXT_DIM);
    };

    float y = top;
    label(y, Focus::Name, std::format("Name: {}", edit_.name.empty() ? "(any)" : edit_.name)); y += LINE;
    label(y, Focus::Scope, std::format("Scope: {}", scope_label(query_.scope))); y += LINE;

    std::string inc = "Include: ";
    for (const auto& wt : query_.include) inc += std::format("{}({}) ", wt.tag, wt.weight);
    if (focused(Focus::Include)) inc += std::format("[{}_ w{}]", edit_.include, edit_.weight);
    label(y, Focus::Include, inc); y += LINE;

    std::string exc = "Exclude: ";
    for (const auto& t : query_.exclude) exc += t + " ";
    if (focused(Focus::Exclude)) exc += std::format("[{}_]", edit_.exclude);
    label(y, Focus::Exclude, exc); y += LINE;

    label(y, Focus::Group, "Groups:"); y += LINE;
    y = draw_groups(r, font_, query_.groups, cur_.group, focused(Focus::Group), x, y);
    if (focused(Focus::Group)) {
        r.draw_text(font_, x + 8, y,
                    std::format("  [{}_]  Enter=add, empty Enter=new group, Del=AND/OR", edit_.group),
                    TEXT_FAINT);
        y += LINE;
    }
    label(y, Focus::GroupJoin, std::format("Join groups: {}", join_label(query_.group_join)));
    y += LINE;

    if (!suggestions_.empty()) draw_dropdown(r, font_, suggestions_, cur_.sugg, x, y);
}

void AdvancedSearchScreen::render_results(gfx::Renderer& r, float x, float colw)
{
    using namespace gfx::theme;
    const bool hot = (focus_ == Focus::Results && !saving_);
    if (hot) r.draw_text(font_, x - 16, 74, ">", ACCENT);
    r.draw_text(font_, x, 74, std::format("Results ({})", results_.size()), TEXT_DIM);

    const auto  H        = static_cast<float>(win_.height());
    const auto  max_rows = static_cast<int>((H - TOP - 40) / (LINE * 0.9f));
    const int   first    = std::max(0, cur_.result - max_rows / 2);
    float       y        = TOP;
    for (int i = first; i < static_cast<int>(results_.size()) && i < first + max_rows; ++i) {
        const bool              sel = (i == cur_.result && hot);
        const vault::SearchHit& hit = results_[i];
        if (sel) r.draw_round_rect({x - 6, y - 4, colw + 12, LINE * 0.9f}, RADIUS_SMALL, SURFACE_HI);
        r.draw_text(font_, x, y, std::format("{}{}", hit.is_gallery ? "[D] " : "    ", hit.path),
                    sel ? TEXT : TEXT_DIM);
        y += LINE * 0.9f;
    }
    if (results_.empty()) r.draw_text(font_, x, y, "(no matches)", TEXT_FAINT);
}

void AdvancedSearchScreen::render_saved(gfx::Renderer& r, float x)
{
    using namespace gfx::theme;
    const bool hot = (focus_ == Focus::Saved && !saving_);
    if (hot) r.draw_text(font_, x - 16, 74, ">", ACCENT);
    r.draw_text(font_, x, 74, "Saved searches", TEXT_DIM);

    float y = TOP;
    for (int i = 0; i < static_cast<int>(saved_.size()); ++i) {
        const bool sel = (i == cur_.saved && hot);
        r.draw_text(font_, x, y, std::format("{} {}", sel ? ">" : " ", saved_[i].name),
                    sel ? TEXT : TEXT_DIM);
        y += LINE * 0.9f;
    }
    if (saved_.empty()) r.draw_text(font_, x, y, "(none — Ctrl+S to save)", TEXT_FAINT);
}

} // namespace ui
