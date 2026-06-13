#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace gfx { class Renderer; class FontAtlas; }

namespace ui {

// Modal confirmation for a dangerous, irreversible action (Phase 10 export).
//
// It names the danger explicitly and defaults focus to Cancel — confirming is a
// deliberate act. Reused by the gallery grid (multi-image export) and the image
// viewer (single-image export). Drawing-only plumbing over gfx primitives; the
// security-relevant "decline writes nothing" behaviour is enforced by the export
// module, which only writes on ExportConsent::Confirm.
class ConsentDialog {
public:
    enum class Result { Pending, Confirmed, Cancelled };

    // Show the modal with a one-line `detail` describing what will be exported
    // (e.g. "Export 3 images?"). The fixed danger warning is rendered below it.
    void open(std::string detail);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }

    // Process a key while active. Enter / Y confirm; Esc / N cancel. Any other
    // key is ignored (Pending). Closes the dialog on a decisive key.
    Result handle_key(SDL_Keycode key);

    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const;

private:
    bool        active_ = false;
    std::string detail_;
};

} // namespace ui
