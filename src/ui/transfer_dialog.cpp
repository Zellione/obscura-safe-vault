#include "ui/transfer_dialog.h"

#include <monocypher.h>

#include <algorithm>
#include <format>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "platform/vault_registry.h"
#include "ui/keybindings.h"
#include "ui/progress_modal.h"
#include "ui/widgets.h"
#include "vault/transfer.h"

namespace ui {

namespace {
constexpr const char* kNewGalleryRow = "+ New gallery…";

// Clamp a list selection to [0, count-1] (0 when the list is empty).
int clamp_index(int sel, int count) noexcept
{
    if (count <= 0 || sel < 0) return 0;
    return sel > count - 1 ? count - 1 : sel;
}

// Map vault::VaultResult to a user-facing error message.
std::string unlock_error_message(vault::VaultResult r) noexcept
{
    using enum vault::VaultResult;
    if (r == AuthFailed) return "Wrong password or keyfile.";
    if (r == BadFormat)  return "Not a valid vault file.";
    return "Could not open the destination vault.";
}
} // namespace

TransferDialog::TransferDialog(vault::Vault& src, std::string src_path,
                               platform::VaultRegistry& registry,
                               platform::FileDialog& dlg, gfx::Window& win)
    : src_(src), src_path_(std::move(src_path)), registry_(registry),
      dlg_(dlg), win_(win) {}

void TransferDialog::open(std::string src_gallery, std::vector<std::string> filenames)
{
    active_       = true;
    source_       = Source::Images;
    stage_        = Stage::Mode;
    mode_         = vault::TransferMode::Move;
    dest_.is_self = false;
    src_gallery_  = std::move(src_gallery);
    filenames_    = std::move(filenames);
    vault_sel_    = 0;
    error_.clear();
    dest_.pw.clear();
    dest_.keyfile_path.clear();
    dest_.awaiting_keyfile = false;
    naming_ = false;
    name_buf_.clear();

    // Destination candidates: every known vault except the active one.
    candidates_.clear();
    for (const auto& p : registry_.list())
        if (p.string() != src_path_) candidates_.push_back(p);
    // "This vault" is always an available destination, so no empty-list error here.

    // The Unlock-stage password field and the new-gallery name overlay both consume
    // SDL_EVENT_TEXT_INPUT, which SDL3 only delivers while text input is active.
    SDL_StartTextInput(win_.sdl_window());
}

void TransferDialog::open_gallery(std::string src_gallery)
{
    open("", {});                  // reuse open() to reset all state + build candidates
    source_      = Source::Gallery;
    src_gallery_ = std::move(src_gallery);
}

void TransferDialog::close()
{
    if (dest_.vault.is_unlocked()) dest_.vault.lock();   // wipe the destination key
    dest_.pw.clear();
    SDL_StopTextInput(win_.sdl_window());                // restore the host's input state
    active_ = false;
}

// The vault the transfer writes into: the active vault for a same-vault transfer,
// otherwise the transiently-unlocked destination. Never lock src_ here — App owns it.
vault::Vault& TransferDialog::dest_vault() noexcept
{
    return dest_.is_self ? src_ : dest_.vault;
}

// --- stage transitions ----------------------------------------------------

void TransferDialog::choose_vault()
{
    error_.clear();
    if (vault_sel_ == 0) {                 // "This vault" — same-vault transfer
        dest_.is_self = true;
        rebuild_targets();
        stage_ = Stage::PickGallery;       // no unlock needed
        return;
    }
    const int ci = vault_sel_ - 1;         // rows after "This vault" are candidates_
    if (ci < 0 || ci >= static_cast<int>(candidates_.size())) return;
    dest_.is_self = false;
    dest_.path = candidates_[static_cast<size_t>(ci)].string();
    dest_.pw.clear();
    dest_.keyfile_path.clear();
    stage_ = Stage::Unlock;
}

void TransferDialog::try_unlock()
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
    rebuild_targets();
    stage_ = Stage::PickGallery;
}

