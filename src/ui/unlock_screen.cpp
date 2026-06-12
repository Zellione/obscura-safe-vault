#include "ui/unlock_screen.h"

#include <monocypher.h>

#include <vector>

#include "crypto/kdf.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "ui/passphrase.h"
#include "ui/unlock_logic.h"
#include "ui/widgets.h"
#include "vault/vault.h"

namespace ui {

UnlockScreen::UnlockScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                           platform::FileDialog& dlg, std::filesystem::path vault_path)
    : win_(win), font_(font), vault_(vault), dlg_(dlg),
      vault_path_(std::move(vault_path)),
      create_mode_(!std::filesystem::exists(vault_path_))
{
}

void UnlockScreen::on_enter() { SDL_StartTextInput(win_.sdl_window()); }

void UnlockScreen::on_exit()
{
    SDL_StopTextInput(win_.sdl_window());
    pw_.clear();
    confirm_.clear();
    reveal_pw_ = false;
}

UnlockScreen::Layout UnlockScreen::layout() const
{
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());
    const float bw = 200.0f;
    const float bh = 44.0f;
    const float gap = 16.0f;
    const float row = H - 140.0f;
    return Layout{
        .keyfile_btn  = {60.0f,                  row, bw, bh},
        .other_btn    = {60.0f + (bw + gap),     row, bw, bh},
        .mode_btn     = {60.0f + 2 * (bw + gap), row, bw, bh},
        .submit_btn   = {W - 60.0f - bw,         row, bw, bh},
        .generate_btn    = {60.0f, 320.0f, bw + 40.0f, 36.0f},
        .new_keyfile_btn = {60.0f + (bw + 40.0f) + gap, 320.0f, bw - 40.0f, 36.0f},
    };
}

