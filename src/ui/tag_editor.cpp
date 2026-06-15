#include "ui/tag_editor.h"

#include <algorithm>
#include <cctype>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/nav_model.h"
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
            // Trim and add the new tag
            std::string trimmed;
            for (char c : new_tag_buf_) {
                if (!std::isspace(static_cast<unsigned char>(c))) {
                    trimmed += c;
                }
            }

            if (!trimmed.empty()) {
                using enum vault::VaultResult;
                switch (vault_.add_tag(node_path_, trimmed)) {
                    case Ok:
                        new_tag_buf_.clear();
                        error_.clear();
                        refresh_tags();
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
                using enum vault::VaultResult;
                switch (vault_.remove_tag(node_path_, tag_to_remove)) {
                    case Ok:
                        refresh_tags();
                        selected_ = std::min(selected_, static_cast<int>(tags_.size()) - 1);
                        error_.clear();
                        break;
                    default:
                        error_ = "Failed to remove tag.";
                        break;
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
    r.draw_text(font, mx + PAD, my + PAD + 24, node_path_, TEXT_FAINT);

    // Input box for new tag
    const float input_y = my + PAD + 60;
    const SDL_FRect input_box{mx + PAD, input_y, MODAL_W - 2 * PAD, INPUT_BOX_H};
    r.draw_round_rect(input_box, gfx::theme::RADIUS_SMALL, gfx::theme::SURFACE);
    r.draw_round_rect(input_box, gfx::theme::RADIUS_SMALL, gfx::theme::ACCENT,
                      /*filled*/ false);
    r.draw_text(font, input_box.x + 8,
                input_box.y + font.text_top_for_center(input_box.y + input_box.h * 0.5f),
                new_tag_buf_, TEXT);

    // Current tags list
    const float list_y = input_y + INPUT_BOX_H + 12;

    r.draw_text(font, mx + PAD, list_y, "Current tags:", TEXT_DIM);

    const float tags_start = list_y + 24;
    for (int i = 0; i < static_cast<int>(tags_.size()); ++i) {
        const float row_y = tags_start + i * (TAG_ROW_H + TAG_LIST_GAP);
        if (row_y + TAG_ROW_H > my + MODAL_H - 50) break;

        const SDL_FRect tag_rect{mx + PAD, row_y, MODAL_W - 2 * PAD, TAG_ROW_H};

        if (i == selected_) {
            r.draw_selection_glow(tag_rect, RADIUS_SMALL, ACCENT);
            r.draw_round_rect(tag_rect, RADIUS_SMALL, SURFACE_HI);
        } else {
            r.draw_round_rect(tag_rect, RADIUS_SMALL, BORDER, /*filled*/ false);
        }

        const std::string display = tags_[i] + " [Delete]";
        const float text_y =
            tag_rect.y + font.text_top_for_center(tag_rect.y + tag_rect.h * 0.5f);
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
                "[Enter] Add  [↑↓] Select  [Del] Remove  [Esc] Close",
                TEXT_FAINT);
}

} // namespace ui
