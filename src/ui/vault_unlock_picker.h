#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

#include "ui/secure_text_field.h"
#include "vault/vault.h"

namespace gfx { class Renderer; class FontAtlas; class Window; }
namespace platform { class VaultRegistry; class FileDialog; }

namespace ui {

// The "pick a destination vault, then unlock it" flow shared by TransferDialog
// and CombineDialog (Phase 44 Part 4 extraction — previously duplicated only
// inside TransferDialog). Two internal stages: PickVault ("This vault" or a
// registry entry) -> Unlock (password + optional keyfile; skipped entirely
// when "This vault" is chosen). Esc at either stage cancels the whole flow
// (matches the pre-extraction behavior exactly — no partial back-navigation).
//
// Owns a transient destination vault::Vault, unlocked only for the caller's
// use and wiped/locked on close()/destruction. Never touches the "self" case's
// vault — that's the caller's own active vault, which this class doesn't (and
// shouldn't) know about; callers combine is_self()/unlocked_vault() with their
// own `Vault& src_` to get "the vault to write into", exactly as
// TransferDialog::dest_vault() already does.
class VaultUnlockPicker {
public:
    VaultUnlockPicker(platform::VaultRegistry& registry, platform::FileDialog& dlg, gfx::Window& win);

    // Rebuilds candidates (every registered vault path except `src_path`) and
    // enters PickVault. "This vault" is always row 0.
    void open(std::string src_path);
    void close();   // locks/wipes the transient dest vault; deactivates

    [[nodiscard]] bool active() const noexcept { return active_; }   // still in PickVault/Unlock
    [[nodiscard]] bool chosen() const noexcept { return chosen_; }   // a usable destination is ready
    [[nodiscard]] bool is_self() const noexcept { return dest_.is_self; }
    [[nodiscard]] vault::Vault& unlocked_vault() noexcept { return dest_.vault; }
    [[nodiscard]] std::string dest_label() const;
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    [[nodiscard]] bool handle_event(const SDL_Event& e);   // true if consumed
    void update();                                          // poll the keyfile dialog
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float ix, float iy, float mw) const;

private:
    enum class Stage { PickVault, Unlock };

    void choose_vault();
    void try_unlock();
    bool handle_pick_vault_key(SDL_Keycode k);
    bool handle_unlock_key(SDL_Keycode k);

    platform::VaultRegistry& registry_;
    platform::FileDialog&    dlg_;
    gfx::Window&              win_;

    bool  active_ = false;
    bool  chosen_ = false;
    Stage stage_  = Stage::PickVault;

    std::string src_path_;
    std::vector<std::filesystem::path> candidates_;
    int vault_sel_ = 0;

    struct Dest {
        vault::Vault    vault;
        std::string     path;
        SecureTextField pw;
        std::string     keyfile_path;
        bool            awaiting_keyfile = false;
        bool            is_self = false;
    };
    Dest dest_;

    std::string error_;
};

} // namespace ui
