#include "ui/tag_editor.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/nav_model.h"
#include "ui/tag_scroll.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

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
}

void TagEditor::open(std::string node_path)
{
    active_ = true;
    node_path_ = std::move(node_path);
    selected_ = 0;
    new_tag_buf_.clear();
    error_.clear();
    refresh_tags();
    SDL_StartTextInput(win_.sdl_window());
}

void TagEditor::close()
{
    active_ = false;
    SDL_StopTextInput(win_.sdl_window());
    node_path_.clear();
    tags_.clear();
    new_tag_buf_.clear();
    error_.clear();
}

void TagEditor::refresh_tags()
{
    // Look up the node and read its current tags
    tags_.clear();
    error_.clear();

    // Split the path and navigate to find the node
    const auto segs = split_path(node_path_);
    if (segs.empty()) {
        error_ = "Invalid node path.";
        return;
    }

    // Get the parent gallery (all segments but the last)
    std::string parent_path;
    if (segs.size() > 1) {
        parent_path = join_path(std::span(segs.data(), segs.size() - 1));
    }

    // Find the node by name in the parent's children
    const auto& children = vault_.list(parent_path);
    for (const auto* child : children) {
        if (child->name == segs.back()) {
            tags_ = child->tags;  // Copy the node's current tags
            return;
        }
    }

    error_ = "Node not found.";
}

void TagEditor::select_tag(std::string_view tag)
{
    // Case-insensitive find so the just-added/merged tag is highlighted and the
    // render window scrolls to reveal it. Falls back to the last row.
    auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
    auto ci_equal = [&](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i])))
                return false;
        return true;
    };
    for (int i = 0; i < static_cast<int>(tags_.size()); ++i)
        if (ci_equal(tags_[i], tag)) { selected_ = i; return; }
    selected_ = std::max(0, static_cast<int>(tags_.size()) - 1);
}

bool TagEditor::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    if (e.type == SDL_EVENT_TEXT_INPUT) {
        new_tag_buf_ += e.text.text;
        return true;
    }

    if (e.type != SDL_EVENT_KEY_DOWN) return false;

    switch (e.key.key) {
        case SDLK_ESCAPE:
            close();
            return true;

        case SDLK_BACKSPACE:
            if (!new_tag_buf_.empty()) {
                new_tag_buf_.pop_back();
                error_.clear();
            }
            return true;

        case SDLK_RETURN:
        case SDLK_KP_ENTER: {
            // Trim only SURROUNDING whitespace so multi-word tags ("new york")
            // survive; the vault owns the canonical normalisation/dedup.
            if (const std::string trimmed = trim_surrounding(new_tag_buf_); !trimmed.empty()) {
                using enum vault::VaultResult;
                switch (vault_.add_tag(node_path_, trimmed)) {
                    case Ok:
                        new_tag_buf_.clear();
                        error_.clear();
                        refresh_tags();
                        select_tag(trimmed);   // scroll the list to reveal it
                        break;
                    case InvalidArg:
                        error_ = "Invalid tag.";
                        break;
                    case NotFound:
                        error_ = "Node not found.";
                        break;
                    default:
                        error_ = "Failed to add tag.";
                        break;
                }
            }
            return true;
        }

        case SDLK_UP:
            if (selected_ > 0) {
                selected_--;
                error_.clear();
            }
            return true;

        case SDLK_DOWN:
            if (selected_ < static_cast<int>(tags_.size()) - 1) {
                selected_++;
                error_.clear();
            }
            return true;

        case SDLK_DELETE: {
            // Delete the selected tag
            if (selected_ >= 0 && selected_ < static_cast<int>(tags_.size())) {
                const std::string tag_to_remove = tags_[selected_];
                if (vault_.remove_tag(node_path_, tag_to_remove) == vault::VaultResult::Ok) {
                    refresh_tags();
                    selected_ = std::min(selected_, static_cast<int>(tags_.size()) - 1);
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

    // Node path display (secondary text)
    r.draw_text(font, mx + PAD, my + PAD + LINE, node_path_, TEXT_FAINT);

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
    const int   total      = static_cast<int>(tags_.size());
    const int   max_visible =
        std::max(1, static_cast<int>(((my + MODAL_H - 50) - tags_start) / row_pitch));
    const int   first = tag_scroll_first(total, selected_, max_visible);
    const int   last  = std::min(total, first + max_visible);

    // Header shows the visible range / count so hidden tags are discoverable.
    std::string header = "Current tags";
    if (total > max_visible)
        header += " (" + std::to_string(first + 1) + "-" + std::to_string(last) +
                  " of " + std::to_string(total) + ")";
    else if (total > 0)
        header += " (" + std::to_string(total) + ")";
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

        const std::string display = tags_[i] + " [Delete]";
        const float text_y =
            font.text_top_for_center(tag_rect.y + tag_rect.h * 0.5f);
        r.draw_text(font, tag_rect.x + 8, text_y, display, TEXT);
    }

    if (tags_.empty()) {
        r.draw_text(font, mx + PAD, tags_start, "(no tags)", TEXT_FAINT);
    }

    // Error message
    if (!error_.empty()) {
        r.draw_text(font, mx + PAD, my + MODAL_H - 32, error_, DANGER);
    }

    // Footer hint
    r.draw_text(font, mx + PAD, my + MODAL_H - 12,
                "[Enter] Add  [Up/Down] Scroll  [Del] Remove  [Esc] Close",
                TEXT_FAINT);
}

} // namespace ui
