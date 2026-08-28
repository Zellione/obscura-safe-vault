#include "ui/advanced_search_screen.h"

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/gallery_view_pref.h"
#include "platform/perf.h"
#include "ui/favorite_batch.h"
#include "ui/gallery_view.h"
#include "ui/gallery_view_setting.h"
#include "ui/grid_layout.h"
#include "ui/list_layout.h"
#include "ui/nav_model.h"
#include "ui/parent_group.h"
#include "ui/position_label.h"
#include "ui/text_input_event.h"
#include "ui/text_metrics.h"
#include "ui/tile_thumb.h"
#include "ui/widgets.h"
#include "vault/index.h"

namespace ui {

namespace {

constexpr float PAD = 32.0f;
constexpr float TOP = 110.0f;

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

// Layout + selection state for draw_groups, bundled to keep its parameter list
// small (cpp:S107).
struct GroupLayout {
    int   cur_group;   // index of the focused group
    int   sel_tag;     // selected tag within the current group (-1 = none)
    bool  focused;     // is the Groups field focused?
    float x;           // left edge
    float colw;        // column width (sizes the highlight bar)
    float y;           // starting baseline
};

// Draw the groups as a vertical list: each group a header row ("* AND name:" when
// current, "- OR name:" otherwise) followed by its tags one per indented row. The
// tag at `lay.sel_tag` in the current group is highlighted. Returns the new
// baseline y. Free function so it stays out of the method count.
float draw_groups(gfx::Renderer& r, gfx::FontAtlas& font, const std::vector<TagGroup>& groups,
                  const GroupLayout& lay)
{
    using namespace gfx::theme;
    const float LINE = line_pitch(font.pixel_height());
    const float ROW  = LINE;   // per-tag rows are full lines now, not 0.85 of one
    // Offset from a draw-y to the text's ink centre (see render_builder), so the
    // selected-tag highlight box can be centred on the tag text it wraps.
    const float ink_dy = -font.text_top_for_center(0.0f);
    float       y      = lay.y;
    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
        const bool  hot  = (i == lay.cur_group && lay.focused);
        std::string head = std::format("  {} {} {}:", hot ? "*" : "-",
                                       join_label(groups[i].combinator), groups[i].name);
        r.draw_text(font, lay.x + 8, y, fit_text(font, head, lay.colw - 16),
                    hot ? TEXT : TEXT_FAINT);
        y += ROW;
        for (int t = 0; t < static_cast<int>(groups[i].tags.size()); ++t) {
            const bool sel = hot && lay.sel_tag == t;
            if (sel) {
                r.draw_round_rect({lay.x + 18, y + ink_dy - ROW * 0.5f, lay.colw - 12, ROW},
                                  RADIUS_SMALL, SURFACE_HI);
                r.draw_text(font, lay.x + 28, y, ">", ACCENT);
            }
            r.draw_text(font, lay.x + 48, y, fit_text(font, groups[i].tags[t], lay.colw - 60),
                        sel ? TEXT : TEXT_DIM);
            y += ROW;
        }
    }
    return y;
}

// Draw the autocomplete dropdown (capped at 6 rows) as a floating panel. Painted
// with an opaque background so it cleanly occludes the builder fields beneath it
// instead of bleeding into them. `colw` sizes the panel. Free function (see above).
void draw_dropdown(gfx::Renderer& r, gfx::FontAtlas& font, const std::vector<std::string>& sugg,
                   int sel, float x, float y, float colw)
{
    using namespace gfx::theme;
    const float LINE = line_pitch(font.pixel_height());
    const float ROW  = LINE;   // per-tag rows are full lines now, not 0.85 of one
    const int n = std::min(static_cast<int>(sugg.size()), 6);
    if (n == 0) return;
    // Panel covers n ROW-tall slots starting at `y`; it spans the column width and
    // overlaps the labels beneath. Each suggestion's ink is vertically centred in
    // its slot via text_top_for_center (draw_text's y is the glyph-cell top, so the
    // ink would otherwise sit low and poke out of the panel).
    const SDL_FRect panel{x - 6.0f, y, colw + 12.0f, static_cast<float>(n) * ROW};
    r.draw_round_rect(panel, RADIUS_SMALL, SURFACE);
    r.draw_round_rect(panel, RADIUS_SMALL, ACCENT, /*filled*/ false);
    for (int i = 0; i < n; ++i) {
        const bool  s  = (i == sel);
        const float ty = font.text_top_for_center(y + (static_cast<float>(i) + 0.5f) * ROW);
        r.draw_text(font, x + 16, ty,
                    fit_text(font, std::format("{} {}", s ? ">" : " ", sugg[i]), colw - 16),
                    s ? ACCENT : TEXT_FAINT);
    }
}

} // namespace

bool current_detail_open(const AdvancedSearchScreen& s) { return s.detail_.panel.open; }

AdvancedSearchScreen::AdvancedSearchScreen(gfx::Window& win, gfx::FontAtlas& font,
                                           vault::Vault& vault, gfx::TextureCache& cache,
                                           AdvancedSearchState& session,
                                           const CollectionBatchOps::Deps& ops_deps,
                                           bool initial_detail_open)
    : win_(win), font_(font), vault_(vault), cache_(cache), session_(session), search_(vault_),
      result_view_(vault_, win_, font_, cache_),
      saved_panel_(search_, font_, status_, saved_),
      ops_(ops_deps)
{
    detail_.panel.open = initial_detail_open;
    // Wire up the result view's request callback to navigate to opened results
    result_view_.set_request_callback([this](int nav_kind, const std::string& path, int idx) {
        open_result(nav_kind, path, idx);
    });
}

