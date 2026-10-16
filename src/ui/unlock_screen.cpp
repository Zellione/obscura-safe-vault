#include "ui/unlock_screen.h"

#include <monocypher.h>

#include <algorithm>
#include <format>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "crypto/kdf.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/path_utf8.h"
#include "platform/paths.h"
#include "ui/clipboard_gate.h"
#include "ui/clipboard_secret.h"
#include "ui/passphrase.h"
#include "ui/progress_modal.h"
#include "ui/text_input_event.h"
#include "ui/text_metrics.h"
#include "ui/unlock_logic.h"
#include "ui/widgets.h"
#include "vault/vault.h"

namespace ui {

namespace {

constexpr double CLIPBOARD_CLEAR_SECS = 25.0;
constexpr SDL_FRect LEGACY_BROWSE_BUTTON{60, 240, 260, 44};
constexpr SDL_FRect LEGACY_CONVERT_BUTTON{60, 300, 260, 44};

size_t conversion_error_line_end(std::string_view text, const gfx::FontAtlas& font, float width)
{
    size_t end = text.size();
    while (end > 0 && static_cast<float>(font.measure(text.substr(0, end))) > width) {
        const auto space = text.rfind(' ', end - 1);
        if (space == std::string_view::npos || space == 0) break;
        end = space;
    }
    return end;
}

gfx::Color strength_color(Strength s)
{
    using enum Strength;
    switch (s) {
    case Medium:
        return gfx::theme::WARN;
    case Strong:
        return gfx::theme::OK;
    case Weak:
        break;
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
    : win_(win), font_(font), vault_(vault), dlg_(dlg), vault_path_(std::move(vault_path)),
      create_mode_(!exists_no_throw(vault_path_))
{}

void UnlockScreen::on_enter()
{
    SDL_StartTextInput(win_.sdl_window());
}

void UnlockScreen::on_exit()
{
    SDL_StopTextInput(win_.sdl_window());
    password_.pw.clear();
    password_.confirm.clear();
    password_.reveal = false;
    crypto_wipe(clipboard_.last_set.data(), clipboard_.last_set.size());
    clipboard_.last_set.clear();
}

UnlockScreen::Layout UnlockScreen::layout() const
{
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());
    const float bw = 200.0f;
    const float bh = 44.0f;
    const float gap = 16.0f;
    const float row = H - 140.0f;
    if (conversion_mode_) {
        const auto form = conversion_form_layout(W, font_.pixel_height(), conversion_scroll_);
        const float step = form.step;
        const float third = std::max(1.0f, (form.width - 32) / 3);
        return Layout{
            .keyfile_btn = {60, form.row(10), third, step - 8},
            .other_btn = {60, form.row(3), std::min(340.0f, form.width), step - 8},
            .mode_btn = {60 + 2 * (third + 16), form.row(10), third, step - 8},
            .submit_btn = {60, form.row(13), std::min(300.0f, form.width), step - 8},
            .generate_btn = {60, form.row(8), std::min(340.0f, form.width), step - 8},
            .new_keyfile_btn = {60 + third + 16, form.row(10), third, step - 8},
            .copy_btn = {},
        };
    }
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

bool UnlockScreen::handle_conversion_input(const SDL_Event& e)
{
    if (conversion_mode_) {
        const auto form = conversion_form_layout(static_cast<float>(win_.width()),
                                                 font_.pixel_height(), conversion_scroll_);
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            size_t error_rows = 0;
            std::string_view remaining = error_;
            while (!remaining.empty()) {
                remaining.remove_prefix(conversion_error_line_end(remaining, font_, form.width));
                ++error_rows;
            }
            const float rows = std::max(19.0f, 15.0f + static_cast<float>(error_rows));
            const float max_scroll =
                std::max(0.0f, 24 + rows * form.step - static_cast<float>(win_.height()));
            conversion_scroll_ =
                std::clamp(conversion_scroll_ - e.wheel.y * form.step, 0.0f, max_scroll);
            mark_dirty();
            return true;
        }
        if (!conversion_job_.active() && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            e.button.button == SDL_BUTTON_LEFT) {
            if (point_in_rect(e.button.x, e.button.y, form.password_field(false)))
                password_.focus = 0;
            if (point_in_rect(e.button.x, e.button.y, form.password_field(true)))
                password_.focus = 1;
        }
    }
    return false;
}

void UnlockScreen::handle_event(const SDL_Event& e)
{
    if (handle_conversion_input(e)) return;
    if (handle_blocking_event(e)) return;
    if (handle_legacy_choice(e)) return;
    if (handle_text_event(e)) return;

    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        switch (e.key.key) {
        case SDLK_TAB:
            if (create_mode_ || conversion_mode_) password_.focus ^= 1;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            submit();
            break;
        case SDLK_ESCAPE:
            if (conversion_mode_) {
                conversion_mode_ = false;
                legacy_choice_ = true;
                password_.pw.clear();
                password_.confirm.clear();
                password_.reveal = false;
                error_.clear();
            } else {
                request(NavKind::ToVaultManager);
            }
            break;
        default:
            break;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        mouse_.x = e.motion.x;
        mouse_.y = e.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) mouse_.down = false;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        handle_click(e.button);
        break;
    default:
        break;
    }
}

bool UnlockScreen::handle_legacy_choice(const SDL_Event& e)
{
    if (!legacy_choice_) return false;
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        mouse_.x = e.motion.x;
        mouse_.y = e.motion.y;
    }
    bool browse = false;
    bool convert = false;
    if (e.type == SDL_EVENT_KEY_DOWN) {
        browse = e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER;
        convert = e.key.key == SDLK_C;
        if (e.key.key == SDLK_ESCAPE) request(NavKind::ToVaultManager);
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        browse = point_in_rect(e.button.x, e.button.y, LEGACY_BROWSE_BUTTON);
        convert = point_in_rect(e.button.x, e.button.y, LEGACY_CONVERT_BUTTON);
    }
    if (browse) request(NavKind::ToGallery);
    if (convert) {
        legacy_choice_ = false;
        conversion_mode_ = true;
        conversion_report_.reset();
        mark_dirty();
    }
    return true;
}

void UnlockScreen::render_legacy_choice(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const float width = static_cast<float>(win_.width()) - 120;
    r.draw_text(font_, 60, 44, "Legacy Vault", TEXT);
    r.draw_text(font_, 60, 110,
                fit_text(font_, "Browse this vault read-only. Conversion is optional.", width),
                TEXT_DIM);
    r.draw_text(
        font_, 60, 160,
        fit_text(font_, "Convert a copy to enable editing; the original stays unchanged.", width),
        TEXT_DIM);
    for (const Button& b : {Button{LEGACY_BROWSE_BUTTON, "Open read-only [Enter]"},
                            Button{LEGACY_CONVERT_BUTTON, "Convert a copy [C]"}}) {
        const auto state = button_state(b.rect, mouse_.x, mouse_.y, false);
        draw_button(r, font_, {b.rect, fit_text(font_, b.label, b.rect.w - 24)}, state.hover,
                    state.active);
    }
}

bool UnlockScreen::handle_blocking_event(const SDL_Event& e)
{
    if (conversion_report_ && conversion_report_->status == vault::LegacyConversionStatus::Ok) {
        if (e.type == SDL_EVENT_KEY_DOWN &&
            (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER))
            request(NavKind::ToGallery, platform::path_to_utf8(destination_path_));
        return true;
    }
    if (conversion_job_.active()) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) conversion_job_.cancel();
        return true;
    }
    // While the KDF worker owns the vault, swallow ALL input: a second submit
    // would race the job, editing fields mid-derivation is misleading, and Esc
    // would tear the screen down under a worker holding &vault_ (the job dtor
    // would join, but the derivation is not cancellable anyway).
    return job_.active();
}

