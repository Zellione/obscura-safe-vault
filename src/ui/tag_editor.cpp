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
#include "ui/tag_category.h"
#include "ui/tag_chip.h"
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
constexpr float INHERIT_LINE = 30.0f;   // line pitch within the inherited-tags section

// Trim surrounding ASCII whitespace only; interior spaces are kept so multi-word
// tags survive (the vault performs the canonical normalisation/dedup).
std::string trim_surrounding(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos) return {};
    return std::string(s.substr(first, s.find_last_not_of(" \t\n\r") - first + 1));
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
    from_contents_.clear();
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
    from_contents_.clear();
    error_.clear();

    if (node_paths_.empty()) {
        error_ = "Invalid node path.";
        return;
    }

    // The ancestor-tag cascade is only meaningful for a single node — a
    // multi-selection can span unrelated branches of the tree, so it's
    // suppressed there rather than showing a misleading merged cascade.
    if (node_paths_.size() == 1) {
        inherited_ = inherited_tags(vault_, node_paths_.front());

        // The descendant-tag union is also single-node only, and galleries only
        const auto segs = split_path(node_paths_.front());
        if (!segs.empty()) {
            std::string parent_path;
            if (segs.size() > 1) { parent_path = join_path(std::span(segs.data(), segs.size() - 1)); }

            const auto& children = vault_.list(parent_path);
            for (const auto* child : children) {
                if (child && child->name == segs.back() && child->is_gallery()) {
                    from_contents_ = contents_tags(vault_, node_paths_.front());
                    break;
                }
            }
        }
    }

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

        case SDLK_DELETE:
            delete_selected_tag();
            return true;

        default:
            return false;
    }
}

void TagEditor::delete_selected_tag()
{
    // Delete the selected tag from every selected node that carries it.
    // remove_tag is idempotent, so calling it on a node that doesn't
    // have the tag is a harmless no-op — no per-node tally check needed.
    if (selected_ < 0 || selected_ >= static_cast<int>(tally_.size())) return;

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

    // The read-only inherited section (ancestor-gallery tags, Phase 27
    // follow-up) and from-contents section (Phase 51) reserve space at the bottom;
    // the own-tags list shrinks to fit.
    constexpr int   INHERIT_MAX_LINES = 3;
    const auto& cats = vault::vault_settings(vault_).categories;

    // Compute chip wrapping for inherited tags to get height
    const ChipWrap wrap_inherited = inherited_.empty()
        ? ChipWrap{}
        : [this, &font, &cats]() {
            std::vector<int> widths;
            widths.reserve(inherited_.size());
            for (const std::string& t : inherited_) {
                widths.push_back(chip_width(font, resolve_tag(t, cats).text));
            }
            return pack_chip_lines(widths, MODAL_W - 2 * PAD, INHERIT_MAX_LINES,
                                   static_cast<float>(font.measure(std::format("+{}", inherited_.size()))));
        }();
    const float inherit_h = inherited_.empty()
        ? 0.0f
        : INHERIT_LINE * static_cast<float>(1 + wrap_inherited.lines.size()) + 8.0f;

    // Compute chip wrapping for from-contents tags to get height
    const ChipWrap wrap_from_contents = from_contents_.empty()
        ? ChipWrap{}
        : [this, &font, &cats]() {
            std::vector<int> widths;
            widths.reserve(from_contents_.size());
            for (const std::string& t : from_contents_) {
                widths.push_back(chip_width(font, resolve_tag(t, cats).text));
            }
            return pack_chip_lines(widths, MODAL_W - 2 * PAD, INHERIT_MAX_LINES,
                                   static_cast<float>(font.measure(std::format("+{}", from_contents_.size()))));
        }();
    const float from_contents_h = from_contents_.empty()
        ? 0.0f
        : INHERIT_LINE * static_cast<float>(1 + wrap_from_contents.lines.size()) + 8.0f;

    const float list_bottom = my + MODAL_H - 50 - inherit_h - from_contents_h;

    const int max_visible =
        std::max(1, static_cast<int>((list_bottom - tags_start) / row_pitch));

    draw_tag_rows(r, font, mx, list_y, tags_start, row_pitch, max_visible);
    draw_inherited_tags(r, font, mx, list_bottom, wrap_inherited);
    draw_from_contents_tags(r, font, mx, list_bottom + inherit_h, wrap_from_contents);

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
    draw_suggestions_dropdown(r, font, mx, input_y);
}

