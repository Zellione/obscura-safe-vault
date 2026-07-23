#pragma once

#include <vector>

#include "ui/import_model.h"
#include "ui/import_queue.h"
#include "ui/screen.h"

namespace gfx { class Window; class FontAtlas; class Renderer; }

namespace ui {

// Import queue status (Phase 50): running item + progress bar, queued items
// (Ctrl+Up/Down reorder, Del cancel), finished items with outcomes (C clears).
// Esc returns via the stored back Nav (same pattern as the favorites screens).
class ImportStatusScreen final : public Screen {
public:
    ImportStatusScreen(gfx::Window& win, gfx::FontAtlas& font,
                       ImportQueue& queue, Nav back);
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;          // poll snapshot; mark_dirty on change
    void render(gfx::Renderer& r) override;
    [[nodiscard]] std::vector<HelpGroup> help_groups() const override;

private:
    gfx::Window&   win_;
    gfx::FontAtlas& font_;
    ImportQueue&   queue_;
    Nav            back_;
    std::vector<ImportTaskInfo> rows_;    // last snapshot
    int            sel_ = 0;
    float          scroll_ = 0.0f;
};

} // namespace ui