bool UnlockScreen::handle_text_event(const SDL_Event& e)
{
    // Precedence rule (Phase 54): the focused field gets first refusal on every
    // event, so its Ctrl+A / Ctrl+V never fall through to a screen shortcut.
    SecureTextInput& f = ((create_mode_ || conversion_mode_) && password_.focus == 1)
                             ? password_.confirm
                             : password_.pw;
    if (e.type == SDL_EVENT_TEXT_INPUT || e.type == SDL_EVENT_KEY_DOWN) {
        const size_t before = f.size();
        if (handle_text_input_event(f, e)) {
            if (f.size() != before) password_.reveal = false;   // edited by hand: stop displaying it
            return true;
            }
    }
    return false;
}

void UnlockScreen::handle_click(const SDL_MouseButtonEvent& b)
{
    mouse_.down = (b.button == SDL_BUTTON_LEFT);
    mouse_.x = b.x;
    mouse_.y = b.y;

    const Layout L = layout();
    const SDL_FPoint p{b.x, b.y};
    if (conversion_mode_ && point_in_rect(p.x, p.y, L.mode_btn)) {
        keyfile_path_.clear();
        error_.clear();
    } else if (!conversion_mode_ && point_in_rect(p.x, p.y, L.mode_btn)) {
        create_mode_ = !create_mode_;
        password_.focus = 0;
        error_.clear();
        password_.reveal = false;
    } else if ((create_mode_ || conversion_mode_) && point_in_rect(p.x, p.y, L.generate_btn)) {
        // Fill both fields with one random passphrase and show it so the user
        // can write it down before creating the vault.
        if (generate_passphrase(password_.pw)) {
            password_.confirm.set_text(
                password_.pw.text_view());  // view straight over the mlock'd bytes
            password_.reveal = true;
            error_.clear();
            copy_password_to_clipboard();   // Phase 45 Part 3: auto-copy the generated passphrase
        }
    } else if ((create_mode_ || conversion_mode_) && point_in_rect(p.x, p.y, L.new_keyfile_btn)) {
        pending_ = Pending::NewKeyfile;
        dlg_.save_keyfile(win_.sdl_window());
    } else if (point_in_rect(p.x, p.y, L.keyfile_btn)) {
        pending_ = Pending::Keyfile;
        dlg_.open_keyfile(win_.sdl_window());
    } else if (point_in_rect(p.x, p.y, L.other_btn)) {
        if (conversion_mode_) {
            pending_ = Pending::Destination;
            dlg_.open_vault_parent(win_.sdl_window());
        } else {
            pending_ = Pending::Vault;
            dlg_.open_vault_directory(win_.sdl_window());
        }
    } else if (point_in_rect(p.x, p.y, L.submit_btn)) {
        submit();
    } else if (!conversion_mode_ && point_in_rect(p.x, p.y, L.copy_btn)) {
        copy_password_to_clipboard();
    }
}

