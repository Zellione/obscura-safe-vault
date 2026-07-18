#include "ui/tag_editor.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/advanced_search_model.h"
#include "ui/nav_model.h"
#include "ui/tag_inherit.h"
#include "ui/tag_scroll.h"
#include "ui/tag_suggest.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"
#include "vault/vault_search.h"

namespace ui {

namespace {
constexpr float MODAL_W = 500.0f;
constexpr float MODAL_H = 450.0f;
constexpr float PAD = 16.0f;
constexpr float INPUT_BOX_H = 40.0f;
constexpr float TAG_ROW_H = 32.0f;
constexpr float TAG_LIST_GAP = 6.0f;
// Baseline-to-baseline spacing for the 28px UI font; 24px was too tight and
// caused the title/subtitle and the "Current tags:" label/first row to collide.
constexpr float LINE = 34.0f;

// Trim surrounding ASCII whitespace only; interior spaces are kept so multi-word
// tags survive (the vault performs the canonical normalisation/dedup).
std::string trim_surrounding(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos) return {};
    return std::string(s.substr(first, s.find_last_not_of(" \t\n\r") - first + 1));
}

// Greedy-pack tags into comma-separated display lines no wider than `max_w`
// (a single overlong tag still gets its own line and clips). Returns at most
// `max_lines` lines plus how many tags they show — the render header reports
// the rest as hidden, since the inherited section is informational, not a list
// the user navigates.
struct PackedLines {
    std::vector<std::string> lines;
    int                      shown = 0;
};

PackedLines pack_tag_lines(const std::vector<std::string>& tags,
                           const gfx::FontAtlas& font, float max_w, int max_lines)
{
    PackedLines out;
    std::string cur;
    int cur_count = 0;
    for (const std::string& tag : tags) {
        if (const std::string cand = cur.empty() ? tag : cur + ", " + tag;
            cur.empty() || static_cast<float>(font.measure(cand)) <= max_w) {
            cur = cand;
            ++cur_count;
            continue;
        }
        out.lines.push_back(cur);
        out.shown += cur_count;
        if (static_cast<int>(out.lines.size()) == max_lines) return out;  // rest hidden
        cur = tag;
        cur_count = 1;
    }
    if (!cur.empty()) {
        out.lines.push_back(cur);
        out.shown += cur_count;
    }
    return out;
}
}

void TagEditor::open(std::string node_path)
{
    open_multi({std::move(node_path)});
}

void TagEditor::open_multi(std::vector<std::string> node_paths)
{
    active_     = true;
    node_paths_ = std::move(node_paths);
    selected_   = 0;
    new_tag_buf_.clear();
    suggestions_.clear();
    sugg_sel_ = -1;
    error_.clear();
    refresh_tags();
    refresh_vocabulary();
    SDL_StartTextInput(win_.sdl_window());
}

void TagEditor::close()
{
    active_ = false;
    SDL_StopTextInput(win_.sdl_window());
    node_paths_.clear();
    tally_.clear();
    inherited_.clear();
    vocabulary_.clear();
    suggestions_.clear();
    sugg_sel_ = -1;
    new_tag_buf_.clear();
    error_.clear();
}

void TagEditor::refresh_vocabulary()
{
    // Vault-wide distinct tags (galleries + images) feeding the autosuggest
    // dropdown (Phase 29). Re-fetched after every add/remove so a just-created
    // tag is immediately suggestible on the next node.
    vocabulary_ = vault::VaultSearch(vault_).all_tags();
}

void TagEditor::refresh_suggestions()
{
    std::vector<std::string> current_tags;
    for (const auto& entry : tally_) {
        current_tags.push_back(entry.tag);
    }
    suggestions_ = editor_tag_suggestions(new_tag_buf_, vocabulary_, current_tags);
    sugg_sel_    = -1;   // typing always returns focus to the buffer
}

