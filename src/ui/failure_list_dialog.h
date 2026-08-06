#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "vault/transfer.h"   // vault::TransferFailure

namespace gfx {
class Renderer;
class FontAtlas;
}

namespace ui {

// Maps a TransferFailure to a human-readable one-line summary: "path - reason".
// Reason text from the Phase 67 spec table; ASCII only (font atlas bakes 32–126).
[[nodiscard]] std::string transfer_failure_line(const vault::TransferFailure& f);

// Just the reason part of transfer_failure_line, without the path prefix.
// Used to build the structural-failure message in file_op_job.cpp.
[[nodiscard]] std::string transfer_failure_reason(vault::VaultResult code,
                                                  vault::TransferFailure::Stage stage);

// Modal dialog that shows a scrollable list of transfer failures.
// One failure per line, with elision for paths that don't fit.
class FailureListDialog {
public:
    // Populate the dialog with a list of failures. If failed_total > failures.size(),
    // append "...and N more" to indicate truncation.
    void open(const std::vector<vault::TransferFailure>& failures, int failed_total);

    void close();

    [[nodiscard]] bool active() const noexcept { return active_; }

    // Handle keyboard input: Esc/Enter closes; Up/Down/PageUp/PageDown scroll.
    // Returns true if the event was consumed.
    [[nodiscard]] bool handle_event(const SDL_Event& e);

    // Render the centered modal with title, scrollable list, and footer hint.
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const;

private:
    bool                      active_       = false;
    std::vector<std::string>  lines_;       // one line per failure + "...and N more" if truncated
    int                       scroll_       = 0;  // scroll offset in lines
    int                       failed_total_ = 0;  // total number of failures (may be > lines_.size())
    mutable int               visible_lines_ = 0;  // last rendered visible line count (for scroll clamping)
};

} // namespace ui
