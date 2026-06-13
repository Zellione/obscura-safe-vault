#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <string>

#include "ui/consent_dialog.h"

namespace gfx { class Renderer; class FontAtlas; class Window; }
namespace platform { class FolderDialog; }

namespace ui {

// Shared "are you sure? -> pick a destination folder" plumbing for export
// (Phase 10), used by both the gallery grid and the image viewer. Owns the
// consent modal and the transient status line; the screen supplies the actual
// write step once a destination has been chosen. SDL plumbing only — the
// security-relevant write/wipe lives in the (unit-tested) export module.
class ExportUi {
public:
    ExportUi(platform::FolderDialog& folder, gfx::Window& win) noexcept
        : folder_(folder), win_(win) {}

    void begin(std::string detail);          // open the consent modal
    bool consume_key(SDL_Keycode key);       // route while the modal is up; opens the
                                             // picker on confirm; true if it owned the key
    [[nodiscard]] bool modal_active() const noexcept;

    // The chosen destination, returned exactly once after the picker closes
    // (nullopt while pending or if cancelled). Poll once per frame.
    [[nodiscard]] std::optional<std::filesystem::path> take_destination();

    void set_status(std::string s) { status_ = std::move(s); }
    [[nodiscard]] const std::string& status() const noexcept { return status_; }

    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const;

private:
    platform::FolderDialog& folder_;
    gfx::Window&            win_;
    ConsentDialog           consent_;
    std::string             status_;
};

} // namespace ui
