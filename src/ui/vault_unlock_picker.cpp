#include "ui/vault_unlock_picker.h"

#include <monocypher.h>

#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "platform/vault_registry.h"
#include "ui/widgets.h"

namespace ui {

namespace {
int clamp_index(int sel, int count) noexcept
{
    if (count <= 0 || sel < 0) return 0;
    return sel > count - 1 ? count - 1 : sel;
}

std::string unlock_error_message(vault::VaultResult r) noexcept
{
    using enum vault::VaultResult;
    if (r == AuthFailed) return "Wrong password or keyfile.";
    if (r == BadFormat)  return "Not a valid vault file.";
    return "Could not open the destination vault.";
}
} // namespace

VaultUnlockPicker::VaultUnlockPicker(platform::VaultRegistry& registry, platform::FileDialog& dlg,
                                     gfx::Window& win)
    : registry_(registry), dlg_(dlg), win_(win) {}

void VaultUnlockPicker::open(std::string src_path)
{
    active_    = true;
    chosen_    = false;
    stage_     = Stage::PickVault;
    src_path_  = std::move(src_path);
    vault_sel_ = 0;
    error_.clear();
    dest_.is_self = false;
    dest_.pw.clear();
    dest_.keyfile_path.clear();
    dest_.awaiting_keyfile = false;

    candidates_.clear();
    for (const auto& p : registry_.list())
        if (p.string() != src_path_) candidates_.push_back(p);
}

void VaultUnlockPicker::close()
{
    if (dest_.vault.is_unlocked()) dest_.vault.lock();
    dest_.pw.clear();
    active_ = false;
    chosen_ = false;
}

std::string VaultUnlockPicker::dest_label() const
{
    return dest_.is_self ? "this vault" : std::filesystem::path(dest_.path).stem().string();
}

void VaultUnlockPicker::choose_vault()
{
    error_.clear();
    if (vault_sel_ == 0) {
        dest_.is_self = true;
        active_       = false;
        chosen_       = true;
        return;
    }
    const int ci = vault_sel_ - 1;
    if (ci < 0 || ci >= static_cast<int>(candidates_.size())) return;
    dest_.is_self = false;
    dest_.path    = candidates_[static_cast<size_t>(ci)].string();
    dest_.pw.clear();
    dest_.keyfile_path.clear();
    stage_ = Stage::Unlock;
}

void VaultUnlockPicker::try_unlock()
{
    using enum vault::VaultResult;

    std::vector<uint8_t> keyfile;
    if (!dest_.keyfile_path.empty()) {
        auto kf = platform::read_file(dest_.keyfile_path);
        if (!kf) { error_ = "Cannot read keyfile."; return; }
        keyfile = std::move(*kf);
    }

    vault::VaultResult r = vault::Vault::open(dest_.path, dest_.vault);
    if (r == Ok) r = dest_.vault.unlock(dest_.pw.bytes(), keyfile);
    if (!keyfile.empty()) crypto_wipe(keyfile.data(), keyfile.size());

    if (r != Ok) {
        error_ = unlock_error_message(r);
        return;
    }
    dest_.pw.clear();
    error_.clear();
    active_ = false;
    chosen_ = true;
}

bool VaultUnlockPicker::handle_pick_vault_key(SDL_Keycode k)
{
    const auto n = static_cast<int>(candidates_.size()) + 1;
    if (k == SDLK_UP)   vault_sel_ = clamp_index(vault_sel_ - 1, n);
    if (k == SDLK_DOWN) vault_sel_ = clamp_index(vault_sel_ + 1, n);
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) choose_vault();
    return true;
}

bool VaultUnlockPicker::handle_unlock_key(SDL_Keycode k)
{
    if (k == SDLK_BACKSPACE) dest_.pw.backspace();
    else if (k == SDLK_TAB) { dlg_.open_keyfile(win_.sdl_window()); dest_.awaiting_keyfile = true; }
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) try_unlock();
    return true;
}

bool VaultUnlockPicker::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    if (e.type == SDL_EVENT_TEXT_INPUT && stage_ == Stage::Unlock) {
        dest_.pw.push_utf8(e.text.text);
        return true;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;

    const SDL_Keycode k = e.key.key;
    if (k == SDLK_ESCAPE) { close(); return true; }   // cancels the whole flow, no back-step

    return stage_ == Stage::PickVault ? handle_pick_vault_key(k) : handle_unlock_key(k);
}

void VaultUnlockPicker::update()
{
    if (!dest_.awaiting_keyfile) return;
    if (auto res = dlg_.take_result()) {
        dest_.awaiting_keyfile = false;
        if (!res->empty()) dest_.keyfile_path = (*res)[0];
    }
}

void VaultUnlockPicker::render(gfx::Renderer& r, gfx::FontAtlas& font, float ix, float iy,
                               float mw) const
{
    using namespace gfx::theme;

    auto row_list = [&](const std::vector<std::string>& items, int sel, float top) {
        for (size_t i = 0; i < items.size(); ++i) {
            const float ry = top + static_cast<float>(i) * 34.0f;
            const bool  on = (static_cast<int>(i) == sel);
            if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
            r.draw_text(font, ix + 8, ry + 4, fit_text(font, items[i], mw - 56),
                        on ? TEXT : TEXT_DIM);
        }
    };

    if (stage_ == Stage::PickVault) {
        r.draw_text(font, ix, iy + 36, "Destination vault:", TEXT_DIM);
        std::vector<std::string> labels = {"This vault"};
        for (const auto& p : candidates_) labels.push_back(p.filename().string());
        row_list(labels, vault_sel_, iy + 72);
    } else {
        r.draw_text(font, ix, iy + 36,
                    fit_text(font, "Unlock " + std::filesystem::path(dest_.path).filename().string(),
                            mw - 40),
                    TEXT_DIM);
        std::string masked(dest_.pw.length(), '*');
        draw_text_field(r, font, {ix, iy + 72, mw - 40, 40}, masked, true);
        r.draw_text(font, ix, iy + 122,
                    dest_.keyfile_path.empty() ? "[Tab] add keyfile  [Enter] unlock"
                                               : "keyfile set  •  [Enter] unlock",
                    TEXT_FAINT);
    }
}

} // namespace ui