void TransferDialog::rebuild_targets()
{
    const vault::Vault& dv = dest_vault();
    std::vector<std::string> targets = (source_ == Source::Gallery)
        ? vault::gallery_target_parents(dv)
        : vault::image_target_galleries(dv);
    picker_.set_items(std::move(targets));
    picker_.set_pinned_suffix(kNewGalleryRow);
}

void TransferDialog::choose_gallery()
{
    const auto& shown = picker_.filtered();
    const int sel = picker_.selected();
    if (sel < 0 || sel >= static_cast<int>(shown.size())) return;
    const std::string& picked = shown[static_cast<size_t>(sel)];
    if (picked == kNewGalleryRow) { naming_ = true; name_buf_.clear(); return; }
    do_move(picked);
}

void TransferDialog::do_move(std::string_view dst_target)
{
    vault::Vault& dv = dest_vault();
    const std::string where = dest_.is_self
        ? "this vault"
        : std::filesystem::path(dest_.path).stem().string();

    // Launch the move/copy on a worker thread so a large gallery never freezes the
    // UI (Phase 25). Do NOT close() yet — the transiently-unlocked destination
    // (dest_.vault) must stay alive for the job's life; the dialog stays active in
    // the Running stage showing a progress modal, and closes when update() drains
    // the outcome. The host grid stops touching the vault while job_active().
    if (source_ == Source::Gallery)
        run_.job.start_transfer_gallery(src_, src_gallery_, dv, std::string(dst_target),
                                        mode_, where);
    else
        run_.job.start_transfer_images(src_, src_gallery_, filenames_, dv, std::string(dst_target),
                                       mode_, where);
    stage_ = Stage::Running;
}

// --- per-stage key handlers ------------------------------------------------

bool TransferDialog::handle_mode_key(SDL_Keycode k)
{
    using enum vault::TransferMode;
    if (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT)
        mode_ = (mode_ == Move) ? Copy : Move;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) stage_ = Stage::PickVault;
    return true;
}

bool TransferDialog::handle_pick_vault_key(SDL_Keycode k)
{
    const auto n = static_cast<int>(candidates_.size()) + 1;   // +1 for the "This vault" row
    if (k == SDLK_UP)   vault_sel_ = clamp_index(vault_sel_ - 1, n);
    if (k == SDLK_DOWN) vault_sel_ = clamp_index(vault_sel_ + 1, n);
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) choose_vault();
    return true;
}

bool TransferDialog::handle_unlock_key(SDL_Keycode k)
{
    if (k == SDLK_BACKSPACE) dest_.pw.backspace();
    else if (k == SDLK_TAB) { dlg_.open_keyfile(win_.sdl_window()); dest_.awaiting_keyfile = true; }
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) try_unlock();
    return true;
}

bool TransferDialog::handle_gallery_key(SDL_Keycode k)
{
    if (k == SDLK_UP)   picker_.move(-1);
    if (k == SDLK_DOWN) picker_.move(1);
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) choose_gallery();
    return true;
}

bool TransferDialog::handle_naming_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_TEXT_INPUT) { name_buf_ += e.text.text; return true; }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;
    switch (e.key.key) {
        case SDLK_BACKSPACE:
            if (!name_buf_.empty()) name_buf_.pop_back();
            break;
        case SDLK_ESCAPE:
            naming_ = false;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (!name_buf_.empty() &&
                dest_vault().create_gallery(name_buf_) == vault::VaultResult::Ok) {
                rebuild_targets();
                naming_ = false;
                do_move(name_buf_);     // move straight into the new gallery
            } else {
                error_ = "Could not create that gallery.";
                naming_ = false;
            }
            break;
        default:
            break;
    }
    return true;
}

// --- input ----------------------------------------------------------------

