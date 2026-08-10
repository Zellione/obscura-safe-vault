#include "ui/vault_unlock_picker.h"

#include <monocypher.h>

#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "platform/path_utf8.h"
#include "platform/vault_registry.h"
#include "ui/second_vault.h"
#include "ui/text_input_event.h"
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

[[nodiscard]] std::string second_vault_mode_label(platform::SecondVaultMode mode) noexcept
{
    using enum platform::SecondVaultMode;
    switch (mode) {
    case LockNow:
        return "Lock immediately";
    case KeepTimed:
        return "Keep open 5 min";
    case KeepSession:
        return "Keep open for session";
    }
    return "Unknown";
}
} // namespace

VaultUnlockPicker::VaultUnlockPicker(platform::VaultRegistry& registry, platform::FileDialog& dlg,
                                     gfx::Window& win, SecondVaultSession* second)
    : registry_(registry), dlg_(dlg), win_(win), second_(second) {}

void VaultUnlockPicker::open(std::string src_path)
{
    active_    = true;
    chosen_    = false;
    from_warm_ = false;
    stage_     = Stage::PickVault;
    src_path_  = std::move(src_path);
    vault_sel_ = 0;
    error_.clear();
    keep_mode_ = second_ ? second_->default_mode() : platform::SecondVaultMode::LockNow;
    dest_.is_self = false;
    dest_.pw.clear();
    dest_.keyfile_path.clear();
    dest_.awaiting_keyfile = false;

    candidates_.clear();
    for (const auto& p : registry_.list())
        if (platform::path_to_utf8(p) != src_path_) candidates_.push_back(p);
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
    return dest_.is_self ? "this vault" : platform::path_to_utf8(platform::utf8_to_path(dest_.path).stem());
}

vault::Vault& VaultUnlockPicker::unlocked_vault() noexcept
{
    return from_warm_ ? second_->vault() : dest_.vault;
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
    dest_.path    = platform::path_to_utf8(candidates_[static_cast<size_t>(ci)]);
    dest_.pw.clear();
    dest_.keyfile_path.clear();

    // Phase 66: the warm slot skips the password stage entirely.
    if (second_ && second_->occupied() && second_->path() == dest_.path) {
        from_warm_ = true;
        active_    = false;
        chosen_    = true;
        return;
    }
    stage_ = Stage::Unlock;
}

void VaultUnlockPicker::try_unlock()
{
    using enum vault::VaultResult;

    std::vector<uint8_t> keyfile;
    if (!dest_.keyfile_path.empty()) {
        auto kf = platform::read_file(platform::utf8_to_path(dest_.keyfile_path));
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

void VaultUnlockPicker::release_to_slot()
{
    if (is_self() || !chosen_) return;
    if (from_warm_) {
        second_->on_transfer_completed();
    } else if (keep_mode_ != platform::SecondVaultMode::LockNow && second_) {
        second_->adopt(std::move(dest_.vault), dest_.path, keep_mode_);
    }
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
    if (second_ && (k == SDLK_UP || k == SDLK_DOWN)) {
        keep_mode_ = static_cast<platform::SecondVaultMode>(
            (std::to_underlying(keep_mode_) + (k == SDLK_DOWN ? 1 : 2)) % 3);
        return true;
    }
    if (k == SDLK_TAB) { dlg_.open_keyfile(win_.sdl_window()); dest_.awaiting_keyfile = true; }
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) try_unlock();
    return true;
}

bool VaultUnlockPicker::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    // Precedence rule (Phase 54): the password field consumes editing keys —
    // Ctrl+A included — before any of this dialog's own shortcuts.
    if (stage_ == Stage::Unlock && handle_text_input_event(dest_.pw, e)) return true;
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
                               float mw)
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
        for (const auto& p : candidates_) labels.push_back(platform::path_to_utf8(p.filename()));
        row_list(labels, vault_sel_, iy + 72);
    } else {
        r.draw_text(font, ix, iy + 36,
                    fit_text(font, "Unlock " + platform::path_to_utf8(platform::utf8_to_path(dest_.path).filename()),
                            mw - 40),
                    TEXT_DIM);
        draw_edit_field(r, font, {ix, iy + 72, mw - 40, 40}, dest_.pw, dest_.pw_chrome,
                        true, /*mask*/ true);
        r.draw_text(font, ix, iy + 122,
                    dest_.keyfile_path.empty() ? "[Tab] add keyfile  [Enter] unlock"
                                               : "keyfile set  •  [Enter] unlock",
                    TEXT_FAINT);
        if (second_) {
            r.draw_text(font, ix, iy + 152,
                        fit_text(font, std::string("After transfer: ") + second_vault_mode_label(keep_mode_) +
                                           "  [Up/Down]", mw - 40),
                        TEXT_FAINT);
        }
    }
}

} // namespace ui
