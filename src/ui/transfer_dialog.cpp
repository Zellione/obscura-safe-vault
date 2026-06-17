#include "ui/transfer_dialog.h"

#include <monocypher.h>

#include <format>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "platform/vault_registry.h"
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
    stage_        = Stage::PickVault;
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
    if (candidates_.empty())
        error_ = "No other vaults. Create or open one from the manager first.";
}

void TransferDialog::close()
{
    if (dest_.vault.is_unlocked()) dest_.vault.lock();   // wipe the destination key
    dest_.pw.clear();
    active_ = false;
}

// --- stage transitions ----------------------------------------------------

void TransferDialog::choose_vault()
{
    if (candidates_.empty()) return;
    if (vault_sel_ < 0 || vault_sel_ >= static_cast<int>(candidates_.size())) return;
    dest_.path = candidates_[static_cast<size_t>(vault_sel_)].string();
    dest_.pw.clear();
    dest_.keyfile_path.clear();
    error_.clear();
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
    gallery_sel_ = 0;
    stage_ = Stage::PickGallery;
}

void TransferDialog::rebuild_targets()
{
    targets_ = vault::image_target_galleries(dest_.vault);
    targets_.emplace_back(kNewGalleryRow);   // last row creates a new gallery
}

void TransferDialog::choose_gallery()
{
    if (gallery_sel_ < 0 || gallery_sel_ >= static_cast<int>(targets_.size())) return;
    const std::string& sel = targets_[static_cast<size_t>(gallery_sel_)];
    if (sel == kNewGalleryRow) { naming_ = true; name_buf_.clear(); return; }
    do_move(sel);
}

void TransferDialog::do_move(std::string_view dst_gallery)
{
    using enum vault::VaultResult;
    int ok = 0;
    for (const auto& fname : filenames_) {
        if (vault::move_image(src_, src_gallery_, fname, dest_.vault, dst_gallery) == Ok) ++ok;
    }
    const std::string where = std::filesystem::path(dest_.path).stem().string();
    completed_status_ = std::format("Moved {} of {} to {}", ok,
                                    static_cast<int>(filenames_.size()), where);
    completed_ = true;
    close();   // wipes the destination key
}

// --- per-stage key handlers ------------------------------------------------

bool TransferDialog::handle_pick_vault_key(SDL_Keycode k)
{
    const auto n = static_cast<int>(candidates_.size());
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
    const auto n = static_cast<int>(targets_.size());
    if (k == SDLK_UP)   gallery_sel_ = clamp_index(gallery_sel_ - 1, n);
    if (k == SDLK_DOWN) gallery_sel_ = clamp_index(gallery_sel_ + 1, n);
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
                dest_.vault.create_gallery(name_buf_) == vault::VaultResult::Ok) {
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

    // New-gallery name entry (overlays the PickGallery stage).
    if (naming_) return handle_naming_event(e);

    if (e.type == SDL_EVENT_TEXT_INPUT && stage_ == Stage::Unlock) {
        dest_.pw.push_utf8(e.text.text);
        return true;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // modal swallows other events

    const SDL_Keycode k = e.key.key;
    if (k == SDLK_ESCAPE) { close(); return true; }

    using enum Stage;
    switch (stage_) {
        case PickVault:   return handle_pick_vault_key(k);
        case Unlock:      return handle_unlock_key(k);
        case PickGallery: return handle_gallery_key(k);
    }
    return true;
}

void TransferDialog::update()
{
    if (!dest_.awaiting_keyfile) return;
    if (auto res = dlg_.take_result()) {
        dest_.awaiting_keyfile = false;
        if (!res->empty()) dest_.keyfile_path = (*res)[0];
    }
}

bool TransferDialog::consume_completed(std::string& status_out)
{
    if (!completed_) return false;
    status_out = std::move(completed_status_);
    completed_ = false;
    return true;
}

// --- render ---------------------------------------------------------------

void TransferDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const
{
    if (!active_) return;
    using namespace gfx::theme;

    const float mw = W * 0.6f;
    const float mh = H * 0.6f;
    const float mx = (W - mw) / 2;
    const float my = (H - mh) / 2;
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    const float ix = mx + 20;
    const float iy = my + 20;
    r.draw_text(font, ix, iy,
                std::format("Move {} image(s)", filenames_.size()), TEXT);

    auto row_list = [&](const std::vector<std::string>& items, int sel, float top) {
        for (size_t i = 0; i < items.size(); ++i) {
            const float ry = top + static_cast<float>(i) * 34.0f;
            const bool on = (static_cast<int>(i) == sel);
            if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
            r.draw_text(font, ix + 8, ry + 4, items[i], on ? TEXT : TEXT_DIM);
        }
    };

    if (stage_ == Stage::PickVault) {
        r.draw_text(font, ix, iy + 36, "Destination vault:", TEXT_DIM);
        std::vector<std::string> labels;
        for (const auto& p : candidates_) labels.push_back(p.filename().string());
        row_list(labels, vault_sel_, iy + 72);
    } else if (stage_ == Stage::Unlock) {
        r.draw_text(font, ix, iy + 36,
                    "Unlock " + std::filesystem::path(dest_.path).filename().string(),
                    TEXT_DIM);
        std::string masked(dest_.pw.length(), '*');
        draw_text_field(r, font, {ix, iy + 72, mw - 40, 40}, masked, true);
        r.draw_text(font, ix, iy + 122,
                    dest_.keyfile_path.empty() ? "[Tab] add keyfile  [Enter] unlock"
                                               : "keyfile set  •  [Enter] unlock",
                    TEXT_FAINT);
    } else {  // PickGallery
        r.draw_text(font, ix, iy + 36, "Destination gallery:", TEXT_DIM);
        row_list(targets_, gallery_sel_, iy + 72);
        if (naming_) {
            r.draw_text(font, ix, my + mh - 92, "New gallery name:", TEXT);
            draw_text_field(r, font, {ix, my + mh - 60, mw - 40, 40}, name_buf_, true);
        }
    }

    if (!error_.empty()) r.draw_text(font, ix, my + mh - 30, error_, DANGER);
}

} // namespace ui
