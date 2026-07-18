#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace gfx { class Renderer; class FontAtlas; class Window; }
namespace vault { class Vault; }

namespace ui {

// Modal that renames the focused image, video, or gallery in place (Phase 44
// Part 2). Single text-entry stage, seeded with the current name. Not a
// TransferDialog-style multi-stage flow — no vault picking, no unlock.
class RenameDialog {
public:
    explicit RenameDialog(gfx::Window& win) : win_(win) {}

    // Activate on `old_name` (an image/video/gallery) inside `gallery_path`.
    void open(std::string gallery_path, std::string old_name);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] bool handle_event(vault::Vault& v, const SDL_Event& e);   // true if consumed
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const;

    // A rename committed successfully; drains the one-shot status message.
    [[nodiscard]] bool consume_completed(std::string& status_out);

private:
    gfx::Window& win_;

    bool        active_ = false;
    std::string gallery_path_;
    std::string old_name_;
    std::string buf_;
    std::string error_;

    bool        done_ = false;
    std::string status_;
};

} // namespace ui
