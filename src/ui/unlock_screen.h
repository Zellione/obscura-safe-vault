#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>

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

private:
    struct Layout {
        SDL_FRect keyfile_btn;
        SDL_FRect other_btn;
        SDL_FRect mode_btn;
        SDL_FRect submit_btn;
    };
    [[nodiscard]] Layout layout() const;
    void submit();

    enum class Pending { None, Vault, Keyfile };

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
    Pending               pending_ = Pending::None;
};

} // namespace ui