bool TransferDialog::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    // While the transfer worker runs, swallow all input except Esc → cooperative
    // cancel (files committed so far remain; see transfer_gallery/transfer_images).
    if (stage_ == Stage::Running) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) run_.job.cancel();
        return true;
    }

    // New-gallery name entry (overlays the PickGallery stage).
    if (naming_) return handle_naming_event(e);

    // PickGallery filter typing (Phase 44 Part 1) — '/' opens it; Esc clears the
    // filter before the outer Esc-closes-the-dialog handling below ever sees it.
    if (stage_ == Stage::PickGallery && picker_.filter_open()) {
        if (e.type == SDL_EVENT_TEXT_INPUT) { picker_.filter_append(e.text.text); return true; }
        if (e.type == SDL_EVENT_KEY_DOWN) {
            switch (e.key.key) {
                case SDLK_BACKSPACE: picker_.filter_backspace(); return true;
                case SDLK_ESCAPE:
                    if (!picker_.filter().empty()) { picker_.filter_clear(); return true; }
                    picker_.close_filter();
                    return true;
                case SDLK_RETURN:
                case SDLK_KP_ENTER: choose_gallery(); return true;
                case SDLK_UP:       picker_.move(-1);  return true;
                case SDLK_DOWN:     picker_.move(1);   return true;
                default: return true;   // swallow other keys while typing a filter
            }
        }
        return true;
    }

    if (e.type == SDL_EVENT_TEXT_INPUT && stage_ == Stage::Unlock) {
        dest_.pw.push_utf8(e.text.text);
        return true;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // modal swallows other events

    const SDL_Keycode k = e.key.key;
    if (k == SDLK_ESCAPE) { close(); return true; }

    using enum Stage;
    switch (stage_) {
        case Mode:        return handle_mode_key(k);
        case PickVault:   return handle_pick_vault_key(k);
        case Unlock:      return handle_unlock_key(k);
        case PickGallery:
            if (is_search_key(e.key)) { picker_.open_filter(); return true; }
            return handle_gallery_key(k);
        case Running:     return true;   // handled above (Esc→cancel); nothing else
    }
    return true;
}

void TransferDialog::update()
{
    // Drain the background transfer once the worker finishes: record the status,
    // then close() (which wipes the destination key now that it is done with it).
    if (stage_ == Stage::Running) {
        if (auto oc = run_.job.take_outcome()) {
            run_.status = std::move(oc->status);
            run_.done   = true;
            close();
        }
        return;
    }

    if (!dest_.awaiting_keyfile) return;
    if (auto res = dlg_.take_result()) {
        dest_.awaiting_keyfile = false;
        if (!res->empty()) dest_.keyfile_path = (*res)[0];
    }
}

bool TransferDialog::consume_completed(std::string& status_out)
{
    if (!run_.done) return false;
    status_out = std::move(run_.status);
    run_.done = false;
    return true;
}

// --- render ---------------------------------------------------------------

void TransferDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const
{
    if (!active_) return;

    // While the worker runs, show a veiling progress modal instead of the dialog —
    // the host grid has already suppressed its own drawing (job_active()).
    if (stage_ == Stage::Running) {
        const char* verb = (mode_ == vault::TransferMode::Copy) ? "Copying…" : "Moving…";
        const int total = run_.job.total();
        const int done  = run_.job.done();
        const std::string count =
            total > 0 ? std::format("{} / {} files", done, total) : "Preparing…";
        draw_op_progress(r, font, W, H,
                         {.title = verb, .count_line = count, .done = done, .total = total});
        return;
    }

    using namespace gfx::theme;

    const float mw = W * 0.6f;
    const float mh = H * 0.6f;
    const float mx = (W - mw) / 2;
    const float my = (H - mh) / 2;
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    const float ix = mx + 20;
    const float iy = my + 20;
    const char* verb = (mode_ == vault::TransferMode::Copy) ? "Copy" : "Move";
    r.draw_text(font, ix, iy,
                source_ == Source::Gallery
                    ? std::format("{} gallery \"{}\"", verb,
                                  std::filesystem::path(src_gallery_).filename().string())
                    : std::format("{} {} image(s)", verb, filenames_.size()),
                TEXT);

    render_body(r, font, ix, iy, mw, mh, my);

    if (!error_.empty()) r.draw_text(font, ix, my + mh - 30, error_, DANGER);
}