void AdvancedSearchScreen::on_enter()
{
    // Restore the session-scoped state (query + builder buffers + cursor)
    // so returning to the screen shows the previous search; a fresh session
    // (active == false) just keeps the default-constructed members.
    if (session_.active) {
        query_         = session_.query;
        edit_.name.set_text(session_.name);
        edit_.include.set_text(session_.include);
        edit_.exclude.set_text(session_.exclude);
        edit_.group.set_text(session_.group);
        edit_.weight   = session_.weight;
        focus_         = static_cast<Focus>(session_.focus);
        cur_.group     = session_.cur_group;
    }
    reload_saved();
    rerun();   // re-derive results_ from query_ (node pointers can't be persisted)

    // Restore sub-view session state
    result_view_.set_cursor(session_.cur_result);
    // Phase 93: the view is the shared machine-wide setting — List or a grid
    // density, exactly as the gallery grid left it.
    result_view_.set_view(gallery_view_setting());
    saved_panel_.set_cursor(session_.cur_saved);

    SDL_StartTextInput(win_.sdl_window());
}

void AdvancedSearchScreen::on_exit()
{
    // Persist the current state so it survives until the screen is reopened (or
    // until App resets the session when the active vault changes).
    session_.active     = true;
    session_.query      = query_;
    session_.name       = edit_.name.str();
    session_.include    = edit_.include.str();
    session_.exclude    = edit_.exclude.str();
    session_.group      = edit_.group.str();
    session_.weight     = edit_.weight;
    session_.focus      = std::to_underlying(focus_);
    session_.cur_group  = cur_.group;

    // Persist sub-view session state
    session_.cur_result = result_view_.get_cursor();
    // Phase 93: the view is NOT stored here — it is the shared machine-wide
    // setting (ui::gallery_view_setting + gallery_view.conf), so no session
    // field to write.
    session_.cur_saved  = saved_panel_.get_cursor();

    SDL_StopTextInput(win_.sdl_window());
}

void AdvancedSearchScreen::on_vault_changed()
{
    // Phase 50: vault's index tree changed (background import drain attached nodes).
    // results_ are now stale; re-run the search.
    rerun();
    mark_dirty();
}

void AdvancedSearchScreen::on_gallery_view_changed(GalleryView view)
{
    if (result_view_.get_view() == view) return;
    result_view_.set_view(view);
    mark_dirty();
}

// --- data flow --------------------------------------------------------------

void AdvancedSearchScreen::reload_saved()
{
    saved_      = search_.list_saved_searches();
    live_.vocabulary = search_.all_tags();
    // Clamp saved_panel's cursor to the new size
    int cur = saved_panel_.get_cursor();
    if (cur >= static_cast<int>(saved_.size())) cur = 0;
    saved_panel_.set_cursor(cur);
}

void AdvancedSearchScreen::rerun()
{
    const platform::PerfScope perf("advsearch.rerun");
    auto results = search_.run_search(query_);
    result_view_.update_results(results);
    detail_.key.clear();   // SearchHit::node pointers are now invalid
}

void AdvancedSearchScreen::open_result(int nav_kind, const std::string& path, int idx)
{
    // Phase 58: flush any pending debounced rerun before opening a result,
    // so the user never opens a stale result from before they finished typing.
    if (live_.rerun.armed()) { live_.rerun.cancel(); rerun(); }
    request(static_cast<NavKind>(nav_kind), path, idx);
}

void AdvancedSearchScreen::rebuild_detail()
{
    if (!detail_.panel.open) { return; }
    const int  idx   = result_view_.get_cursor();
    const auto count = static_cast<int>(result_view_.get_results().size());

    std::string key = std::format("{}|{}", idx, count);
    if (key == detail_.key) { return; }
    detail_.key = std::move(key);
    detail_.panel.scroll = 0.0f;

    if (idx < 0 || idx >= count || result_view_.get_results()[static_cast<size_t>(idx)].node == nullptr) {
        detail_.content = DetailContent{};
        return;
    }
    const auto& hit = result_view_.get_results()[static_cast<size_t>(idx)];
    const auto from_contents = hit.node->is_gallery() ? contents_tags(vault_, hit.path) : std::vector<std::string>{};
    detail_.content = build_node_details(*hit.node, inherited_tags(vault_, hit.path), from_contents, vault::vault_settings(vault_).default_sort);
}

void AdvancedSearchScreen::start_rename()
{
    if (rename_.active()) return;
    const auto& results = result_view_.get_results();
    const int   idx     = result_view_.get_cursor();
    if (idx < 0 || idx >= static_cast<int>(results.size())) return;

    const std::string& path  = results[idx].path;
    const auto          slash = path.rfind('/');
    const std::string   gallery_path = slash == std::string::npos ? std::string{} : path.substr(0, slash);
    const std::string   name         = slash == std::string::npos ? path : path.substr(slash + 1);
    rename_.open(gallery_path, name);
}

// --- Phase 68: batch operations over the result multi-selection --------------