void TagEditor::add_chosen_tag()
{
    // The typed text wins unless a suggestion is explicitly highlighted
    // (Phase 29). Trim only SURROUNDING whitespace so multi-word tags
    // ("new york") survive; the vault owns normalisation/dedup.
    const bool from_sugg =
        sugg_sel_ >= 0 && sugg_sel_ < static_cast<int>(suggestions_.size());
    const std::string chosen =
        from_sugg ? suggestions_[sugg_sel_] : trim_surrounding(new_tag_buf_);
    if (chosen.empty()) { return; }

    using enum vault::VaultResult;
    int failures = 0;
    for (const std::string& path : node_paths_) {
        if (vault_.add_tag(path, chosen) != Ok) { ++failures; }
    }

    if (failures == static_cast<int>(node_paths_.size())) {
        error_ = "Failed to add tag.";
        return;
    }
    new_tag_buf_.clear();
    error_.clear();
    refresh_tags();
    refresh_vocabulary();
    refresh_suggestions();   // buffer is empty → dropdown closes
    select_tag(chosen);      // scroll the list to reveal it
}

void TagEditor::move_cursor(int dir)
{
    // While the dropdown is showing, Up/Down drive the suggestion highlight
    // (-1 = back to the buffer); otherwise they scroll the node's own tag list.
    if (!suggestions_.empty()) {
        sugg_sel_ = move_tag_cursor(sugg_sel_, dir, static_cast<int>(suggestions_.size()));
        return;
    }
    const int next = selected_ + dir;
    if (next >= 0 && next < static_cast<int>(tally_.size())) {
        selected_ = next;
        error_.clear();
    }
}

void TagEditor::refresh_tags()
{
    tally_.clear();
    inherited_.clear();
    error_.clear();

    if (node_paths_.empty()) {
        error_ = "Invalid node path.";
        return;
    }

    // The ancestor-tag cascade is only meaningful for a single node — a
    // multi-selection can span unrelated branches of the tree, so it's
    // suppressed there rather than showing a misleading merged cascade.
    if (node_paths_.size() == 1) { inherited_ = inherited_tags(vault_, node_paths_.front()); }

    std::vector<std::vector<std::string>> per_node_tags;
    per_node_tags.reserve(node_paths_.size());
    int resolved = 0;
    for (const std::string& path : node_paths_) {
        const auto segs = split_path(path);
        if (segs.empty()) { continue; }
        std::string parent_path;
        if (segs.size() > 1) { parent_path = join_path(std::span(segs.data(), segs.size() - 1)); }

        const auto& children = vault_.list(parent_path);
        for (const auto* child : children) {
            if (child->name == segs.back()) {
                per_node_tags.push_back(child->tags);
                ++resolved;
                break;
            }
        }
    }
    if (resolved == 0) { error_ = "Node not found."; return; }
    tally_ = compute_tag_tally(per_node_tags);
}

void TagEditor::select_tag(std::string_view tag)
{
    // Case-insensitive find so the just-added/merged tag is highlighted and the
    // render window scrolls to reveal it. Falls back to the last row.
    for (int i = 0; i < static_cast<int>(tally_.size()); ++i)
        if (tag_ci_equal(tally_[i].tag, tag)) { selected_ = i; return; }
    selected_ = std::max(0, static_cast<int>(tally_.size()) - 1);
}

bool TagEditor::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    if (e.type == SDL_EVENT_TEXT_INPUT) {
        new_tag_buf_ += e.text.text;
        refresh_suggestions();
        return true;
    }

    if (e.type != SDL_EVENT_KEY_DOWN) return false;

    switch (e.key.key) {
        case SDLK_ESCAPE:
            // First Esc deselects a highlighted suggestion (back to the typed
            // buffer); only an Esc with nothing highlighted closes the modal.
            if (sugg_sel_ >= 0) {
                sugg_sel_ = -1;
                return true;
            }
            close();
            return true;

        case SDLK_BACKSPACE:
            if (!new_tag_buf_.empty()) {
                new_tag_buf_.pop_back();
                refresh_suggestions();
                error_.clear();
            }
            return true;

        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            add_chosen_tag();
            return true;

        case SDLK_UP:
            move_cursor(-1);
            return true;

        case SDLK_DOWN:
            move_cursor(1);
            return true;

        case SDLK_DELETE: {
            // Delete the selected tag from every selected node that carries it.
            // remove_tag is idempotent, so calling it on a node that doesn't
            // have the tag is a harmless no-op — no per-node tally check needed.
            if (selected_ >= 0 && selected_ < static_cast<int>(tally_.size())) {
                const std::string tag_to_remove = tally_[selected_].tag;
                bool any_ok = false;
                for (const std::string& path : node_paths_) {
                    if (vault_.remove_tag(path, tag_to_remove) == vault::VaultResult::Ok) { any_ok = true; }
                }
                if (any_ok) {
                    refresh_tags();
                    refresh_vocabulary();
                    refresh_suggestions();   // the removed tag is suggestible again
                    selected_ = std::min(selected_, static_cast<int>(tally_.size()) - 1);
                    error_.clear();
                } else {
                    error_ = "Failed to remove tag.";
                }
            }
            return true;
        }

        default:
            return false;
    }
}