void UnlockScreen::handle_event(const SDL_Event& e)
{
    switch (e.type) {
        case SDL_EVENT_TEXT_INPUT: {
            SecureTextField& f = (create_mode_ && focus_ == 1) ? confirm_ : pw_;
            f.push_utf8(e.text.text);
            reveal_pw_ = false;  // edited by hand: stop displaying it
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            SecureTextField& f = (create_mode_ && focus_ == 1) ? confirm_ : pw_;
            switch (e.key.key) {
                case SDLK_BACKSPACE: f.backspace(); reveal_pw_ = false; break;
                case SDLK_TAB:       if (create_mode_) focus_ ^= 1; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:  submit(); break;
                default: break;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            const Layout L = layout();
            if (const SDL_FPoint p{e.button.x, e.button.y};
                point_in_rect(p.x, p.y, L.mode_btn)) {
                create_mode_ = !create_mode_; focus_ = 0; error_.clear();
                reveal_pw_ = false;
            } else if (create_mode_ && point_in_rect(p.x, p.y, L.generate_btn)) {
                // Fill both fields with one random passphrase and show it so
                // the user can write it down before creating the vault.
                if (generate_passphrase(pw_)) {
                    confirm_.clear();
                    confirm_.push_utf8(std::string_view(
                        reinterpret_cast<const char*>(pw_.bytes().data()), pw_.length()));
                    reveal_pw_ = true;
                    error_.clear();
                }
            } else if (create_mode_ && point_in_rect(p.x, p.y, L.new_keyfile_btn)) {
                pending_ = Pending::NewKeyfile; dlg_.save_keyfile(win_.sdl_window());
            } else if (point_in_rect(p.x, p.y, L.keyfile_btn)) {
                pending_ = Pending::Keyfile; dlg_.open_keyfile(win_.sdl_window());
            } else if (point_in_rect(p.x, p.y, L.other_btn)) {
                pending_ = Pending::Vault;   dlg_.open_vault(win_.sdl_window());
            } else if (point_in_rect(p.x, p.y, L.submit_btn)) {
                submit();
            }
            break;
        }
        default: break;
    }
}

void UnlockScreen::update(double)
{
    using enum Pending;
    if (auto res = dlg_.take_result()) {
        if (!res->empty()) {
            if (pending_ == Vault) {
                vault_path_  = (*res)[0];
                create_mode_ = !std::filesystem::exists(vault_path_);
            } else if (pending_ == Keyfile) {
                keyfile_path_ = (*res)[0];
                error_.clear();  // a freshly picked keyfile invalidates old errors
            } else if (pending_ == NewKeyfile) {
                if (platform::write_new_keyfile((*res)[0])) {
                    keyfile_path_ = (*res)[0];
                    error_.clear();
                } else {
                    error_ = "Could not create the keyfile.";
                }
            }
        }
        pending_ = None;
    }
}

void UnlockScreen::submit()
{
    using enum vault::VaultResult;
    error_.clear();

    if (vault_path_.empty()) {
        error_ = "Please select a vault file.";
        return;
    }

    std::vector<uint8_t> keyfile;
    if (!keyfile_path_.empty()) {
        auto kf = platform::read_file(keyfile_path_);
        if (!kf) { error_ = "Cannot read keyfile."; return; }
        keyfile = std::move(*kf);
    }

    const SubmitDecision d = decide_submit(create_mode_, pw_.bytes(), confirm_.bytes());
    if (d.error) {
        error_ = d.error;
        if (!keyfile.empty()) crypto_wipe(keyfile.data(), keyfile.size());
        return;
    }

    vault::VaultResult r;
    if (d.action == SubmitAction::Create) {
        r = vault::Vault::create(vault_path_.string(), pw_.bytes(), keyfile,
                                 crypto::DEFAULT_KDF_PARAMS, vault_);
    } else {
        r = vault::Vault::open(vault_path_.string(), vault_);
        if (r == Ok) r = vault_.unlock(pw_.bytes(), keyfile);
    }
    if (!keyfile.empty()) crypto_wipe(keyfile.data(), keyfile.size());

    if (r == Ok) {
        pw_.clear();
        confirm_.clear();
        reveal_pw_ = false;
        request(NavKind::ToGallery);
        return;
    }
    switch (r) {
        case AuthFailed: error_ = "Wrong password or keyfile."; break;
        case BadFormat:  error_ = "Not a valid vault file.";    break;
        case IoError:    error_ = "Could not read/write the vault file."; break;
        default:         error_ = "Unlock failed.";             break;
    }
}

void UnlockScreen::render(gfx::Renderer& r)
{
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    r.draw_text(font_, 60, 50, create_mode_ ? "Create Vault" : "Unlock Vault",
                gfx::Color{240, 240, 245, 255});
    r.draw_text(font_, 60, 100, "Vault: " + vault_path_.string(),
                gfx::Color{150, 150, 160, 255});

    const float fx = 60;
    const float fw = W - 120;
    const float fh = 44;
    r.draw_text(font_, fx, 134, "Password", gfx::Color{150, 150, 160, 255});
    draw_text_field(r, font_, {fx, 160, fw, fh},
                    std::string(pw_.length(), '*'), !create_mode_ || focus_ == 0);
    if (create_mode_) {
        r.draw_text(font_, fx, 234, "Confirm", gfx::Color{150, 150, 160, 255});
        draw_text_field(r, font_, {fx, 260, fw, fh},
                        std::string(confirm_.length(), '*'), focus_ == 1);

        // The password is the vault's real security boundary: show what the
        // user is committing to.
        if (!pw_.empty()) {
            const Strength s = classify_strength(pw_.bytes());
            const gfx::Color col = s == Strength::Weak   ? gfx::Color{230, 120, 120, 255}
                                 : s == Strength::Medium ? gfx::Color{230, 200, 110, 255}
                                                         : gfx::Color{130, 220, 140, 255};
            r.draw_text(font_, fx + 110, 134,
                        std::string("strength: ") += strength_label(s), col);
        }

        const Layout L0 = layout();
        draw_button(r, font_, {L0.generate_btn, "Generate passphrase"}, false, false);
        draw_button(r, font_, {L0.new_keyfile_btn, "New keyfile..."}, false, false);
        if (reveal_pw_ && !pw_.empty()) {
            // string_view straight over the mlock'd buffer — no unlocked copy.
            r.draw_text(font_, fx, 372,
                        std::string_view(reinterpret_cast<const char*>(pw_.bytes().data()),
                                         pw_.length()),
                        gfx::Color{200, 210, 160, 255});
            r.draw_text(font_, fx, 398, "Write this down, then press Create.",
                        gfx::Color{150, 150, 160, 255});
        }
    }

    const Layout L = layout();
    draw_button(r, font_, {L.keyfile_btn,
                keyfile_path_.empty() ? "Keyfile: none" : "Keyfile: set"}, false, false);
    draw_button(r, font_, {L.other_btn, "Open other..."}, false, false);
    draw_button(r, font_, {L.mode_btn,
                create_mode_ ? "Have a vault?" : "New vault?"}, false, false);
    draw_button(r, font_, {L.submit_btn, create_mode_ ? "Create" : "Unlock"}, false, false);

    if (!error_.empty())
        r.draw_text(font_, 60, H - 70, error_, gfx::Color{230, 120, 120, 255});
}

} // namespace ui
