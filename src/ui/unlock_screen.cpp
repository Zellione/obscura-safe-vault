#include "ui/unlock_screen.h"

#include <monocypher.h>

#include <optional>
#include <system_error>
#include <vector>

#include "crypto/kdf.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "platform/path_utf8.h"
#include "ui/clipboard_secret.h"
#include "ui/passphrase.h"
#include "ui/text_input_event.h"
#include "ui/unlock_logic.h"
#include "ui/widgets.h"
#include "vault/vault.h"

namespace ui {

namespace {

constexpr double CLIPBOARD_CLEAR_SECS = 25.0;

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

// std::filesystem::exists(path) (no error_code) throws on a query failure
// (e.g. a network-mapped drive hiccup) instead of just reporting false; an
// uncaught throw here would terminate() the whole app. Use the non-throwing
// overload, treating any query failure as "doesn't exist" (falls into create
// mode, same as a real absence).
bool exists_no_throw(const std::filesystem::path& path) noexcept
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

} // namespace

UnlockScreen::UnlockScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                           platform::FileDialog& dlg, std::filesystem::path vault_path)
    : win_(win), font_(font), vault_(vault), dlg_(dlg),
      vault_path_(std::move(vault_path)),
      create_mode_(!exists_no_throw(vault_path_))
{
}

void UnlockScreen::on_enter() { SDL_StartTextInput(win_.sdl_window()); }

void UnlockScreen::on_exit()
{
    SDL_StopTextInput(win_.sdl_window());
    pw_.clear();
    confirm_.clear();
    reveal_pw_ = false;
    crypto_wipe(clipboard_last_set_.data(), clipboard_last_set_.size());
    clipboard_last_set_.clear();
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
        .copy_btn        = {W - 60.0f - 70.0f, 118.0f, 70.0f, 26.0f},
    };
}

void UnlockScreen::handle_event(const SDL_Event& e)
{
    // While the KDF worker owns the vault, swallow ALL input: a second submit
    // would race the job, editing fields mid-derivation is misleading, and Esc
    // would tear the screen down under a worker holding &vault_ (the job dtor
    // would join, but the derivation is not cancellable anyway).
    if (job_.active()) return;

    // Precedence rule (Phase 54): the focused field gets first refusal on every
    // event, so its Ctrl+A / Ctrl+V never fall through to a screen shortcut.
    SecureTextInput& f = (create_mode_ && focus_ == 1) ? confirm_ : pw_;
    if (e.type == SDL_EVENT_TEXT_INPUT || e.type == SDL_EVENT_KEY_DOWN) {
        const size_t before = f.size();
        if (handle_text_input_event(f, e)) {
            if (f.size() != before) reveal_pw_ = false;   // edited by hand: stop displaying it
            return;
        }
    }

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (e.key.key) {
                case SDLK_TAB:       if (create_mode_) focus_ ^= 1; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:  submit(); break;
                case SDLK_ESCAPE:    request(NavKind::ToVaultManager); break;
                default: break;
            }
            break;
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
            confirm_.set_text(pw_.text_view());   // view straight over the mlock'd bytes
            reveal_pw_ = true;
            error_.clear();
            copy_password_to_clipboard();   // Phase 45 Part 3: auto-copy the generated passphrase
        }
    } else if (create_mode_ && point_in_rect(p.x, p.y, L.new_keyfile_btn)) {
        pending_ = Pending::NewKeyfile; dlg_.save_keyfile(win_.sdl_window());
    } else if (point_in_rect(p.x, p.y, L.keyfile_btn)) {
        pending_ = Pending::Keyfile; dlg_.open_keyfile(win_.sdl_window());
    } else if (point_in_rect(p.x, p.y, L.other_btn)) {
        pending_ = Pending::Vault;   dlg_.open_vault(win_.sdl_window());
    } else if (point_in_rect(p.x, p.y, L.submit_btn)) {
        submit();
    } else if (point_in_rect(p.x, p.y, L.copy_btn)) {
        copy_password_to_clipboard();
    }
}

// Human-facing wording for a failed open/unlock/create (no secrets).
static const char* unlock_error_message(vault::VaultResult r)
{
    using enum vault::VaultResult;
    switch (r) {
        case AuthFailed: return "Wrong password or keyfile.";
        case BadFormat:  return "Not a valid vault file.";
        case IoError:    return "Could not read/write the vault file.";
        default:         return "Unlock failed.";
    }
}