namespace {

// The selected hits, or the focused one when nothing is Space-selected.
std::vector<const vault::SearchHit*> selected_hits(const SearchResultView& view)
{
    std::vector<const vault::SearchHit*> out;
    const auto& results = view.get_results();
    if (view.selection().empty()) {
        if (const int idx = view.get_cursor();
            idx >= 0 && idx < static_cast<int>(results.size())) {
            out.push_back(&results[static_cast<size_t>(idx)]);
        }
        return out;
    }
    for (int i : view.selection().indices()) {
        if (i >= 0 && i < static_cast<int>(results.size())) {
            out.push_back(&results[static_cast<size_t>(i)]);
        }
    }
    return out;
}

}  // namespace

void toggle_favorite_results(AdvancedSearchScreen& s)
{
    const auto hits = selected_hits(s.result_view_);
    std::vector<const vault::IndexNode*> nodes;
    std::vector<std::string>             paths;
    for (const vault::SearchHit* h : hits) {
        nodes.push_back(h->node);
        paths.push_back(h->path);
    }
    if (paths.empty()) { return; }
    const bool target = batch_favorite_target(nodes);
    (void)vault::set_favorites_batch(s.vault_, paths, target);
    s.status_ = std::format("{} {} {}", target ? "Favorited" : "Unfavorited", paths.size(),
                            paths.size() == 1 ? "item" : "items");
    s.mark_dirty();
}

void start_export_results(AdvancedSearchScreen& s)
{
    // The collect callback resolves the selection when the folder pick lands —
    // a rerun in between clears the selection and turns this into a clean no-op.
    std::size_t count = s.result_view_.selection().count();
    if (count == 0 && !s.result_view_.get_results().empty()) { count = 1; }   // focused fallback
    s.ops_.request_export(count, [&s]() {
        std::vector<const vault::IndexNode*> picked;
        for (const vault::SearchHit* h : selected_hits(s.result_view_)) {
            if (h->node != nullptr && !h->is_gallery) { picked.push_back(h->node); }
        }
        return picked;
    }, s.status_);
}

void start_transfer_results(AdvancedSearchScreen& s)
{
    std::vector<std::string> media_paths;
    std::vector<std::string> gallery_paths;
    for (const vault::SearchHit* h : selected_hits(s.result_view_)) {
        (h->is_gallery ? gallery_paths : media_paths).push_back(h->path);
    }
    if (media_paths.empty() && gallery_paths.empty()) { return; }
    s.ops_.request_transfer(group_by_parent(media_paths), std::move(gallery_paths), s.status_);
    s.mark_dirty();
}

void start_delete_results(AdvancedSearchScreen& s)
{
    std::vector<std::string> paths;
    for (const vault::SearchHit* h : selected_hits(s.result_view_)) {
        paths.push_back(h->path);
    }
    if (paths.empty()) { return; }
    s.ops_.request_delete(paths, s.status_);
    s.mark_dirty();
}

ITextInput* AdvancedSearchScreen::active_buffer()
{
    // Check if saved_panel is in save mode (has an active buffer)
    if (auto* buf = saved_panel_.active_buffer()) return buf;

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
    const ITextInput* buf = active_buffer();
    const bool tag_field = !saved_panel_.active_buffer() && (focus_ == Include || focus_ == Exclude || focus_ == Group);
    if (tag_field && buf && !buf->empty()) {
        live_.suggestions = tag_suggestions(buffer_text(*buf), live_.vocabulary);
        cur_.sugg    = live_.suggestions.empty() ? -1 : 0;
    } else {
        live_.suggestions.clear();
        cur_.sugg = -1;
    }
}

std::string AdvancedSearchScreen::accepted(const std::string& buf) const
{
    if (cur_.sugg >= 0 && cur_.sugg < static_cast<int>(live_.suggestions.size()))
        return live_.suggestions[cur_.sugg];
    return trim(buf);
}

// --- event handling ---------------------------------------------------------

void AdvancedSearchScreen::handle_event(const SDL_Event& e)
{
    if (ops_.handle_event(e)) { return; }   // Phase 68 batch-op modals first
    if (rename_.active()) { (void)rename_.handle_event(vault_, e); return; }

    // Precedence rule (Phase 54): the focused builder field gets first refusal,
    // so Ctrl+A selects the text being typed rather than reaching a screen
    // shortcut. field_owns_event() is what keeps the empty-buffer chip
    // semantics (Left/Right switch group, Del removes a tag, Backspace drops
    // the last chip) working exactly as they did before.
    if (ITextInput* buf = clearing_ ? nullptr : active_buffer();
        buf != nullptr && field_owns_event(*buf, e)) {
        const uint64_t rev = buf->revision();
        if (handle_text_input_event(*buf, e)) {
            if (buf->revision() != rev) after_advanced_search_buffer_edit(*this);
            return;
        }
    }

    if (e.type == SDL_EVENT_KEY_DOWN) handle_key(e.key);
    else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        // Route wheel to detail panel if over that region.
        if (detail_panel_hit(detail_.panel.open, static_cast<float>(win_.width()), e.wheel.mouse_x)) {
            scroll_detail_panel(detail_.panel, e.wheel.y);
            return;
        }

        // Route wheel to saved-panel if over that region (Phase 68 Part 3 Task B).
        const auto W = static_cast<float>(win_.width());
        const auto H = static_cast<float>(win_.height());
        const auto cW = W - detail_panel_width(detail_.panel.open, W);
        const float colW = (cW - 2 * PAD) / 3.0f - 16;
        const float mx = PAD + colW + 24;
        const float saved_x = mx + colW + 24;
        if (e.wheel.mouse_x >= saved_x) {
            saved_panel_.handle_wheel(e.wheel.y, H);
            mark_dirty();
            return;
        }
    }
}

