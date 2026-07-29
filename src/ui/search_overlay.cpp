#include "ui/search_overlay.h"

#include <algorithm>
#include <span>
#include <string>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/list_layout.h"
#include "ui/nav_model.h"
#include "ui/search_model.h"
#include "ui/text_input_event.h"
#include "ui/text_metrics.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {
// Preferred size; render() clamps to the window so the modal never overflows it.
constexpr float MODAL_W = 900.0f;
constexpr float MODAL_H = 600.0f;
constexpr float PAD = 24.0f;         // panel inset: keeps rows/text off the border
constexpr float ROW_TEXT_INSET = 14.0f;   // text inset within a result row
constexpr float SEARCH_BOX_H = 44.0f;
constexpr float SCOPE_TOGGLE_H = 36.0f;
constexpr float RESULT_LIST_GAP = 8.0f;

// Build a one-line result label: kind icon + path + up to two effective tags.
std::string format_hit_label(const vault::SearchHit& hit)
{
    std::string label = hit.is_gallery ? "[G] " : "[I] ";
    label += hit.path;
    if (hit.effective_tags.empty()) return label;

    label += " {";
    for (size_t j = 0; j < hit.effective_tags.size() && j < 2; ++j) {
        label += (j == 0 ? "" : ",");
        label += hit.effective_tags[j];
    }
    if (hit.effective_tags.size() > 2) label += ",...";
    label += "}";
    return label;
}
}

void SearchOverlay::open()
{
    active_ = true;
    query_.clear();
    scope_ = vault::SearchScope::Both;
    selected_ = 0;
    gather_results();
    SDL_StartTextInput(win_.sdl_window());
}

void SearchOverlay::close()
{
    active_ = false;
    SDL_StopTextInput(win_.sdl_window());
    query_.clear();
    filtered_.clear();
    all_results_.clear();
}

void SearchOverlay::gather_results()
{
    // Gather all matches in the current scope
    all_results_ = vault_.search("", scope_);
    filter_results();
}

void SearchOverlay::filter_results()
{
    // Tokenize the query and filter/rank the results
    const auto tokens = tokenize(query_.view());
    filtered_.clear();
    filtered_.reserve(all_results_.size());

    for (const auto& hit : all_results_) {
        if (matches(tokens, hit.name, hit.effective_tags)) {
            filtered_.push_back(&hit);
        }
    }

    // Sort by score (descending), then by name (ascending).
    std::ranges::sort(filtered_,
                      [&tokens](const vault::SearchHit* a, const vault::SearchHit* b) {
                          const int score_a = score(tokens, a->name, a->effective_tags);
                          if (const int score_b = score(tokens, b->name, b->effective_tags);
                              score_a != score_b) {
                              return score_a > score_b;
                          }
                          return a->name < b->name;
                      });

    // Clamp selection to valid range. Guard the empty case: with no results the
    // upper bound would be -1, and std::clamp(x, 0, -1) trips a hi<lo assertion.
    selected_ = filtered_.empty()
                    ? 0
                    : std::clamp(selected_, 0, static_cast<int>(filtered_.size()) - 1);
}

bool SearchOverlay::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    // Precedence rule (Phase 54): the query field consumes editing keys before
    // any of this overlay's own shortcuts.
    if (const uint64_t rev = query_.revision(); handle_text_input_event(query_, e)) {
        if (query_.revision() != rev) filter_results();
        return true;
    }

    if (e.type != SDL_EVENT_KEY_DOWN) return false;

    switch (e.key.key) {
        case SDLK_ESCAPE:    close();                                       return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  activate_selected();                           return true;
        case SDLK_TAB:       cycle_scope();                                 return true;
        case SDLK_UP:        move_selection(-1);                            return true;
        case SDLK_DOWN:      move_selection(1);                             return true;
        default:             return false;
    }
}

void SearchOverlay::cycle_scope()
{
    using enum vault::SearchScope;
    switch (scope_) {
        case Both:      scope_ = Images;    break;
        case Images:    scope_ = Galleries; break;
        case Galleries: scope_ = Both;      break;
    }
    gather_results();
}

void SearchOverlay::move_selection(int delta)
{
    if (filtered_.empty()) return;
    selected_ = std::clamp(selected_ + delta, 0, static_cast<int>(filtered_.size()) - 1);
}

