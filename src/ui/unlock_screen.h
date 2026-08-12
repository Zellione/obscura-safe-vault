#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>

#include "ui/screen.h"
#include "ui/secure_text_input.h"
#include "ui/unlock_job.h"
#include "ui/widgets.h"

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

    // Keep frames ticking while the KDF worker runs, so update() polls the
    // outcome promptly and the "Deriving key…" notice animates redraws.
    [[nodiscard]] bool animating() const override { return job_.active(); }

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

    // UI references and configuration
    gfx::Window&          win_;
    gfx::FontAtlas&       font_;
    vault::Vault&         vault_;
    platform::FileDialog& dlg_;
    std::filesystem::path vault_path_;
    bool                  create_mode_;

    // Password input state
    struct PasswordInput {
        int             focus = 0;     // 0 = password, 1 = confirm
        SecureTextInput pw;
        SecureTextInput confirm;
        TextFieldChrome pw_chrome;
        TextFieldChrome confirm_chrome;
        bool            reveal = false;  // show a freshly generated passphrase
    };
    PasswordInput password_;

    // File and status state
    std::string     keyfile_path_;
    std::string     error_;
    Pending         pending_ = Pending::None;
    UnlockJob       job_;               // Argon2id runs off the main thread

    // Phase 45 Part 3: clipboard state (what we last copied and timer)
    struct ClipboardState {
        std::string last_set;
        double      clear_timer = -1.0;  // -1 = no pending auto-clear
    };
    ClipboardState clipboard_;

    // Mouse tracking for button hover/active states
    struct MouseState {
        float x    = -1.0f;
        float y    = -1.0f;
        bool  down = false;
    };
    MouseState mouse_;
};

} // namespace ui
