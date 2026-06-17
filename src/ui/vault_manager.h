#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

#include "ui/screen.h"

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace platform { class VaultRegistry; class FileDialog; }

namespace ui {

// The app's home screen: a vertical list of known vaults (from the registry) with
// the currently-unlocked one badged. Actions: open/select (Enter), create a new
// vault (N → save dialog), open another file (O → open dialog), remove from the
// list (R/Del), lock the active vault (L), quit (Esc/Q). Selecting a vault emits a
// transition; App owns the actual Vault instances.
class VaultManager : public Screen {
public:
    VaultManager(gfx::Window& win, gfx::FontAtlas& font,
                 platform::VaultRegistry& registry, platform::FileDialog& dlg,
                 std::string active_path);

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;

private:
    struct Layout {
        SDL_FRect new_btn;
        SDL_FRect open_btn;
        float     list_top = 0.0f;
        float     row_h    = 0.0f;
    };
    [[nodiscard]] Layout layout() const;

    void reload();                 // re-read the registry into entries_
    void move(int delta);          // move the selection, clamped
    void open_selected();          // Enter/double-action on the selected row
    void remove_selected();
    void handle_key(const SDL_KeyboardEvent& key);
    void handle_click(const SDL_MouseButtonEvent& b);
    [[nodiscard]] int hit_test(float my) const;  // row under cursor, or -1

    gfx::Window&             win_;
    gfx::FontAtlas&          font_;
    platform::VaultRegistry& registry_;
    platform::FileDialog&    dlg_;
    std::string              active_path_;
    std::vector<std::filesystem::path> entries_;
    int                      selected_ = 0;
    bool                     awaiting_dialog_ = false;   // a New/Open file dialog is open

    float mouse_x_    = -1.0f;
    float mouse_y_    = -1.0f;
    bool  mouse_down_ = false;
};

} // namespace ui
