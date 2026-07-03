#include "ui/unlock_screen.h"

#include <monocypher.h>

#include <vector>

#include "crypto/kdf.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "ui/passphrase.h"
#include "ui/unlock_logic.h"
#include "ui/widgets.h"
#include "vault/vault.h"

namespace ui {

namespace {

gfx::Color strength_color(Strength s)
{
    using enum Strength;
    switch (s) {
        case Medium: return gfx::theme::WARN;
        case Strong: return gfx::theme::OK;
        case Weak:   break;
    }
    return gfx::theme::DANGER;
}

} // namespace

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
                case SDLK_ESCAPE:    request(NavKind::ToVaultManager); break;
                default: break;
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            mouse_x_ = e.motion.x;
            mouse_y_ = e.motion.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) mouse_down_ = false;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            handle_click(e.button);
            break;
        default: break;
    }
}

void UnlockScreen::handle_click(const SDL_MouseButtonEvent& b)
{
    mouse_down_ = (b.button == SDL_BUTTON_LEFT);
    mouse_x_ = b.x;
    mouse_y_ = b.y;

    const Layout L = layout();
    const SDL_FPoint p{b.x, b.y};
    if (point_in_rect(p.x, p.y, L.mode_btn)) {
        create_mode_ = !create_mode_; focus_ = 0; error_.clear();
        reveal_pw_ = false;
    } else if (create_mode_ && point_in_rect(p.x, p.y, L.generate_btn)) {
        // Fill both fields with one random passphrase and show it so the user
        // can write it down before creating the vault.
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
}

void UnlockScreen::update(double)
{
    if (auto res = dlg_.take_result()) {
        if (!res->empty()) apply_dialog_result((*res)[0]);
        pending_ = Pending::None;
        mark_dirty();   // keyfile/vault picker closed — repaint
    }
}

void UnlockScreen::apply_dialog_result(const std::string& path)
{
    using enum Pending;
    switch (pending_) {
        case Vault:
            vault_path_  = path;
            create_mode_ = !std::filesystem::exists(vault_path_);
            break;
        case Keyfile:
            keyfile_path_ = path;
            error_.clear();  // a freshly picked keyfile invalidates old errors
            break;
        case NewKeyfile:
            if (platform::write_new_keyfile(path)) {
                keyfile_path_ = path;
                error_.clear();
            } else {
                error_ = "Could not create the keyfile.";
            }
            break;
        case None:
            break;
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
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    // Draw a button wired to live hover/active state.
    auto btn = [&](const SDL_FRect& rect, std::string_view label) {
        const ButtonState s = button_state(rect, mouse_x_, mouse_y_, mouse_down_);
        draw_button(r, font_, {rect, std::string(label)}, s.hover, s.active);
    };

    r.draw_text(font_, 60, 44, create_mode_ ? "Create Vault" : "Unlock Vault", TEXT);
    r.draw_text(font_, 60, 92, "Vault: " + vault_path_.string(), TEXT_DIM);

    const float fx = 60;
    const float fw = W - 120;
    const float fh = 44;
    r.draw_text(font_, fx, 126, "Password", TEXT_DIM);
    draw_text_field(r, font_, {fx, 160, fw, fh},
                    std::string(pw_.length(), '*'), !create_mode_ || focus_ == 0);
    if (create_mode_) {
        r.draw_text(font_, fx, 226, "Confirm", TEXT_DIM);
        draw_text_field(r, font_, {fx, 260, fw, fh},
                        std::string(confirm_.length(), '*'), focus_ == 1);

        // The password is the vault's real security boundary: show what the
        // user is committing to.
        if (!pw_.empty()) {
            const Strength s = classify_strength(pw_.bytes());
            std::string label = "strength: ";
            label += strength_label(s);
            r.draw_text(font_, fx + 110, 126, label, strength_color(s));
        }

        const Layout L0 = layout();
        btn(L0.generate_btn, "Generate passphrase");
        btn(L0.new_keyfile_btn, "New keyfile...");
        if (reveal_pw_ && !pw_.empty()) {
            // string_view straight over the mlock'd buffer — no unlocked copy.
            r.draw_text(font_, fx, 372,
                        std::string_view(reinterpret_cast<const char*>(pw_.bytes().data()),
                                         pw_.length()),
                        gfx::Color{200, 210, 160, 255});
            r.draw_text(font_, fx, 398, "Write this down, then press Create.", TEXT_DIM);
        }
    }

    const Layout L = layout();
    btn(L.keyfile_btn, keyfile_path_.empty() ? "Keyfile: none" : "Keyfile: set");
    btn(L.other_btn, "Open other...");
    btn(L.mode_btn, create_mode_ ? "Have a vault?" : "New vault?");
    btn(L.submit_btn, create_mode_ ? "Create" : "Unlock");

    if (!error_.empty())
        r.draw_text(font_, 60, H - 70, error_, DANGER);
}

} // namespace ui