void AdvancedSearchScreen::update(double dt)
{
    if (auto p = ops_.poll(); p.dirty || p.reload || !p.status.empty()) {
        if (!p.status.empty()) { status_ = std::move(p.status); }
        if (p.reload) { rerun(); }   // a Move removed items from the results
        mark_dirty();
    }
    // While a batch worker owns the vault handle the tree must not be walked
    // (no rerun, no detail rebuild, no decode submits) — Phase 68 contract.
    if (ops_.busy()) { return; }

    result_view_.pump_thumbnails();   // upload any off-thread thumb/cover decodes
    if (live_.rerun.fire(dt)) rerun();   // Phase 58: fire debounced query reruns
    if (std::string s; rename_.consume_completed(s)) {
        status_ = std::move(s);
        rerun();   // the renamed result's new name/path must show up
    }
    rebuild_detail();
    mark_dirty();   // mark screen dirty since thumbnails changed
}

// Everything that must follow a change to the active buffer's content. This is
// a free friend so AdvancedSearchScreen stays within Sonar S1448's method cap.
void after_advanced_search_buffer_edit(AdvancedSearchScreen& s)
{
    s.cur_.tag = -1;
    if (!s.saved_panel_.active_buffer() && s.focus_ == AdvancedSearchScreen::Focus::Name) {
        s.query_.name_query = s.edit_.name.str();
        s.live_.rerun.arm();  // Phase 58: debounce name-query reruns to input silence
    }
    s.refresh_suggestions();  // in-memory autocomplete is cheap; stays immediate
}

void AdvancedSearchScreen::handle_clearing_key(const SDL_KeyboardEvent& key)
{
    if (key.key == SDLK_Y || key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) {
        query_ = {};   // reset to a default (empty) query + builder + cursor
        edit_.reset();
        cur_   = {};
        focus_ = Focus::Include;
        clearing_ = false;
        status_   = "Search cleared.";
        refresh_suggestions();
        rerun();
    } else if (key.key == SDLK_N || key.key == SDLK_ESCAPE) {
        clearing_ = false;
        status_   = "Clear cancelled.";
    }
}

void AdvancedSearchScreen::handle_save_mode_key(const SDL_KeyboardEvent& key)
{
    if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) {
        // Phase 58: flush any pending debounced rerun before saving,
        // so the saved search captures the final query after typing.
        if (live_.rerun.armed()) { live_.rerun.cancel(); rerun(); }
        if (saved_panel_.finalize_save(query_)) {
            reload_saved();
        }
    } else if (key.key == SDLK_ESCAPE) {
        status_ = "Save cancelled.";
    }
}

