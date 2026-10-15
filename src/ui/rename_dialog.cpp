#include "ui/rename_dialog.h"

#include <format>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/text_input_event.h"
#include "ui/text_metrics.h"
#include "ui/widgets.h"
#include "vault/vault.h"

namespace ui {

void RenameDialog::open(std::string gallery_path, std::string old_name)
{
    tag_mode_ = false;
    skip_opening_text_ = false;
    done_ = false;
    active_       = true;
    gallery_path_ = std::move(gallery_path);
    old_name_     = std::move(old_name);
    buf_.set_text(old_name_);
    error_.clear();
    SDL_StartTextInput(win_.sdl_window());
}

void RenameDialog::open_tag(std::string tag)
{
    open({}, std::move(tag));
    tag_mode_ = true;
    skip_opening_text_ = true;
    buf_.select_all();
}

void RenameDialog::close()
{
    SDL_StopTextInput(win_.sdl_window());
    active_ = false;
}

bool RenameDialog::handle_event(vault::Vault& v, const SDL_Event& e)
{
    if (!active_) return false;

    if (skip_opening_text_ && e.type == SDL_EVENT_TEXT_INPUT) {
        skip_opening_text_ = false;
        return true;
    }
    if (e.type == SDL_EVENT_KEY_UP || e.type == SDL_EVENT_KEY_DOWN)
        skip_opening_text_ = false;

    // Precedence rule (Phase 54): the field consumes editing keys first.
    if (handle_text_input_event(buf_, e)) return true;
    if (e.type != SDL_EVENT_KEY_DOWN) return true;

    switch (e.key.key) {
        case SDLK_ESCAPE:
            close();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: {
            using enum vault::VaultResult;
            if (buf_.empty()) { error_ = "Name cannot be empty."; return true; }
            if (const vault::VaultResult r = tag_mode_
                    ? vault::rename_tag(v, old_name_, buf_.str())
                    : vault::rename_node(v, gallery_path_, old_name_, buf_.str()); r == Ok) {
                status_ = std::format(R"(Renamed "{}" to "{}".)", old_name_, buf_.str());
                done_   = true;
                close();
            } else if (r == AlreadyExists) {
                error_ = tag_mode_ ? "A tag with that name already exists."
                                  : "Something with that name already exists here.";
            } else if (r == InvalidArg) {
                error_ = tag_mode_ ? "Invalid name or incompatible category fields."
                                  : "That name isn't allowed.";
            } else {
                error_ = "Rename failed.";
            }
            return true;
        }
        default:
            return true;
    }
}

bool RenameDialog::consume_completed(std::string& status_out)
{
    if (!done_) return false;
    status_out = std::move(status_);
    done_      = false;
    return true;
}

void RenameDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
{
    if (!active_) return;
    using namespace gfx::theme;

    const float mw = W * 0.5f;
    const float pitch = line_pitch(font.pixel_height());
    const float input_h = pitch + 8.0f;
    const float mh = 48.0f + 3 * pitch + input_h;
    const float mx = (W - mw) / 2;
    const float my = (H - mh) / 2;
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    const float ix = mx + 20;
    const float iy = my + 20;
    // The title carries a vault-supplied node name, so its length is unbounded —
    // elide it, and the hint with it, rather than letting either run past the border.
    r.draw_text(font, ix, iy,
                fit_text(font, std::format("Rename \"{}\"", old_name_), mw - 40), TEXT);
    draw_edit_field(r, font, {ix, iy + pitch, mw - 40, input_h}, buf_, buf_chrome_, true);
    r.draw_text(font, ix, iy + pitch + input_h + 8.0f,
                fit_text(font, "[Enter] Rename  [Esc] Cancel", mw - 40), TEXT_FAINT);
    if (!error_.empty())
        r.draw_text(font, ix, iy + 2 * pitch + input_h + 8.0f, fit_text(font, error_, mw - 40), DANGER);
}

} // namespace ui