void TagEditor::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
{
    if (!active_) return;

    using namespace gfx::theme;

    // Dim the background
    r.draw_rect({0, 0, W, H}, {0, 0, 0, 180}, /*filled*/ true);

    // Modal panel background
    const float mx = (W - MODAL_W) / 2;
    const float my = (H - MODAL_H) / 2;
    r.draw_round_rect({mx, my, MODAL_W, MODAL_H}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, MODAL_W, MODAL_H}, RADIUS, ACCENT, /*filled*/ false);

    // Title
    r.draw_text(font, mx + PAD, my + PAD, "Edit Tags", TEXT);

    // Node path display (secondary text): the path in single-node mode, or a
    // "3 items selected" summary in multi-node mode.
    const std::string subtitle = node_paths_.size() == 1
        ? node_paths_.front()
        : std::format("{} items selected", node_paths_.size());
    r.draw_text(font, mx + PAD, my + PAD + LINE,
                fit_text(font, subtitle, MODAL_W - 2 * PAD), TEXT_FAINT);

    // Input box for new tag
    const float input_y = my + PAD + 2 * LINE + 8;
    const SDL_FRect input_box{mx + PAD, input_y, MODAL_W - 2 * PAD, INPUT_BOX_H};
    r.draw_round_rect(input_box, gfx::theme::RADIUS_SMALL, gfx::theme::SURFACE);
    r.draw_round_rect(input_box, gfx::theme::RADIUS_SMALL, gfx::theme::ACCENT,
                      /*filled*/ false);
    r.draw_text(font, input_box.x + 8,
                font.text_top_for_center(input_box.y + input_box.h * 0.5f),
                new_tag_buf_, TEXT);

    // Current tags list — scrolls to keep the selected (and newly-added) tag
    // visible. A node can hold far more tags than fit the fixed-height modal, so
    // we render a window that follows `selected_` (Phase 21 fix). Indicators use
    // ASCII only (the baked font atlas covers printable ASCII).
    const float list_y     = input_y + INPUT_BOX_H + 16;
    const float tags_start = list_y + LINE;
    const float row_pitch  = TAG_ROW_H + TAG_LIST_GAP;
    const auto  total      = static_cast<int>(tally_.size());

    // The read-only inherited section (ancestor-gallery tags, Phase 27
    // follow-up) reserves space at the bottom; the own-tags list shrinks to fit.
    constexpr int   INHERIT_MAX_LINES = 3;
    constexpr float INHERIT_LINE      = 30.0f;
    const PackedLines inh = inherited_.empty()
        ? PackedLines{}
        : pack_tag_lines(inherited_, font, MODAL_W - 2 * PAD, INHERIT_MAX_LINES);
    const float inherit_h = inherited_.empty()
        ? 0.0f
        : INHERIT_LINE * static_cast<float>(1 + inh.lines.size()) + 8.0f;
    const float list_bottom = my + MODAL_H - 50 - inherit_h;

    const int   max_visible =
        std::max(1, static_cast<int>((list_bottom - tags_start) / row_pitch));
    const int   first = tag_scroll_first(total, selected_, max_visible);
    const int   last  = std::min(total, first + max_visible);

    // Header shows the visible range / count so hidden tags are discoverable.
    std::string header = "Current tags";
    if (total > max_visible)
        header += std::format(" ({}-{} of {})", first + 1, last, total);
    else if (total > 0)
        header += std::format(" ({})", total);
    header += ":";
    r.draw_text(font, mx + PAD, list_y, header, TEXT_DIM);

    for (int i = first; i < last; ++i) {
        const float row_y = tags_start + static_cast<float>(i - first) * row_pitch;
        const SDL_FRect tag_rect{mx + PAD, row_y, MODAL_W - 2 * PAD, TAG_ROW_H};

        if (i == selected_) {
            r.draw_selection_glow(tag_rect, RADIUS_SMALL, ACCENT);
            r.draw_round_rect(tag_rect, RADIUS_SMALL, SURFACE_HI);
        } else {
            r.draw_round_rect(tag_rect, RADIUS_SMALL, BORDER, /*filled*/ false);
        }

        // Multi-node mode annotates each tag with how many of the selected
        // nodes carry it; single-node mode stays exactly as before.
        const std::string label = node_paths_.size() == 1
            ? tally_[i].tag
            : std::format("{} ({}/{})", tally_[i].tag, tally_[i].count, node_paths_.size());
        const std::string display = fit_text(font, label + " [Delete]", tag_rect.w - 16);
        const float text_y =
            font.text_top_for_center(tag_rect.y + tag_rect.h * 0.5f);
        r.draw_text(font, tag_rect.x + 8, text_y, display, TEXT);
    }

    if (tally_.empty()) {
        r.draw_text(font, mx + PAD, tags_start, "(no tags)", TEXT_FAINT);
    }

    // Inherited section: informational only (Del/selection never touch it —
    // removing one of these means editing the ancestor gallery it lives on).
    if (!inherited_.empty()) {
        float y = list_bottom + 8.0f;
        std::string inh_header = "Inherited from gallery";
        if (inh.shown < static_cast<int>(inherited_.size()))
            inh_header += std::format(" ({} of {} shown)", inh.shown, inherited_.size());
        inh_header += ":";
        r.draw_text(font, mx + PAD, y, inh_header, TEXT_DIM);
        for (const std::string& line : inh.lines) {
            y += INHERIT_LINE;
            r.draw_text(font, mx + PAD, y, fit_text(font, line, MODAL_W - 2 * PAD),
                        TEXT_FAINT);
        }
    }

    // Error message
    if (!error_.empty()) {
        r.draw_text(font, mx + PAD, my + MODAL_H - 32, error_, DANGER);
    }

    // Footer hint
    r.draw_text(font, mx + PAD, my + MODAL_H - 12,
                "[Enter] Add  [Up/Down] Scroll  [Del] Remove  [Esc] Close",
                TEXT_FAINT);

    // Autosuggest dropdown (Phase 29) — drawn last so it overlays the tag list
    // like a combobox. Up/Down move the highlight; Enter adds the highlighted
    // suggestion (or, with none highlighted, exactly the typed text).
    if (!suggestions_.empty()) {
        constexpr float SUGG_ROW = 30.0f;
        const SDL_FRect drop{mx + PAD, input_y + INPUT_BOX_H + 4,
                             MODAL_W - 2 * PAD,
                             SUGG_ROW * static_cast<float>(suggestions_.size()) + 8};
        r.draw_round_rect(drop, RADIUS_SMALL, SURFACE_HI);
        r.draw_round_rect(drop, RADIUS_SMALL, ACCENT, /*filled*/ false);
        for (int i = 0; i < static_cast<int>(suggestions_.size()); ++i) {
            const bool  sel   = i == sugg_sel_;
            const float row_y = drop.y + 4 + SUGG_ROW * static_cast<float>(i);
            const float ty    = font.text_top_for_center(row_y + SUGG_ROW * 0.5f);
            r.draw_text(font, drop.x + 10, ty,
                        fit_text(font,
                                 std::format("{} {}", sel ? ">" : " ", suggestions_[i]),
                                 drop.w - 20),
                        sel ? TEXT : TEXT_DIM);
        }
    }
}

} // namespace ui
