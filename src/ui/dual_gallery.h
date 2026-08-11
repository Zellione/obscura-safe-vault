#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

#include "ui/screen.h"
#include "ui/gallery_grid.h"
#include "ui/dual_transfer.h"
#include "ui/file_op_job.h"

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; }

namespace ui {
class ImportQueue;
struct DualSessionState;

// Copy an SDL event, shifting mouse coordinates into pane-local space
// (subtract pane origin). Non-mouse events pass through unchanged.
[[nodiscard]] SDL_Event translate_event_to_pane(const SDL_Event& e, const SDL_FRect& pane) noexcept;

// Side-by-side dual-pane gallery (Phase 77). Coordinates two GalleryGrid instances
// with synchronized keyboard/mouse event routing and snapshot/restore of pane state.
class DualGalleryScreen : public Screen {
public:
    // Same dependency set as GalleryGrid (the ctor builds two of them), plus
    // the dual session state to restore from / snapshot into.
    DualGalleryScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                      gfx::TextureCache& cache, GalleryGrid::GridDialogs dialogs,
                      GalleryGrid::GridVaultCtx vault_ctx, GallerySessionState& session,
                      ImportQueue& queue, DualSessionState& dual);

    void on_enter() override;
    void on_exit() override;                 // snapshots both panes into dual_
    void on_vault_changed() override;        // forward to both panes + walk-up
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;
    [[nodiscard]] bool animating() const override;         // OR of panes
    [[nodiscard]] bool blocks_idle_lock() const override;  // OR of panes + own job
    [[nodiscard]] std::vector<HelpGroup> help_groups() const override;

private:
    GalleryGrid& active();
    const GalleryGrid& active() const;
    GalleryGrid& inactive();
    void set_active(int pane);   // updates dialog-pump flags
    void snapshot();             // both panes -> dual_

    gfx::Window&              win_;
    gfx::FontAtlas&           font_;
    vault::Vault&             vault_;
    DualSessionState&         dual_;
    ImportQueue&              queue_;
    std::unique_ptr<GalleryGrid> left_;
    std::unique_ptr<GalleryGrid> right_;
    int                       active_ = 0;
    std::string               status_;
    DualTransferPrompt        prompt_;
    FileOpJob                 job_;
};

} // namespace ui
