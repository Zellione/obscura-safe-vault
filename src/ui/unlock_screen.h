#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <string>

#include "ui/clipboard_secret.h"
#include "ui/screen.h"
#include "ui/secure_text_field.h"

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }
namespace platform { class FileDialog; }

namespace ui {

class UnlockScreen : public Screen {
public:
    UnlockScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                 platform::FileDialog& dlg, std::filesystem::path vault_path);

    void on_enter() override;
    void on_exit() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;
    [[nodiscard]] std::vector<ui::HelpGroup> help_groups() const override;

private:
    struct Layout {
        SDL_FRect keyfile_btn;
        SDL_FRect other_btn;
        SDL_FRect mode_btn;
        SDL_FRect submit_btn;
        SDL_FRect generate_btn;     // create mode only
        SDL_FRect new_keyfile_btn;  // create mode only
        SDL_FRect copy_btn;         // Phase 45 Part 3: copy password to clipboard
    };
    [[nodiscard]] Layout layout() const;
    void handle_click(const SDL_MouseButtonEvent& b);
    void submit();
    void apply_dialog_result(const std::string& path);
    void copy_password_to_clipboard();   // Phase 45 Part 3

    enum class Pending { None, Vault, Keyfile, NewKeyfile };

    gfx::Window&          win_;
    gfx::FontAtlas&       font_;
    vault::Vault&         vault_;
    platform::FileDialog& dlg_;
    std::filesystem::path vault_path_;
    bool                  create_mode_;
    int                   focus_ = 0;   // 0 = password, 1 = confirm
    SecureTextField       pw_;
    SecureTextField       confirm_;
    std::string           keyfile_path_;
    std::string           error_;
    Pending               pending_   = Pending::None;
    bool                  reveal_pw_ = false;  // show a freshly generated passphrase

    // Phase 45 Part 3: what we last copied (for the auto-clear equality
    // check) and how long ago (-1 = no pending auto-clear).
    std::string           clipboard_last_set_;
    double                clipboard_clear_timer_ = -1.0;

    // Mouse tracking for button hover/active states.
    float                 mouse_x_    = -1.0f;
    float                 mouse_y_    = -1.0f;
    bool                  mouse_down_ = false;
};

} // namespace ui
