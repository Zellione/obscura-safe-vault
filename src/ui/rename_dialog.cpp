#include "ui/rename_dialog.h"

#include <format>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/widgets.h"
#include "vault/vault.h"

namespace ui {

void RenameDialog::open(std::string gallery_path, std::string old_name)
{
    active_       = true;
    gallery_path_ = std::move(gallery_path);
    old_name_     = std::move(old_name);
    buf_          = old_name_;
    error_.clear();
    SDL_StartTextInput(win_.sdl_window());
}

void RenameDialog::close()
{
    SDL_StopTextInput(win_.sdl_window());
    active_ = false;
}

bool RenameDialog::handle_event(vault::Vault& v, const SDL_Event& e)
{
    if (!active_) return false;

    if (e.type == SDL_EVENT_TEXT_INPUT) { buf_ += e.text.text; return true; }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;

    switch (e.key.key) {
        case SDLK_ESCAPE:
            close();
            return true;
        case SDLK_BACKSPACE:
            if (!buf_.empty()) buf_.pop_back();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: {
            if (buf_.empty()) { error_ = "Name cannot be empty."; return true; }
            const vault::VaultResult r = vault::rename_node(v, gallery_path_, old_name_, buf_);
            if (r == vault::VaultResult::Ok) {
                status_ = std::format("Renamed \"{}\" to \"{}\".", old_name_, buf_);
                done_   = true;
                close();
            } else if (r == vault::VaultResult::AlreadyExists) {
                error_ = "Something with that name already exists here.";
            } else if (r == vault::VaultResult::InvalidArg) {
                error_ = "That name isn't allowed.";
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

void RenameDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const
{
    if (!active_) return;
    using namespace gfx::theme;

    const float mw = W * 0.5f;
    const float mh = 160.0f;
    const float mx = (W - mw) / 2;
    const float my = (H - mh) / 2;
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    const float ix = mx + 20;
    const float iy = my + 20;
    r.draw_text(font, ix, iy, std::format("Rename \"{}\"", old_name_), TEXT);
    draw_text_field(r, font, {ix, iy + 40, mw - 40, 40}, buf_, true);
    r.draw_text(font, ix, iy + 96, "[Enter] Rename  [Esc] Cancel", TEXT_FAINT);
    if (!error_.empty()) r.draw_text(font, ix, iy + 124, error_, DANGER);
}

} // namespace ui