// Human-facing wording for a failed open/unlock/create (no secrets).
static const char* unlock_error_message(vault::VaultResult r)
{
    using enum vault::VaultResult;
    switch (r) {
    case AuthFailed:
        return "Wrong password or keyfile.";
    case BadFormat:
        return "Not a valid vault file.";
    case IoError:
        return "Could not read/write the vault file.";
    case Busy:
        return "Vault is busy in another process.";
    case IncompleteConversion:
        return "Conversion incomplete. Open the original vault and resume conversion.";
    case Damaged:
        return "Vault is damaged: missing or inconsistent media. Restore a verified backup.";
    case UnsupportedVersion:
        return "Vault was created by a newer unsupported version.";
    case AlreadyExists:
        return "A vault already exists at that path.";
    default:
        return "Unlock failed.";
    }
}

void UnlockScreen::update(double dt)
{
    // Phase 92: a gated (Warn) password copy writes only once the App's confirm
    // modal completes; arm the auto-clear timer here when that write lands.
    if (auto copy = take_confirmed_copy(); copy && copy->sensitive) {
        crypto_wipe(clipboard_.last_set.data(), clipboard_.last_set.size());
        clipboard_.last_set    = std::move(copy->text);
        clipboard_.clear_timer = 0.0;
    }

    // Collect the KDF worker's outcome (animating() keeps frames ticking
    // while it runs, so this polls promptly).
    if (auto oc = job_.take_outcome()) {
        if (*oc == vault::VaultResult::Ok) {
            password_.pw.clear();
            password_.confirm.clear();
            password_.reveal = false;
            if (vault::vault_is_read_only(vault_)) {
                legacy_choice_ = true;
                create_mode_ = false;
                keyfile_path_.clear();
                error_.clear();
            } else {
                request(NavKind::ToGallery);
            }
        } else {
            error_ = unlock_error_message(*oc);
        }
        mark_dirty();
    }

    if (auto converted = conversion_job_.take_outcome()) {
        conversion_report_ = *converted;
        if (converted->status != vault::LegacyConversionStatus::Ok)
            error_ = conversion_error_message(*converted);
        password_.pw.clear();
        password_.confirm.clear();
        password_.reveal = false;
        mark_dirty();
    }

    if (auto res = dlg_.take_result()) {
        if (!res->empty()) apply_dialog_result((*res)[0]);
        pending_ = Pending::None;
        mark_dirty();   // keyfile/vault picker closed — repaint
    }

    if (clipboard_.clear_timer < 0.0) return;
    clipboard_.clear_timer += dt;
    if (clipboard_.clear_timer < CLIPBOARD_CLEAR_SECS) return;

    clipboard_.clear_timer = -1.0;
    std::optional<std::string> current;
    if (char* cur = SDL_GetClipboardText()) {
        current = cur;
        SDL_free(cur);
    }
    if (should_clear_clipboard(current, clipboard_.last_set)) {
        SDL_SetClipboardText("");
    }
    crypto_wipe(clipboard_.last_set.data(), clipboard_.last_set.size());
    clipboard_.last_set.clear();
}

