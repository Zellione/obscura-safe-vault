#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

#include "ui/secure_text_field.h"
#include "vault/vault.h"   // owns a transient vault::Vault dst_

namespace gfx { class Renderer; class FontAtlas; class Window; }
namespace platform { class VaultRegistry; class FileDialog; }

namespace ui {

// Modal that moves the grid's selected images into another vault (Phase 14 PR2).
// Stages: pick a destination vault (from the registry, source excluded) -> unlock
// it (password + optional keyfile) -> pick a destination leaf-gallery (or create
// one) -> move each image via vault::move_image -> re-lock the destination.
//
// The destination vault is unlocked only for the transfer and its key is wiped on
// every exit (close()/completion); ~Vault is the backstop.
class TransferDialog {
public:
    // `src_path` is the active vault's file path — used to exclude it from the
    // destination-vault candidate list (the registry stores paths, not handles).
    TransferDialog(vault::Vault& src, std::string src_path,
                   platform::VaultRegistry& registry,
                   platform::FileDialog& dlg, gfx::Window& win);

    // Activate to move `filenames` (image names within `src_gallery`).
    void open(std::string src_gallery, std::vector<std::string> filenames);
    void close();                                   // wipes dest_.vault key, deactivates
    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] bool handle_event(const SDL_Event& e);   // true if consumed
    void update();                                          // poll the keyfile dialog
    [[nodiscard]] bool consume_completed(std::string& status_out);
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const;

private:
    enum class Stage { PickVault, Unlock, PickGallery };

    void choose_vault();      // PickVault Enter: open dest_.vault for the selected path
    void try_unlock();        // Unlock Enter: open()+unlock() dest_.vault
    void choose_gallery();    // PickGallery Enter: move into the selected target (or "New")
    void do_move(std::string_view dst_gallery);   // run the transfer + re-lock
    void rebuild_targets();   // image_target_galleries(dest_.vault) + the "New gallery…" row

    bool handle_pick_vault_key(SDL_Keycode k);   // per-stage key handler
    bool handle_unlock_key(SDL_Keycode k);
    bool handle_gallery_key(SDL_Keycode k);
    bool handle_naming_event(const SDL_Event& e);   // new-gallery name overlay

    vault::Vault&            src_;
    std::string              src_path_;            // active vault's path (excluded as a dest)
    platform::VaultRegistry& registry_;
    platform::FileDialog&    dlg_;
    gfx::Window&             win_;

    bool        active_ = false;
    Stage       stage_  = Stage::PickVault;
    std::string src_gallery_;
    std::vector<std::string> filenames_;

    std::vector<std::filesystem::path> candidates_;   // PickVault: registry minus src
    int         vault_sel_ = 0;

    // Destination unlock state — bundled to fix S1820 (>20 data members).
    struct Dest {
        vault::Vault     vault;           // transient destination vault
        std::string      path;
        SecureTextField  pw;
        std::string      keyfile_path;
        bool             awaiting_keyfile = false;
    };
    Dest dest_;

    std::vector<std::string> targets_;                 // PickGallery: leaf paths + "<new>"
    int         gallery_sel_ = 0;
    bool        naming_ = false;
    std::string name_buf_;

    std::string error_;
    std::string completed_status_;     // set on a finished move; drained by consume_completed
    bool        completed_ = false;
};

} // namespace ui