void UnlockScreen::update(double dt)
{
    // Collect the KDF worker's outcome (animating() keeps frames ticking
    // while it runs, so this polls promptly).
    if (auto oc = job_.take_outcome()) {
        if (*oc == vault::VaultResult::Ok) {
            pw_.clear();
            confirm_.clear();
            reveal_pw_ = false;
            request(NavKind::ToGallery);
        } else {
            error_ = unlock_error_message(*oc);
        }
        mark_dirty();
    }

    if (auto res = dlg_.take_result()) {
        if (!res->empty()) apply_dialog_result((*res)[0]);
        pending_ = Pending::None;
        mark_dirty();   // keyfile/vault picker closed — repaint
    }

    if (clipboard_clear_timer_ < 0.0) return;
    clipboard_clear_timer_ += dt;
    if (clipboard_clear_timer_ < CLIPBOARD_CLEAR_SECS) return;

    clipboard_clear_timer_ = -1.0;
    std::optional<std::string> current;
    if (char* cur = SDL_GetClipboardText()) {
        current = cur;
        SDL_free(cur);
    }
    if (should_clear_clipboard(current, clipboard_last_set_)) { SDL_SetClipboardText(""); }
    crypto_wipe(clipboard_last_set_.data(), clipboard_last_set_.size());
    clipboard_last_set_.clear();
}

void UnlockScreen::apply_dialog_result(const std::string& path)
{
    using enum Pending;
    switch (pending_) {
        case Vault:
            vault_path_  = path;
            create_mode_ = !exists_no_throw(vault_path_);
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

void UnlockScreen::copy_password_to_clipboard()
{
    if (pw_.empty()) return;
    std::string tmp(pw_.text_view());
    SDL_SetClipboardText(tmp.c_str());
    crypto_wipe(clipboard_last_set_.data(), clipboard_last_set_.size());
    clipboard_last_set_   = tmp;
    clipboard_clear_timer_ = 0.0;
    crypto_wipe(tmp.data(), tmp.size());
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

    // Hand the KDF to the worker; the job copies password + keyfile into its
    // own mlock'd buffers before returning, so both can be wiped/kept here.
    // update() collects the outcome and navigates / reports the error.
    if (d.action == SubmitAction::Create) {
        job_.start_create(vault_, platform::path_to_utf8(vault_path_), pw_.bytes(), keyfile,
                          crypto::DEFAULT_KDF_PARAMS);
    } else {
        job_.start_unlock(vault_, platform::path_to_utf8(vault_path_), pw_.bytes(), keyfile);
    }
    if (!keyfile.empty()) crypto_wipe(keyfile.data(), keyfile.size());
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
    r.draw_text(font_, 60, 92,
                fit_text(font_, "Vault: " + platform::path_to_utf8(vault_path_), W - 120), TEXT_DIM);

    const float fx = 60;
    const float fw = W - 120;
    const float fh = 44;
    r.draw_text(font_, fx, 126, "Password", TEXT_DIM);
    draw_edit_field(r, font_, {fx, 160, fw, fh}, pw_, pw_chrome_,
                    !create_mode_ || focus_ == 0, /*mask*/ true);
    if (create_mode_) {
        r.draw_text(font_, fx, 226, "Confirm", TEXT_DIM);
        draw_edit_field(r, font_, {fx, 260, fw, fh}, confirm_, confirm_chrome_,
                        focus_ == 1, /*mask*/ true);

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
            r.draw_text(font_, fx, 372, pw_.text_view(), gfx::Color{200, 210, 160, 255});
            r.draw_text(font_, fx, 398, "Write this down, then press Create.", TEXT_DIM);
        }
    }

    const Layout L = layout();
    btn(L.keyfile_btn, keyfile_path_.empty() ? "Keyfile: none" : "Keyfile: set");
    btn(L.other_btn, "Open other...");
    btn(L.mode_btn, create_mode_ ? "Have a vault?" : "New vault?");
    btn(L.submit_btn, create_mode_ ? "Create" : "Unlock");
    btn(L.copy_btn, "Copy");

    if (job_.active()) {
        // KDF in flight: input is swallowed (handle_event) until the worker
        // hands back its outcome, so tell the user why nothing reacts.
        r.draw_text(font_, 60, H - 70,
                    create_mode_ ? "Creating vault — deriving key…" : "Deriving key…", TEXT_DIM);
    } else if (!error_.empty()) {
        r.draw_text(font_, 60, H - 70, error_, DANGER);
    }
}

std::vector<ui::HelpGroup> UnlockScreen::help_groups() const
{
    return {{"Unlock", {
        {"Tab", "Switch field (create mode)"}, {"Enter", "Submit"},
        {"Esc", "Back to vault manager"},
    }},
    text_editing_help_group()};
}

} // namespace ui
