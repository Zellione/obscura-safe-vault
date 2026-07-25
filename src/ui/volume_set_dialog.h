#pragma once

// Phase 53: confirm importing a detected multi-volume archive set.
//
// Unlike ConsentDialog, this one can be in a state where confirming is simply
// not offered: a set with a missing volume yields a CORRUPT gallery rather than
// a partial one, so a gap is a refusal, not a warning to click past. The dialog
// still opens (the user needs to see WHICH volume is missing) but Enter does
// nothing.

#include "ui/volume_import.h"

#include <SDL3/SDL.h>

namespace gfx { class Renderer; class FontAtlas; }

namespace ui {

class VolumeSetDialog {
public:
    enum class Result { Pending, Confirmed, Cancelled };

    void open(VolumeSetSummary summary);
    void close();

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const VolumeSetSummary& summary() const noexcept { return summary_; }

    // Enter / Y confirm — but ONLY when the summary permits import. Esc / N
    // cancel. Anything else is ignored. A decisive key closes the dialog;
    // Enter on an unimportable set is not decisive, so the dialog stays up
    // with its warning visible.
    Result handle_key(SDL_Keycode key);

    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const;

private:
    bool             active_ = false;
    VolumeSetSummary summary_;
};

} // namespace ui
