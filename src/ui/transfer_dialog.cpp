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
    pw_.clear();
    keyfile_path_.clear();
    awaiting_keyfile_ = false;
    naming_ = false;
    name_buf_.clear();

    // Destination candidates: every known vault except the active one.
    candidates_.clear();
    for (auto& p : registry_.list())
        if (p.string() != src_path_) candidates_.push_back(p);
    if (candidates_.empty())
        error_ = "No other vaults. Create or open one from the manager first.";
}

void TransferDialog::close()
{
    if (dst_.is_unlocked()) dst_.lock();   // wipe the destination key
    pw_.clear();
    active_ = false;
}

// --- stage transitions ----------------------------------------------------

void TransferDialog::choose_vault()
{
    if (candidates_.empty()) return;
    if (vault_sel_ < 0 || vault_sel_ >= static_cast<int>(candidates_.size())) return;
    dst_path_ = candidates_[static_cast<size_t>(vault_sel_)].string();
    pw_.clear();
    keyfile_path_.clear();
    error_.clear();
    stage_ = Stage::Unlock;
}

void TransferDialog::try_unlock()
{
    using enum vault::VaultResult;

    std::vector<uint8_t> keyfile;
    if (!keyfile_path_.empty()) {
        auto kf = platform::read_file(keyfile_path_);
        if (!kf) { error_ = "Cannot read keyfile."; return; }
        keyfile = std::move(*kf);
    }

    vault::VaultResult r = vault::Vault::open(dst_path_, dst_);
    if (r == Ok) r = dst_.unlock(pw_.bytes(), keyfile);
    if (!keyfile.empty()) crypto_wipe(keyfile.data(), keyfile.size());

    if (r != Ok) {
        error_ = (r == AuthFailed) ? "Wrong password or keyfile."
               : (r == BadFormat)  ? "Not a valid vault file."
                                   : "Could not open the destination vault.";
        return;
    }
    pw_.clear();
    error_.clear();
    rebuild_targets();
    gallery_sel_ = 0;
    stage_ = Stage::PickGallery;
}

void TransferDialog::rebuild_targets()
{
    targets_ = vault::image_target_galleries(dst_);
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
        if (vault::move_image(src_, src_gallery_, fname, dst_, dst_gallery) == Ok) ++ok;
    }
    const std::string where = std::filesystem::path(dst_path_).stem().string();
    completed_status_ = std::format("Moved {} of {} to {}", ok,
                                    static_cast<int>(filenames_.size()), where);
    completed_ = true;
    close();   // wipes the destination key
}

// --- input ----------------------------------------------------------------

bool TransferDialog::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    // New-gallery name entry (overlays the PickGallery stage).
    if (naming_) {
        if (e.type == SDL_EVENT_TEXT_INPUT) { name_buf_ += e.text.text; return true; }
        if (e.type == SDL_EVENT_KEY_DOWN) {
            switch (e.key.key) {
                case SDLK_BACKSPACE: if (!name_buf_.empty()) name_buf_.pop_back(); break;
                case SDLK_ESCAPE:    naming_ = false; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (!name_buf_.empty() &&
                        dst_.create_gallery(name_buf_) == vault::VaultResult::Ok) {
                        rebuild_targets();
                        naming_ = false;
                        do_move(name_buf_);     // move straight into the new gallery
                    } else {
                        error_ = "Could not create that gallery.";
                        naming_ = false;
                    }
                    break;
                default: break;
            }
        }
        return true;
    }

    if (e.type == SDL_EVENT_TEXT_INPUT && stage_ == Stage::Unlock) {
        pw_.push_utf8(e.text.text);
        return true;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // modal swallows other events

    const SDL_Keycode k = e.key.key;
    if (k == SDLK_ESCAPE) { close(); return true; }

    switch (stage_) {
        case Stage::PickVault: {
            const int n = static_cast<int>(candidates_.size());
            if (k == SDLK_UP)   vault_sel_ = clamp_index(vault_sel_ - 1, n);
            if (k == SDLK_DOWN) vault_sel_ = clamp_index(vault_sel_ + 1, n);
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER) choose_vault();
            break;
        }
        case Stage::Unlock:
            if (k == SDLK_BACKSPACE) pw_.backspace();
            else if (k == SDLK_TAB) { dlg_.open_keyfile(win_.sdl_window()); awaiting_keyfile_ = true; }
            else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) try_unlock();
            break;
        case Stage::PickGallery: {
            const int n = static_cast<int>(targets_.size());
            if (k == SDLK_UP)   gallery_sel_ = clamp_index(gallery_sel_ - 1, n);
            if (k == SDLK_DOWN) gallery_sel_ = clamp_index(gallery_sel_ + 1, n);
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER) choose_gallery();
            break;
        }
    }
    return true;
}

void TransferDialog::update()
{
    if (!awaiting_keyfile_) return;
    if (auto res = dlg_.take_result()) {
        awaiting_keyfile_ = false;
        if (!res->empty()) keyfile_path_ = (*res)[0];
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

void TransferDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
{
    if (!active_) return;
    using namespace gfx::theme;

    const float mw = W * 0.6f, mh = H * 0.6f;
    const float mx = (W - mw) / 2, my = (H - mh) / 2;
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    const float ix = mx + 20, iy = my + 20;
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
        for (auto& p : candidates_) labels.push_back(p.filename().string());
        row_list(labels, vault_sel_, iy + 72);
    } else if (stage_ == Stage::Unlock) {
        r.draw_text(font, ix, iy + 36,
                    "Unlock " + std::filesystem::path(dst_path_).filename().string(),
                    TEXT_DIM);
        std::string masked(pw_.length(), '*');
        draw_text_field(r, font, {ix, iy + 72, mw - 40, 40}, masked, true);
        r.draw_text(font, ix, iy + 122,
                    keyfile_path_.empty() ? "[Tab] add keyfile  [Enter] unlock"
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