void TagEditor::draw_tag_rows(gfx::Renderer& r, gfx::FontAtlas& font, float mx, float list_y,
                               float tags_start, float row_pitch, int max_visible) const
{
    using namespace gfx::theme;

    const auto total = static_cast<int>(tally_.size());
    const int  first  = tag_scroll_first(total, selected_, max_visible);
    const int  last   = std::min(total, first + max_visible);
    const auto& cats = vault::vault_settings(vault_).categories;

    // Header shows the visible range / count so hidden tags are discoverable.
    std::string header = "Current tags";
    if (total > max_visible) {
        header += std::format(" ({}-{} of {})", first + 1, last, total);
    } else if (total > 0) {
        header += std::format(" ({})", total);
    }
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

        // Multi-node mode: reserve space for the count suffix and draw it after the chip
        auto avail = tag_rect.w - 16;
        std::string count_str;
        if (node_paths_.size() != 1) {
            count_str = std::format("({}/{})", tally_[i].count, node_paths_.size());
            avail -= CHIP_SPACING + static_cast<float>(font.measure(count_str));
        }

        // Draw the chip
        const auto chip_y = tag_rect.y + (tag_rect.h - CHIP_ROW_H) * 0.5f;
        draw_tag_chips(r, font, tag_rect.x + 8, chip_y, avail, std::span(&tally_[i].tag, 1), cats);

        // Draw the count suffix in multi-node mode
        if (!count_str.empty()) {
            const auto count_x = tag_rect.x + 8 + static_cast<float>(chip_width(font, resolve_tag(tally_[i].tag, cats).text)) + CHIP_SPACING;
            const auto text_y = font.text_top_for_center(tag_rect.y + tag_rect.h * 0.5f);
            r.draw_text(font, count_x, text_y, count_str, TEXT_DIM);
        }
    }

    if (tally_.empty()) {
        r.draw_text(font, mx + PAD, tags_start, "(no tags)", TEXT_FAINT);
    }
}

void TagEditor::draw_inherited_tags(gfx::Renderer& r, gfx::FontAtlas& font, float mx,
                                     float list_bottom, const ChipWrap& wrap) const
{
    // Informational only (Del/selection never touch it — removing one of
    // these means editing the ancestor gallery it lives on).
    if (inherited_.empty()) return;

    using namespace gfx::theme;

    const auto& cats = vault::vault_settings(vault_).categories;
    const int shown_count = static_cast<int>(inherited_.size()) - wrap.hidden;

    float y = list_bottom + 8.0f;
    std::string inh_header = "Inherited from gallery";
    if (shown_count < static_cast<int>(inherited_.size())) {
        inh_header += std::format(" ({} of {} shown)", shown_count, inherited_.size());
    }
    inh_header += ":";
    r.draw_text(font, mx + PAD, y, inh_header, TEXT_DIM);
    for (const auto& line : wrap.lines) {
        y += INHERIT_LINE;
        const auto tags_slice = std::span(inherited_).subspan(static_cast<size_t>(line.first),
                                                               static_cast<size_t>(line.count));
        draw_tag_chips(r, font, mx + PAD, y, MODAL_W - 2 * PAD, tags_slice, cats);
    }
    if (wrap.hidden > 0) {
        const std::string counter   = std::format("+{}", wrap.hidden);
        const auto        counter_w = static_cast<float>(font.measure(counter));
        // Sit on the same centre line draw_tag_chips uses, so the counter aligns
        // with the chips beside it instead of riding high on their row. With no
        // line to sit beside it stays on the header's baseline.
        const float counter_y =
            wrap.lines.empty() ? y : font.text_top_for_center(y + CHIP_ROW_H * 0.5f);
        r.draw_text(font, mx + PAD + (MODAL_W - 2 * PAD) - counter_w, counter_y, counter,
                    TEXT_FAINT);
    }
}