void UnlockScreen::apply_dialog_result(const std::string& path)
{
    using enum Pending;
    switch (pending_) {
        case Vault:
            vault_path_  = platform::utf8_to_path(path);
            create_mode_ = !exists_no_throw(vault_path_);
            break;
        case Destination:
            destination_path_ =
                platform::utf8_to_path(path) / (vault_path_.stem().native() + "-converted.osv");
            error_.clear();
            break;
        case Keyfile:
            keyfile_path_ = path;
            error_.clear();  // a freshly picked keyfile invalidates old errors
            break;
        case NewKeyfile:
            if (platform::write_new_keyfile(platform::utf8_to_path(path))) {
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
    if (password_.pw.empty()) return;
    std::string tmp(password_.pw.text_view());
    using enum ClipboardGateAction;
    switch (clipboard_gate_action(clipboard_gate())) {
    case Refuse:
        // Disable: a deliberate no-op — the OS clipboard is a persistent
        // cross-process plaintext sink, and the user switched the gate off.
        crypto_wipe(tmp.data(), tmp.size());
        return;
    case Confirm:
        // Warn: park for the App's default-cancel confirm. The auto-clear
        // timer is armed only once the confirm actually writes (update()
        // polls take_confirmed_copy), so a declined copy never arms a clear.
        (void)request_clipboard_confirm(std::move(tmp), /*sensitive=*/true);
        return;
    case Copy:
        break;
    }
    // Allow: today's behaviour, unchanged.
    SDL_SetClipboardText(tmp.c_str());
    crypto_wipe(clipboard_.last_set.data(), clipboard_.last_set.size());
    clipboard_.last_set    = tmp;
    clipboard_.clear_timer = 0.0;
    crypto_wipe(tmp.data(), tmp.size());
}

void UnlockScreen::submit()
{
    using enum vault::VaultResult;
    error_.clear();

    if (vault_path_.empty()) {
        error_ = "Please select a vault path.";
        return;
    }

    if (conversion_mode_ && destination_path_.empty()) {
        error_ = "Please select a destination folder.";
        return;
    }

    crypto::SecureBytes keyfile;
    if (!keyfile_path_.empty()) {
        auto kf = platform::read_keyfile(platform::utf8_to_path(keyfile_path_));
        if (!kf) {
            error_ = "Cannot read keyfile.";
            return;
        }
        keyfile = std::move(*kf);
    }

    const SubmitDecision d = decide_submit(create_mode_ || conversion_mode_, password_.pw.bytes(),
                                            password_.confirm.bytes());
    if (d.error) {
        error_ = d.error;
        return;  // keyfile (a SecureBytes) wipes itself on scope exit
    }

    // Hand the KDF to the worker; the job copies password + keyfile into its
    // own mlock'd buffers before returning, so both can be wiped/kept here.
    // update() collects the outcome and navigates / reports the error.
    if (conversion_mode_) {
        conversion_report_.reset();
        if (!conversion_job_.start(vault_, destination_path_, password_.pw.bytes(),
                                   keyfile.as_span(), crypto::DEFAULT_KDF_PARAMS))
            error_ = "Could not start conversion.";
    } else if (d.action == SubmitAction::Create) {
        job_.start_create(vault_, platform::path_to_utf8(vault_path_), password_.pw.bytes(),
                          keyfile.as_span(), crypto::DEFAULT_KDF_PARAMS);
    } else {
        job_.start_unlock(vault_, platform::path_to_utf8(vault_path_), password_.pw.bytes(),
                          keyfile.as_span());
    }
}

void UnlockScreen::render(gfx::Renderer& r)
{
    if (conversion_job_.active()) {
        const auto text = conversion_progress_text(
            conversion_job_.conversion_progress().phase.load(),
            conversion_job_.progress().done.load(), conversion_job_.progress().total.load(),
            conversion_job_.conversion_progress().bytes.load());
        draw_op_progress(r, font_, static_cast<float>(win_.width()),
                         static_cast<float>(win_.height()),
                         {.title = text.title,
                          .count_line = text.count_line,
                          .done = text.done,
                          .total = text.total});
        return;
    }
    if (legacy_choice_) {
        render_legacy_choice(r);
        return;
    }
    if (conversion_mode_) {
        render_conversion(r);
        return;
    }
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    // Draw a button wired to live hover/active state.
    auto btn = [&](const SDL_FRect& rect, std::string_view label) {
        const ButtonState s = button_state(rect, mouse_.x, mouse_.y, mouse_.down);
        draw_button(r, font_, {rect, fit_text(font_, label, rect.w - 24)}, s.hover, s.active);
    };

    r.draw_text(font_, 60, 44, create_mode_ ? "Create Vault" : "Unlock Vault", TEXT);
    r.draw_text(font_, 60, 92,
                fit_text(font_, "Vault: " + platform::path_to_utf8(vault_path_), W - 120),
                TEXT_DIM);

    render_credentials(r, W);

    const Layout L = layout();
    btn(L.keyfile_btn, keyfile_path_.empty() ? "Keyfile: none" : "Keyfile: set");
    btn(L.other_btn, "Open other...");
    btn(L.mode_btn, create_mode_ ? "Have a vault?" : "New vault?");
    btn(L.submit_btn, create_mode_ ? "Create" : "Unlock");
    btn(L.copy_btn, "Copy");
    render_status(r, W, H);
}

void UnlockScreen::render_conversion(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto form = conversion_form_layout(static_cast<float>(win_.width()), font_.pixel_height(),
                                             conversion_scroll_);
    const float width = form.width;
    const auto y = [&form](float row) { return form.row(row); };
    const auto text = [&](float row, std::string_view label, gfx::Color color) {
        r.draw_text(font_, 60, y(row), fit_text(font_, label, width), color);
    };
    const auto button = [&](const SDL_FRect& rect, std::string_view label) {
        const auto state = button_state(rect, mouse_.x, mouse_.y, mouse_.down);
        draw_button(r, font_, {rect, fit_text(font_, label, rect.w - 24)}, state.hover,
                    state.active);
    };
    text(0, "Convert a copy of your vault", TEXT);
    text(1, "The source is already unlocked. Conversion leaves the original unchanged.", TEXT_DIM);
    text(2, "Source: " + platform::path_to_utf8(vault_path_), TEXT_DIM);
    const auto L = layout();
    button(L.other_btn, "Choose destination...");
    text(4,
         destination_path_.empty() ? "New vault: choose a parent folder above."
                                   : "New vault: " + platform::path_to_utf8(destination_path_),
         TEXT_DIM);
    text(5, "Choose credentials for the new vault. They may differ from the source.", TEXT);
    const float field_width = (width - 24) / 2;
    r.draw_text(font_, 60, y(6), fit_text(font_, "New vault password", field_width), TEXT_DIM);
    r.draw_text(font_, 84 + field_width, y(6), fit_text(font_, "Confirm new password", field_width),
                TEXT_DIM);
    const SDL_FRect password_rect = form.password_field(false);
    const SDL_FRect confirm_rect = form.password_field(true);
    draw_edit_field(r, font_, password_rect, password_.pw, password_.pw_chrome,
                    password_.focus == 0, true);
    draw_edit_field(r, font_, confirm_rect, password_.confirm, password_.confirm_chrome,
                    password_.focus == 1, true);
    button(L.generate_btn, "Generate passphrase");
    if (!password_.pw.empty()) {
        const auto strength = classify_strength(password_.pw.bytes());
        r.draw_text(
            font_, 420, y(8),
            fit_text(font_, std::format("Strength: {}", strength_label(strength)), width - 360),
            strength_color(strength));
    }
    text(9, "New vault keyfile (optional): choose an existing file or create one.", TEXT);
    button(L.keyfile_btn, "Use existing file...");
    button(L.new_keyfile_btn, "Create keyfile...");
    button(L.mode_btn, "Use none");
    text(11,
         keyfile_path_.empty() ? "No keyfile: the new vault will require only its password."
                               : "Required to open the new vault: " + keyfile_path_,
         TEXT_DIM);
    if (password_.reveal && !password_.pw.empty())
        text(12, password_.pw.text_view(), OK);
    else
        text(12, "Resuming? Use the same destination, password and keyfile as the first attempt.",
             TEXT_DIM);
    button(L.submit_btn, "Convert copy");
    render_conversion_status(r);
}

void UnlockScreen::render_conversion_status(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto form = conversion_form_layout(static_cast<float>(win_.width()), font_.pixel_height(),
                                             conversion_scroll_);
    const float width = form.width;
    const auto text = [&](float row, std::string_view label, gfx::Color color) {
        r.draw_text(font_, 60, form.row(row), fit_text(font_, label, width), color);
    };
    if (conversion_report_ && conversion_report_->status == vault::LegacyConversionStatus::Ok) {
        text(14, "Copy verified. Original preserved. Press Enter to open the new vault.", OK);
        text(15, "Back up and test the new vault before deleting the original.", TEXT_DIM);
        text(16,
             std::format("Copied {} galleries, {} images and {} videos.",
                         conversion_report_->galleries, conversion_report_->images,
                         conversion_report_->videos),
             TEXT_DIM);
        if (conversion_report_->missing_derived)
            text(17,
                 std::format("{} source thumbnails/posters were absent; originals are intact.",
                             conversion_report_->missing_derived),
                 WARN);
    } else if (!error_.empty()) {
        // Keep both the cause and recovery instruction visible, rather than
        // eliding the middle of the diagnostic on smaller windows.
        std::string_view remaining = error_;
        int row = 14;
        while (!remaining.empty()) {
            const size_t end = conversion_error_line_end(remaining, font_, width);
            text(static_cast<float>(row), remaining.substr(0, end), DANGER);
            ++row;
            remaining.remove_prefix(end);
            while (remaining.starts_with(' '))
                remaining.remove_prefix(1);
        }
    }
}

void UnlockScreen::render_credentials(gfx::Renderer& r, float width)
{
    using namespace gfx::theme;
    const float fx = 60;
    const float fw = width - 120;
    const float fh = 44;
    r.draw_text(font_, fx, 126, "Password", TEXT_DIM);
    draw_edit_field(r, font_, {fx, 160, fw, fh}, password_.pw, password_.pw_chrome,
                    !create_mode_ || password_.focus == 0, /*mask*/ true);
    if (create_mode_) {
        r.draw_text(font_, fx, 226, "Confirm", TEXT_DIM);
        draw_edit_field(r, font_, {fx, 260, fw, fh}, password_.confirm, password_.confirm_chrome,
                        password_.focus == 1, /*mask*/ true);

        // The password is the vault's real security boundary: show what the
        // user is committing to.
        if (!password_.pw.empty()) {
            const Strength s = classify_strength(password_.pw.bytes());
            std::string label = "strength: ";
            label += strength_label(s);
            r.draw_text(font_, fx + 110, 126, label, strength_color(s));
        }

        const Layout L0 = layout();
        const auto draw = [&](const SDL_FRect& rect, std::string_view label) {
            const ButtonState state = button_state(rect, mouse_.x, mouse_.y, mouse_.down);
            draw_button(r, font_, {rect, std::string(label)}, state.hover, state.active);
        };
        draw(L0.generate_btn, "Generate passphrase");
        draw(L0.new_keyfile_btn, "New keyfile...");
        if (password_.reveal && !password_.pw.empty()) {
            // string_view straight over the mlock'd buffer — no unlocked copy.
            r.draw_text(font_, fx, 372, fit_text(font_, password_.pw.text_view(), fw), OK);
            r.draw_text(font_, fx, 398, "Write this down, then press Create.", TEXT_DIM);
        }
    }
}

void UnlockScreen::render_status(gfx::Renderer& r, float width, float height)
{
    using namespace gfx::theme;
    (void)width;
    if (job_.active()) {
        // KDF in flight: input is swallowed (handle_event) until the worker
        // hands back its outcome, so tell the user why nothing reacts.
        r.draw_text(font_, 60, height - 70,
                    create_mode_ ? "Creating vault — deriving key…" : "Deriving key…", TEXT_DIM);
    } else if (!error_.empty()) {
        r.draw_text(font_, 60, height - 70, error_, DANGER);
    }
}

std::vector<ui::HelpGroup> UnlockScreen::help_groups() const
{
    if (legacy_choice_)
        return {{"Legacy vault",
                 {{"Enter", "Open read-only"},
                  {"C", "Convert a copy"},
                  {"Esc", "Back to vault manager"}}}};
    std::vector<ui::HelpGroup> groups{
        {conversion_mode_ ? "Legacy conversion" : "Unlock",
         {
             {"Tab", "Switch password field"},
             {"Enter", "Submit"},
             {"Esc", conversion_mode_ ? "Back to read-only choice" : "Back to vault manager"},
         }},
        text_editing_help_group()};
    if (conversion_mode_)
        groups.front().entries.emplace_back("Mouse wheel", "Scroll conversion form");
    return groups;
}

} // namespace ui
