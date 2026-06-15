#include "ui/search_overlay.h"

#include <algorithm>
#include <format>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/nav_model.h"
#include "ui/search_model.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float MODAL_W = 600.0f;
constexpr float MODAL_H = 500.0f;
constexpr float PAD = 16.0f;
constexpr float SEARCH_BOX_H = 44.0f;
constexpr float SCOPE_TOGGLE_H = 36.0f;
constexpr float RESULT_ROW_H = 40.0f;
constexpr float RESULT_LIST_GAP = 8.0f;
}

void SearchOverlay::open()
{
    active_ = true;
    query_.clear();
    scope_ = vault::SearchScope::Both;
    selected_ = 0;
    refresh_results();
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

void SearchOverlay::refresh_results()
{
    // Gather all matches in the current scope
    all_results_ = vault_.search("", scope_);

    // Tokenize the query and filter/rank the results
    const auto tokens = tokenize(query_);
    filtered_.clear();
    filtered_.reserve(all_results_.size());

    for (auto& hit : all_results_) {
        if (matches(tokens, hit.name, hit.effective_tags)) {
            filtered_.push_back(&hit);
        }
    }

    // Sort by score (descending), then by name (ascending)
    std::sort(filtered_.begin(), filtered_.end(),
              [&tokens](const vault::SearchHit* a, const vault::SearchHit* b) {
                  const int score_a = score(tokens, a->name, a->effective_tags);
                  const int score_b = score(tokens, b->name, b->effective_tags);
                  if (score_a != score_b) return score_a > score_b;
                  return a->name < b->name;
              });

    // Clamp selection to valid range
    selected_ = std::clamp(selected_, 0, static_cast<int>(filtered_.size()) - 1);
}

bool SearchOverlay::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    if (e.type == SDL_EVENT_TEXT_INPUT) {
        query_ += e.text.text;
        refresh_results();
        return true;
    }

    if (e.type != SDL_EVENT_KEY_DOWN) return false;

    using enum vault::SearchScope;
    switch (e.key.key) {
        case SDLK_ESCAPE:
            close();
            return true;

        case SDLK_BACKSPACE:
            if (!query_.empty()) {
                query_.pop_back();
                refresh_results();
            }
            return true;

        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (selected_ >= 0 && selected_ < static_cast<int>(filtered_.size())) {
                const auto& hit = *filtered_[selected_];
                if (hit.is_gallery) {
                    // Gallery: navigate to it
                    nav_ = Nav{NavKind::ToGallery, hit.path, 0};
                } else {
                    // Image: split the path, find the image index, open the viewer
                    const auto segs = split_path(hit.path);
                    if (segs.empty()) return true;  // shouldn't happen

                    // The parent gallery is all but the last segment
                    std::string parent_path;
                    if (segs.size() > 1) {
                        parent_path = join_path(std::span(segs.data(), segs.size() - 1));
                    }

                    // Find the image's index within its gallery
                    int img_idx = -1;
                    const auto& images = vault_.list(parent_path);
                    for (int i = 0; i < static_cast<int>(images.size()); ++i) {
                        if (images[i]->name == segs.back()) {
                            img_idx = i;
                            break;
                        }
                    }

                    if (img_idx >= 0) {
                        nav_ = Nav{NavKind::ToViewer, parent_path, img_idx};
                    }
                }
                close();
            }
            return true;

        case SDLK_TAB:
            // Cycle through scopes: Both -> Images -> Galleries -> Both
            switch (scope_) {
                case Both:     scope_ = Images; break;
                case Images:   scope_ = Galleries; break;
                case Galleries: scope_ = Both; break;
            }
            refresh_results();
            return true;

        case SDLK_UP:
            if (selected_ > 0) selected_--;
            return true;

        case SDLK_DOWN:
            if (selected_ < static_cast<int>(filtered_.size()) - 1) selected_++;
            return true;

        default:
            return false;
    }
}

void SearchOverlay::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
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
    r.draw_text(font, mx + PAD, my + PAD, "Search", TEXT);

    // Search input box
    const float search_y = my + PAD + 28;
    const SDL_FRect search_box{mx + PAD, search_y, MODAL_W - 2 * PAD, SEARCH_BOX_H};
    draw_text_field(r, font, search_box, query_, true);

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
    const float list_h = MODAL_H - (list_y - my) - PAD;

    // Draw results
    int visible = 0;
    for (int i = 0; i < static_cast<int>(filtered_.size()); ++i) {
        const float row_y = list_y + visible * (RESULT_ROW_H + RESULT_LIST_GAP);
        if (row_y + RESULT_ROW_H > my + MODAL_H - PAD) break;

        const auto& hit = *filtered_[i];
        const SDL_FRect row_rect{mx + PAD, row_y, MODAL_W - 2 * PAD, RESULT_ROW_H};

        // Highlight the selected row
        if (i == selected_) {
            r.draw_selection_glow(row_rect, RADIUS_SMALL, ACCENT);
            r.draw_round_rect(row_rect, RADIUS_SMALL, SURFACE_HI);
        } else {
            r.draw_round_rect(row_rect, RADIUS_SMALL, BORDER, /*filled*/ false);
        }

        // Format the display: icon (folder/image) + path + tags
        std::string display = hit.is_gallery ? "[G] " : "[I] ";
        display += hit.path;
        if (!hit.effective_tags.empty()) {
            display += " {";
            for (size_t j = 0; j < hit.effective_tags.size() && j < 2; ++j) {
                if (j > 0) display += ",";
                display += hit.effective_tags[j];
            }
            if (hit.effective_tags.size() > 2) display += ",...";
            display += "}";
        }

        const float text_y =
            row_rect.y + font.text_top_for_center(row_rect.y + row_rect.h * 0.5f);
        r.draw_text(font, row_rect.x + 8, text_y, display, TEXT);

        visible++;
    }

    if (filtered_.empty()) {
        const float msg_y =
            list_y + list_h * 0.5f - font.pixel_height() * 0.5f;
        r.draw_text(font, mx + PAD, msg_y, "No results", TEXT_FAINT);
    }

    // Footer hint
    r.draw_text(font, mx + PAD, my + MODAL_H - PAD - 16,
                "[Enter] Select  [Esc] Close  [↑↓] Navigate",
                TEXT_FAINT);
}

} // namespace ui