void TransferDialog::render_body(gfx::Renderer& r, gfx::FontAtlas& font,
                                 float ix, float iy, float mw, float mh, float my) const
{
    using namespace gfx::theme;

    auto row_list = [&](const std::vector<std::string>& items, int sel, float top) {
        for (size_t i = 0; i < items.size(); ++i) {
            const float ry = top + static_cast<float>(i) * 34.0f;
            const bool on = (static_cast<int>(i) == sel);
            if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
            r.draw_text(font, ix + 8, ry + 4, fit_text(font, items[i], mw - 56),
                        on ? TEXT : TEXT_DIM);
        }
    };

    if (stage_ == Stage::Mode) {
        r.draw_text(font, ix, iy + 36, "Action:", TEXT_DIM);
        const std::vector<std::string> modes = {"Move", "Copy"};
        const int msel = (mode_ == vault::TransferMode::Copy) ? 1 : 0;
        row_list(modes, msel, iy + 72);
        r.draw_text(font, ix, iy + 150, "[Up/Down] choose  [Enter] next", TEXT_FAINT);
    } else if (stage_ == Stage::PickVault) {
        r.draw_text(font, ix, iy + 36, "Destination vault:", TEXT_DIM);
        std::vector<std::string> labels = {"This vault"};
        for (const auto& p : candidates_) labels.push_back(p.filename().string());
        row_list(labels, vault_sel_, iy + 72);
    } else if (stage_ == Stage::Unlock) {
        r.draw_text(font, ix, iy + 36,
                    fit_text(font,
                             "Unlock " + std::filesystem::path(dest_.path).filename().string(),
                             mw - 40),
                    TEXT_DIM);
        std::string masked(dest_.pw.length(), '*');
        draw_text_field(r, font, {ix, iy + 72, mw - 40, 40}, masked, true);
        r.draw_text(font, ix, iy + 122,
                    dest_.keyfile_path.empty() ? "[Tab] add keyfile  [Enter] unlock"
                                               : "keyfile set  •  [Enter] unlock",
                    TEXT_FAINT);
    } else {  // PickGallery
        r.draw_text(font, ix, iy + 36,
                    source_ == Source::Gallery ? "Destination parent gallery:"
                                               : "Destination gallery:",
                    TEXT_DIM);
        const float list_top = iy + 72;
        const float row_h    = 34.0f;
        const float list_bottom = naming_ ? my + mh - 100 : my + mh - 20;
        const int   visible_rows = std::max(1, static_cast<int>((list_bottom - list_top) / row_h));
        const auto  g = picker_.geom(visible_rows);
        const auto& shown = picker_.filtered();
        for (int i = g.first; i < g.first + g.visible && i < static_cast<int>(shown.size()); ++i) {
            const float ry = list_top + static_cast<float>(i - g.first) * row_h;
            const bool  on = (i == picker_.selected());
            if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
            r.draw_text(font, ix + 8, ry + 4, fit_text(font, shown[static_cast<size_t>(i)], mw - 56),
                        on ? TEXT : TEXT_DIM);
        }
        if (shown.empty())
            r.draw_text(font, ix, list_top, "No matches.", TEXT_FAINT);
        if (picker_.filter_open() || !picker_.filter().empty())
            r.draw_text(font, ix, iy + 54,
                        fit_text(font, "Filter: " + picker_.filter(), mw - 40), TEXT_FAINT);
        if (naming_) {
            r.draw_text(font, ix, my + mh - 92, "New gallery name:", TEXT);
            draw_text_field(r, font, {ix, my + mh - 60, mw - 40, 40}, name_buf_, true);
        }
    }
}

} // namespace ui