void AdvancedSearchScreen::handle_key(const SDL_KeyboardEvent& key)
{
    // The clear-search confirmation is modal: it swallows every key until the
    // user answers (Y/Enter = clear, N/Esc = cancel).
    if (clearing_) { handle_clearing_key(key); return; }

    // Handle save mode keys (Escape/Enter/Backspace)
    if (saved_panel_.active_buffer()) { handle_save_mode_key(key); return; }

    if (handle_detail_panel_scroll(key, detail_.panel)) { return; }
    if (key.key == SDLK_D && (key.mod & SDL_KMOD_CTRL) != 0) {
        detail_.panel.open = !detail_.panel.open;
        detail_.key.clear();
        return;
    }

    if ((key.mod & SDL_KMOD_CTRL) != 0 && key.key == SDLK_S) {
        saved_panel_.begin_naming();
        return;
    }
    // Phase 93: plain L on Results focus cycles the full view sequence
    // (List -> Grid S/M/L/XL/XXL -> List) and live-saves the shared machine-wide
    // setting — the gallery grid's L-key behaviour. Bare letters elsewhere
    // belong to the builder fields, so L is gated on Results focus (the old
    // Phase 20 Ctrl+L List<->Grid toggle is superseded).
    if (key.key == SDLK_L && focus_ == Focus::Results && !saved_panel_.active_buffer() &&
        !clearing_) {
        auto new_view = next_gallery_view(result_view_.get_view());
        result_view_.set_view(new_view);
        set_gallery_view_setting(new_view);
        (void)platform::GalleryViewPref::default_location().save(new_view);
        status_ = std::format("View: {}", gallery_view_label(new_view));
        return;
    }
    if ((key.mod & SDL_KMOD_CTRL) != 0 && key.key == SDLK_R) {
        clearing_ = true;   // ask before wiping the query (confirmed in the block above)
        return;
    }
    if (focus_ == Focus::Results && key.key == SDLK_R) { start_rename(); return; }
    // Phase 68 batch ops over the result multi-selection (Results focus only —
    // elsewhere these letters belong to the builder fields).
    if (focus_ == Focus::Results && key.key == SDLK_B) { toggle_favorite_results(*this); return; }
    if (focus_ == Focus::Results && key.key == SDLK_X) { start_export_results(*this); return; }
    if (focus_ == Focus::Results && key.key == SDLK_M && (key.mod & SDL_KMOD_SHIFT) == 0) {
        start_transfer_results(*this);
        return;
    }
    if (focus_ == Focus::Results && key.key == SDLK_DELETE) {
        start_delete_results(*this);
        return;
    }

    switch (key.key) {
        case SDLK_ESCAPE:    request(NavKind::ToGallery, "", 0); return;
        case SDLK_TAB:       cycle_focus((key.mod & SDL_KMOD_SHIFT) ? -1 : 1); return;
        case SDLK_BACKSPACE: remove_last_chip(); return;
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
        case Results:   result_view_.handle_key(key); break;
        case Saved: {
            const auto action = saved_panel_.handle_key(key);
            using enum SavedSearchPanel::Action;
            switch (action) {
                case Loaded: {
                    if (AdvancedQuery loaded_query; saved_panel_.load_focused(loaded_query)) {
                        query_ = std::move(loaded_query);
                        edit_.name.set_text(query_.name_query);
                        cur_.group = 0;
                        rerun();
                    }
                    break;
                }
                case Deleted:
                    reload_saved();
                    break;
                case None:
                    break;
            }
            break;
        }
        case Name:      break;   // typing handled via text input
    }
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
    using enum Focus;

    // While typing: Up/Down drive the suggestion dropdown; Enter commits.
    if (const ITextInput* buf = active_buffer(); buf && !buf->empty()) {
        switch (key.key) {
            case SDLK_DOWN:     move_suggestion(1);  break;
            case SDLK_UP:       move_suggestion(-1); break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER: commit_text();       break;
            default:
                if (focus_ == Include) handle_weight_key(key);
                break;
        }
        return;
    }

    // Empty buffer: Up/Down select a committed tag; Enter/Del act on it.
    switch (key.key) {
        case SDLK_DOWN: select_tag(*this, 1);  break;
        case SDLK_UP:   select_tag(*this, -1); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (cur_.tag >= 0) edit_selected_tag(*this);
            else               commit_text();   // empty Enter (Group: new group)
            break;
        case SDLK_DELETE:
            if (cur_.tag >= 0)        remove_selected_tag(*this);
            else if (focus_ == Group) handle_group_nav_key(key);  // toggle AND/OR
            break;
        default:
            if (focus_ == Group)        handle_group_nav_key(key);  // Left/Right switch group
            else if (focus_ == Include) handle_weight_key(key);
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
        case SDLK_LEFT:  cur_.group = (cur_.group - 1 + n) % n; cur_.tag = -1; break;
        case SDLK_RIGHT: cur_.group = (cur_.group + 1) % n;     cur_.tag = -1; break;
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

void AdvancedSearchScreen::cycle_focus(int dir)
{
    constexpr int N = 8;
    const int idx = (static_cast<int>(std::to_underlying(focus_)) + dir % N + N) % N;
    focus_ = static_cast<Focus>(idx);
    cur_.tag = -1;
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
    if (live_.suggestions.empty()) return;
    const auto n = static_cast<int>(live_.suggestions.size());
    cur_.sugg    = (cur_.sugg + dir + n) % n;
}

// --- committed-tag selection (free friends; see header for the S1448 rationale) ---

int current_tag_count(const AdvancedSearchScreen& s)
{
    using enum AdvancedSearchScreen::Focus;
    switch (s.focus_) {
        case Include: return static_cast<int>(s.query_.include.size());
        case Exclude: return static_cast<int>(s.query_.exclude.size());
        case Group:
            if (s.query_.groups.empty()) return 0;
            return static_cast<int>(
                s.query_.groups[std::clamp(s.cur_.group, 0,
                                static_cast<int>(s.query_.groups.size()) - 1)].tags.size());
        default: return 0;
    }
}

void select_tag(AdvancedSearchScreen& s, int dir)
{
    s.cur_.tag = move_tag_cursor(s.cur_.tag, dir, current_tag_count(s));
}

void remove_selected_tag(AdvancedSearchScreen& s)
{
    using enum AdvancedSearchScreen::Focus;
    if (s.cur_.tag < 0 || s.cur_.tag >= current_tag_count(s)) return;
    switch (s.focus_) {
        case Include: s.query_.include.erase(s.query_.include.begin() + s.cur_.tag); break;
        case Exclude: s.query_.exclude.erase(s.query_.exclude.begin() + s.cur_.tag); break;
        case Group: {
            auto& t = s.query_.groups[s.cur_.group].tags;
            t.erase(t.begin() + s.cur_.tag);
            break;
        }
        default: return;
    }
    s.cur_.tag = std::min(s.cur_.tag, current_tag_count(s) - 1);   // -1 if list now empty
    s.rerun();
}

void edit_selected_tag(AdvancedSearchScreen& s)
{
    using enum AdvancedSearchScreen::Focus;
    if (s.cur_.tag < 0 || s.cur_.tag >= current_tag_count(s)) return;
    switch (s.focus_) {
        case Include: {
            const WeightedTag& wt = s.query_.include[s.cur_.tag];
            s.edit_.include.set_text(wt.tag);
            s.edit_.weight  = wt.weight;
            s.query_.include.erase(s.query_.include.begin() + s.cur_.tag);
            break;
        }
        case Exclude:
            s.edit_.exclude.set_text(s.query_.exclude[s.cur_.tag]);
            s.query_.exclude.erase(s.query_.exclude.begin() + s.cur_.tag);
            break;
        case Group: {
            auto& t = s.query_.groups[s.cur_.group].tags;
            s.edit_.group.set_text(t[s.cur_.tag]);
            t.erase(t.begin() + s.cur_.tag);
            break;
        }
        default: return;
    }
    s.cur_.tag = -1;
    s.refresh_suggestions();
    s.rerun();
}

void AdvancedSearchScreen::commit_text()
{
    using enum Focus;
    switch (focus_) {
        case Include:
            if (std::string t = accepted(edit_.include.str()); !t.empty()) {
                // Include/Exclude are mutually exclusive: adding to Include drops any
                // matching Exclude entry. Re-adding an existing Include tag just
                // updates its weight (tags stay unique within the list).
                if (int e = tag_index_ci(query_.exclude, t); e >= 0)
                    query_.exclude.erase(query_.exclude.begin() + e);
                if (int j = weighted_tag_index_ci(query_.include, t); j >= 0)
                    query_.include[j].weight = edit_.weight;
                else
                    query_.include.emplace_back(t, edit_.weight);
            }
            edit_.include.clear(); edit_.weight = 1; refresh_suggestions(); rerun();
            break;
        case Exclude:
            if (std::string t = accepted(edit_.exclude.str()); !t.empty()) {
                // Mutually exclusive with Include; unique within Exclude.
                if (int j = weighted_tag_index_ci(query_.include, t); j >= 0)
                    query_.include.erase(query_.include.begin() + j);
                if (tag_index_ci(query_.exclude, t) < 0)
                    query_.exclude.push_back(std::move(t));
            }
            edit_.exclude.clear(); refresh_suggestions(); rerun();
            break;
        case Group:
            commit_group_text(accepted(edit_.group.str()));
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
    std::vector<std::string>& tags = query_.groups[cur_.group].tags;
    if (tag_index_ci(tags, tag) < 0)   // keep a group's tags unique
        tags.push_back(std::move(tag));
}

// Backspace with an EMPTY builder buffer. A non-empty buffer never reaches
// here: the shared text-input handler already consumed the key (see
// field_owns_event).
void AdvancedSearchScreen::remove_last_chip()
{
    using enum Focus;
    if (active_buffer() == nullptr || saved_panel_.active_buffer()) return;

    // A tag is selected: remove exactly that tag.
    if ((focus_ == Include || focus_ == Exclude || focus_ == Group) && cur_.tag >= 0) {
        remove_selected_tag(*this);
        return;
    }

    // Otherwise a Backspace removes the last committed chip of the field.
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

// --- rendering --------------------------------------------------------------

void AdvancedSearchScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    // Reserve space for the detail panel; use the reduced width for all layout
    const auto cW = W - detail_panel_width(detail_.panel.open, W);

    r.draw_text(font_, PAD, 36, "Advanced Search", TEXT);
    r.draw_text(font_, PAD, 74, "[F1] Help", TEXT_FAINT);

    // While a batch worker owns the vault handle, drawing results would submit
    // thumbnail decodes against it — draw only the chrome + the modal (Phase 68).
    if (ops_.busy()) {
        ops_.render(r, font_, W, H);
        return;
    }

    const float colW = (cW - 2 * PAD) / 3.0f - 16;
    render_builder(r, PAD, TOP, colW);
    const float mx = PAD + colW + 24;
    render_results(r, mx, colW);
    const bool saved_hot = (focus_ == Focus::Saved && !saved_panel_.active_buffer() && !clearing_);
    const float saved_x = mx + colW + 24;
    saved_panel_.render(r, saved_x, cW - saved_x - PAD, H, saved_hot);

    if (saved_panel_.active_buffer()) {
        r.draw_rect({0, 0, W, H}, {0, 0, 0, 180}, /*filled*/ true);
        const SDL_FRect box{W / 2 - 220, H / 2 - 40, 440, 80};
        r.draw_round_rect(box, RADIUS, SURFACE);
        r.draw_round_rect(box, RADIUS, ACCENT, /*filled*/ false);
        r.draw_text(font_, box.x + 16, box.y + 14, "Save search as:", TEXT_DIM);
        draw_inline_edit_text(r, font_, box.x + 16, box.y + 44, box.w - 32,
                              *saved_panel_.active_buffer(), saved_panel_.save_chrome());
    }
    if (clearing_) {
        r.draw_rect({0, 0, W, H}, {0, 0, 0, 180}, /*filled*/ true);
        const SDL_FRect box{W / 2 - 220, H / 2 - 40, 440, 80};
        r.draw_round_rect(box, RADIUS, SURFACE);
        r.draw_round_rect(box, RADIUS, ACCENT, /*filled*/ false);
        // Both lines are elided to the box: it is a fixed 440 px wide, and the
        // prompt is wider than that at the app's font.
        const float box_text_w = box.w - 32;
        r.draw_text(font_, box.x + 16, box.y + 14,
                    fit_text(font_, "Clear search? Resets all parameters.", box_text_w), TEXT_DIM);
        r.draw_text(font_, box.x + 16, box.y + 44,
                    fit_text(font_, "[Y] Yes      [N] No", box_text_w), TEXT);
    }
    if (!status_.empty()) r.draw_text(font_, PAD, H - 24, status_, TEXT_FAINT);

    if (const float pw = detail_panel_width(detail_.panel.open, W);
        pw > 0.0f) {
        const SDL_FRect panel{W - pw, 0.0f, pw, H};
        detail_.content_h = draw_detail_panel(r, font_, panel, detail_.content, detail_.panel.scroll,
                                              vault::vault_settings(vault_).categories);
        detail_.panel.scroll = ui::clamp_scroll(detail_.panel.scroll, detail_.content_h, H);
    }

    rename_.render(r, font_, W, H);
    ops_.render(r, font_, W, H);
}

void AdvancedSearchScreen::render_builder(gfx::Renderer& r, float x, float top, float colw)
{
    using namespace gfx::theme;
    const float LINE = line_pitch(font_.pixel_height());
    const float ROW  = LINE;   // per-tag rows are full lines now, not 0.85 of one
    auto focused = [&](Focus f) { return focus_ == f && !saved_panel_.active_buffer() && !clearing_; };
    auto label   = [&](float ly, Focus f, std::string_view s) {
        if (focused(f)) r.draw_text(font_, x - 16, ly, ">", ACCENT);
        r.draw_text(font_, x, ly, s, focused(f) ? TEXT : TEXT_DIM);
    };
    // Offset from a draw-y (glyph-cell top) to the text's ink centre, so a highlight
    // box can be centred on the text it wraps (the text itself stays aligned with the
    // field labels above it).
    const float ink_dy = -font_.text_top_for_center(0.0f);
    // The autocomplete dropdown should float at this y; set when edit row is drawn.
    float drop_y = -1.0f;

    float y = top;
    // "Name:" stays a label; the value beside it becomes a real editable run
    // (caret + selection) while the field is focused.
    const auto name_w = static_cast<float>(font_.measure("Name: "));
    label(y, Focus::Name, "Name:");
    if (focused(Focus::Name))
        draw_inline_edit_text(r, font_, x + name_w, y, colw - name_w, edit_.name, edit_chrome_.name);
    else
        r.draw_text(font_, x + name_w, y,
                    fit_text(font_, edit_.name.empty() ? "(any)" : edit_.name.str(), colw - name_w),
                    TEXT_DIM);
    y += LINE;
    label(y, Focus::Scope, std::format("Scope: {}", scope_label(query_.scope)));                 y += LINE;

    // --- Include (weighted) ---
    label(y, Focus::Include, "Include:"); y += ROW;
    y = render_include_section(r, x, y, colw, ROW, ink_dy, drop_y);

    // --- Exclude ---
    label(y, Focus::Exclude, "Exclude:"); y += ROW;
    y = render_exclude_section(r, x, y, colw, ROW, ink_dy);

    // --- Groups ---
    label(y, Focus::Group, "Groups:"); y += ROW;
    y = draw_groups(r, font_, query_.groups,
                    {.cur_group = cur_.group,
                     .sel_tag   = focused(Focus::Group) ? cur_.tag : -1,
                     .focused   = focused(Focus::Group),
                     .x = x, .colw = colw, .y = y});
    if (focused(Focus::Group)) {
        draw_inline_edit_text(r, font_, x + 24, y, colw - 24, edit_.group, edit_chrome_.group);
        y += ROW;
        r.draw_text(font_, x + 8, y, "Enter=add  empty Enter=new group  Del=AND/OR", TEXT_FAINT);
        y += ROW;
        if (!live_.suggestions.empty()) drop_y = y;
    }

    label(y, Focus::GroupJoin, std::format("Join groups: {}", join_label(query_.group_join)));

    // Floating autocomplete dropdown, painted on top of everything above.
    if (drop_y >= 0.0f && !live_.suggestions.empty())
        draw_dropdown(r, font_, live_.suggestions, cur_.sugg, x, drop_y, colw);
}

void AdvancedSearchScreen::render_results(gfx::Renderer& r, float x, float colw)
{
    using namespace gfx::theme;
    const float LINE = line_pitch(font_.pixel_height());
    const bool hot = (focus_ == Focus::Results && !saved_panel_.active_buffer() && !clearing_);
    if (result_view_.get_view() != GalleryView::List) {
        result_view_.render(r, x, colw, hot);
        return;
    }

    // List view rendering (when not in a grid density mode)
    if (hot) r.draw_text(font_, x - 16, TOP, ">", ACCENT);

    // Phase 68: Build results header with position counter if available
    std::string header = std::format("Results ({})", result_view_.get_results().size());
    if (const std::string pos = ui::position_label(result_view_.get_cursor(),
                                                   result_view_.get_results().size());
        !pos.empty()) {
        header += " · " + pos;
    }
    r.draw_text(font_, x, TOP, header, TEXT_DIM);

    const auto  H        = static_cast<float>(win_.height());
    const float rh       = LINE * 0.9f;
    const float ink_dy   = -font_.text_top_for_center(0.0f);  // draw-top -> ink centre
    float       y        = TOP + LINE;
    const auto  max_rows = static_cast<int>((H - y - 40) / rh);
    const int   cur      = result_view_.get_cursor();
    const int   first    = std::max(0, cur - max_rows / 2);
    for (int i = first; i < static_cast<int>(result_view_.get_results().size()) && i < first + max_rows; ++i) {
        const bool              sel = (i == cur && hot);
        const vault::SearchHit& hit = result_view_.get_results()[i];
        if (sel) r.draw_round_rect({x - 6, y + ink_dy - rh * 0.5f, colw + 12, rh}, RADIUS_SMALL, SURFACE_HI);
        // Multi-select marker (Phase 68): a small accent square in the row gutter.
        if (result_view_.selection().contains(i)) {
            r.draw_round_rect({x - 14, y + ink_dy - 5.0f, 10, 10}, RADIUS_SMALL, ACCENT);
        }
        r.draw_text(font_, x, y,
                    fit_text(font_, std::format("{}{}", hit.is_gallery ? "[D] " : "    ", hit.path),
                             colw),
                    sel ? TEXT : TEXT_DIM);
        y += rh;
    }
    if (result_view_.get_results().empty()) r.draw_text(font_, x, y, "(no matches)", TEXT_FAINT);
}

float AdvancedSearchScreen::render_include_section(gfx::Renderer& r, float x, float y, float colw,
                                                   float row_h, float ink_dy, float& drop_y)
{
    using namespace gfx::theme;
    const float ROW = row_h;
    const bool is_focused = focus_ == Focus::Include && !saved_panel_.active_buffer() && !clearing_;

    for (int i = 0; i < static_cast<int>(query_.include.size()); ++i) {
        const float row_y = list_row_y({.top = y, .row_h = ROW}, i);
        const bool sel = is_focused && cur_.tag == i;
        if (sel) {
            r.draw_round_rect({x - 6, row_y + ink_dy - ROW * 0.5f, colw + 12, ROW},
                              RADIUS_SMALL, SURFACE_HI);
            r.draw_text(font_, x + 4, row_y, ">", ACCENT);
        }
        r.draw_text(font_, x + 24, row_y,
                    fit_text(font_, std::format("{} (w{})", query_.include[i].tag,
                                               query_.include[i].weight),
                             colw - 24),
                    sel ? TEXT : TEXT_DIM);
    }
    if (is_focused) {
        // The weight is right-aligned so it cannot be pushed off-screen by a
        // long tag, and so it never sits where the caret needs to be.
        const std::string wlabel = std::format("w{}", edit_.weight);
        const auto        wlab_w = static_cast<float>(font_.measure(wlabel));
        const float edit_y = list_row_y({.top = y, .row_h = ROW},
                                        static_cast<int>(query_.include.size()));
        r.draw_text(font_, x + colw - wlab_w, edit_y, wlabel, TEXT_FAINT);
        draw_inline_edit_text(r, font_, x + 24, edit_y, colw - 24, edit_.include,
                              edit_chrome_.include);
        y = edit_y + ROW;
        if (!live_.suggestions.empty()) drop_y = y;
    } else {
        y = list_row_y({.top = y, .row_h = ROW},
                       static_cast<int>(query_.include.size()));
    }
    return y;
}

float AdvancedSearchScreen::render_exclude_section(gfx::Renderer& r, float x, float y, float colw,
                                                   float row_h, float ink_dy)
{
    using namespace gfx::theme;
    const float ROW = row_h;
    const bool is_focused = focus_ == Focus::Exclude && !saved_panel_.active_buffer() && !clearing_;

    for (int i = 0; i < static_cast<int>(query_.exclude.size()); ++i) {
        const float row_y = list_row_y({.top = y, .row_h = ROW}, i);
        const bool sel = is_focused && cur_.tag == i;
        if (sel) {
            r.draw_round_rect({x - 6, row_y + ink_dy - ROW * 0.5f, colw + 12, ROW},
                              RADIUS_SMALL, SURFACE_HI);
            r.draw_text(font_, x + 4, row_y, ">", ACCENT);
        }
        r.draw_text(font_, x + 24, row_y, fit_text(font_, query_.exclude[i], colw - 24),
                    sel ? TEXT : TEXT_DIM);
    }
    if (is_focused) {
        const float edit_y = list_row_y({.top = y, .row_h = ROW},
                                        static_cast<int>(query_.exclude.size()));
        draw_inline_edit_text(r, font_, x + 24, edit_y, colw - 24, edit_.exclude,
                              edit_chrome_.exclude);
        y = edit_y + ROW;
    } else {
        y = list_row_y({.top = y, .row_h = ROW},
                       static_cast<int>(query_.exclude.size()));
    }
    return y;
}

std::vector<ui::HelpGroup> AdvancedSearchScreen::help_groups() const
{
    return {
        {"Build query",
         {
             {"Tab", "Next field"},
             {"Up/Down/Left/Right", "Navigate"},
             {"Enter", "Add term / open result"},
             {"+ / -", "Adjust weight"},
             {"Del", "Remove term"},
             {"Backspace", "Edit term"},
         }},
        {"Results & saved searches",
         {
             {"Ctrl+S", "Save search"},
             {"L", "Cycle view: list / grid size"},
             {"Ctrl+R", "Clear query"},
             {"R", "Rename focused result"},
             {"Space", "Select result"},
             {"Shift+Space", "Select range"},
             {"Ctrl+A", "Select all results"},
             {"B", "Favorite (acts on selection)"},
             {"X", "Export selection"},
             {"M", "Move/copy selection"},
             {"Del", "Delete selection"},
             {"Ctrl+D", "Toggle the detail panel"},
         }},
        {"Navigate", {{"Esc", "Back"}}},
        text_editing_help_group(),
    };
}

} // namespace ui
