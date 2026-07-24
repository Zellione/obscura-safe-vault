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
    [[nodiscard]] bool blocks_idle_lock() const override { return false; }  // App-level import_busy suppression covers the queue; this screen itself never owns vault work

private:
    void move_selection(int delta);
    void reorder_selected(int delta);          // Ctrl+Up/Down on the selected queued row
    void handle_key(const SDL_KeyboardEvent& key);

    gfx::Window&   win_;
    gfx::FontAtlas& font_;
    ImportQueue&   queue_;
    Nav            back_;
    std::vector<ImportTaskInfo> rows_;    // last snapshot
    int            sel_ = 0;
    float          scroll_ = 0.0f;         // vertical scroll offset (pixels scrolled down)
    bool           last_lane_failed_ = false;  // track lane failure for dirty marking
};

} // namespace ui