// Resolve an image hit to a viewer nav: its parent gallery path + index within
// that gallery's listing. Returns a None nav if the image can't be located.
Nav SearchOverlay::nav_for_image(const vault::SearchHit& hit) const
{
    const auto segs = split_path(hit.path);
    if (segs.empty()) return {};

    const std::string parent =
        segs.size() > 1 ? join_path(std::span(segs.data(), segs.size() - 1)) : std::string{};

    const auto images = vault_.list(parent);
    for (int i = 0; i < static_cast<int>(images.size()); ++i) {
        if (images[i]->name == segs.back()) return Nav{NavKind::ToViewer, parent, i};
    }
    return {};
}

void SearchOverlay::activate_selected()
{
    if (selected_ < 0 || selected_ >= static_cast<int>(filtered_.size())) return;

    const auto& hit = *filtered_[selected_];
    nav_ = hit.is_gallery ? Nav{NavKind::ToGallery, hit.path, 0} : nav_for_image(hit);
    close();
}

void SearchOverlay::on_vault_changed()
{
    if (active_) gather_results();
}

void SearchOverlay::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
{
    if (!active_) return;

    using namespace gfx::theme;
    const float LINE         = line_pitch(font.pixel_height());
    const float RESULT_ROW_H = row_height(font.pixel_height(), 6.0f);

    // Dim the background
    r.draw_rect({0, 0, W, H}, {0, 0, 0, 180}, /*filled*/ true);

    // Modal panel background
    const float mw = std::min(MODAL_W, W - 2 * PAD);
    const float mh = std::min(MODAL_H, H - 2 * PAD);
    const float mx = (W - mw) / 2;
    const float my = (H - mh) / 2;
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    // Title
    r.draw_text(font, mx + PAD, my + PAD, "Search", TEXT);

    // Search input box
    const float search_y = my + PAD + LINE;
    const SDL_FRect search_box{mx + PAD, search_y, mw - 2 * PAD, SEARCH_BOX_H};
    draw_edit_field(r, font, search_box, query_, query_chrome_, true);

    // Scope toggle label and cycle instruction
    const float scope_y = search_y + SEARCH_BOX_H + 12;
    const std::string scope_label = [&]() -> std::string {
        using enum vault::SearchScope;
        switch (scope_) {
            case Both:      return "Scope: Both [Tab]";
            case Images:    return "Scope: Images [Tab]";
            case Galleries: return "Scope: Galleries [Tab]";
        }
        return "Scope: Both";
    }();
    r.draw_text(font, mx + PAD, scope_y, scope_label, TEXT_DIM);

    // Result list
    const float list_y = scope_y + SCOPE_TOGGLE_H;
    const float list_h = mh - (list_y - my) - PAD;

    // Draw results
    const int max_rows = list_rows_fit({.top = list_y, .row_h = RESULT_ROW_H, .gap = RESULT_LIST_GAP},
                                        my + mh - PAD, static_cast<int>(filtered_.size()));
    for (int i = 0; i < max_rows; ++i) {
        const float row_y =
            list_row_y({.top = list_y, .row_h = RESULT_ROW_H, .gap = RESULT_LIST_GAP}, i);

        const auto& hit = *filtered_[i];
        const SDL_FRect row_rect{mx + PAD, row_y, mw - 2 * PAD, RESULT_ROW_H};

        // Highlight the selected row
        if (i == selected_) {
            r.draw_selection_glow(row_rect, RADIUS_SMALL, ACCENT);
            r.draw_round_rect(row_rect, RADIUS_SMALL, SURFACE_HI);
        } else {
            r.draw_round_rect(row_rect, RADIUS_SMALL, BORDER, /*filled*/ false);
        }

        const std::string display =
            fit_text(font, format_hit_label(hit), row_rect.w - 2 * ROW_TEXT_INSET);
        const float text_y =
            font.text_top_for_center(row_rect.y + row_rect.h * 0.5f);
        r.draw_text(font, row_rect.x + ROW_TEXT_INSET, text_y, display, TEXT);
    }

    if (filtered_.empty()) {
        const float msg_y =
            list_y + list_h * 0.5f - font.pixel_height() * 0.5f;
        r.draw_text(font, mx + PAD, msg_y, "No results", TEXT_FAINT);
    }

    // Footer hint
    r.draw_text(font, mx + PAD, my + mh - PAD - 16,
                "[Enter] Select  [Esc] Close  [↑↓] Navigate",
                TEXT_FAINT);
}

} // namespace ui
