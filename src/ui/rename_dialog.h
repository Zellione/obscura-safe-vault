#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

#include "ui/text_input_model.h"
#include "ui/widgets.h"

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
    void open_tag(std::string tag);
    [[nodiscard]] std::string_view new_name() const { return buf_.str(); }
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] bool handle_event(vault::Vault& v, const SDL_Event& e);   // true if consumed
    // Not const: drawing an editable field advances its caret/scroll view state.
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);

    // A rename committed successfully; drains the one-shot status message.
    [[nodiscard]] bool consume_completed(std::string& status_out);

private:
    gfx::Window& win_;

    bool        active_ = false;
    bool        tag_mode_ = false;
    bool        skip_opening_text_ = false;
    std::string gallery_path_;
    std::string old_name_;
    TextInputModel  buf_;
    TextFieldChrome buf_chrome_;   // caret/scroll view state, advanced by render()
    std::string error_;

    bool        done_ = false;
    std::string status_;
};

} // namespace ui