void TagEditor::draw_from_contents_tags(gfx::Renderer& r, gfx::FontAtlas& font, float mx,
                                         float list_bottom, const ChipWrap& wrap) const
{
    // Informational only (Del/selection never touch it — removing one of
    // these means editing a descendant node it lives on).
    if (from_contents_.empty()) return;

    using namespace gfx::theme;

    const auto& cats = vault::vault_settings(vault_).categories;
    const int shown_count = static_cast<int>(from_contents_.size()) - wrap.hidden;

    float y = list_bottom + 8.0f;
    std::string hdr = "From contents";
    if (shown_count < static_cast<int>(from_contents_.size())) {
        hdr += std::format(" ({} of {} shown)", shown_count, from_contents_.size());
    }
    hdr += ":";
    r.draw_text(font, mx + PAD, y, hdr, TEXT_DIM);
    for (const auto& line : wrap.lines) {
        y += INHERIT_LINE;
        const auto tags_slice = std::span(from_contents_).subspan(static_cast<size_t>(line.first),
                                                                   static_cast<size_t>(line.count));
        draw_tag_chips(r, font, mx + PAD, y, MODAL_W - 2 * PAD, tags_slice, cats);
    }
    if (wrap.hidden > 0) {
        const std::string counter   = std::format("+{}", wrap.hidden);
        const auto        counter_w = static_cast<float>(font.measure(counter));
        const float counter_y =
            wrap.lines.empty() ? y : font.text_top_for_center(y + CHIP_ROW_H * 0.5f);
        r.draw_text(font, mx + PAD + (MODAL_W - 2 * PAD) - counter_w, counter_y, counter,
                    TEXT_FAINT);
    }
}

void TagEditor::draw_suggestions_dropdown(gfx::Renderer& r, gfx::FontAtlas& font, float mx,
                                           float input_y) const
{
    // Drawn last so it overlays the tag list like a combobox. Up/Down move
    // the highlight; Enter adds the highlighted suggestion (or, with none
    // highlighted, exactly the typed text).
    if (suggestions_.empty()) return;

    using namespace gfx::theme;

    constexpr float SUGG_ROW = 30.0f;
    const SDL_FRect drop{mx + PAD, input_y + INPUT_BOX_H + 4,
                         MODAL_W - 2 * PAD,
                         SUGG_ROW * static_cast<float>(suggestions_.size()) + 8};
    r.draw_round_rect(drop, RADIUS_SMALL, SURFACE_HI);
    r.draw_round_rect(drop, RADIUS_SMALL, ACCENT, /*filled*/ false);

    const auto& cats = vault::vault_settings(vault_).categories;
    for (int i = 0; i < static_cast<int>(suggestions_.size()); ++i) {
        const bool  sel   = i == sugg_sel_;
        const auto row_y = drop.y + 4 + SUGG_ROW * static_cast<float>(i);
        const auto ty    = font.text_top_for_center(row_y + SUGG_ROW * 0.5f);

        // Draw the marker (> or space)
        const std::string marker = sel ? ">" : " ";
        const gfx::Color marker_color = sel ? TEXT : TEXT_DIM;
        r.draw_text(font, drop.x + 10, ty, marker, marker_color);

        // Draw the suggestion as a chip
        const auto chip_x = drop.x + 10 + static_cast<float>(font.measure("> "));
        const auto max_w = drop.w - (chip_x - drop.x) - 10;
        const auto chip_row_y = row_y + (SUGG_ROW - CHIP_ROW_H) * 0.5f;
        draw_tag_chips(r, font, chip_x, chip_row_y, max_w, std::span(&suggestions_[i], 1), cats);
    }
}

} // namespace ui
