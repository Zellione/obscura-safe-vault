#pragma once

#include <SDL3/SDL.h>

#include <string>

#include "ui/file_op_job.h"
#include "ui/gallery_picker.h"
#include "ui/vault_unlock_picker.h"
#include "vault/vault.h"

namespace gfx { class Renderer; class FontAtlas; class Window; }
namespace platform { class VaultRegistry; class FileDialog; }

namespace ui {

// Result of a finished combine, drained by the host grid to decide where to
// navigate (Phase 44 Part 4).
struct CombineOutcome {
    std::string status;
    bool        source_gone = false;   // the merge fully emptied (and removed) the source
    bool        same_vault  = false;   // the destination was the active vault
    std::string dest_path;             // valid when same_vault && source_gone
};

// Modal that merges the CURRENTLY BROWSED gallery into another one, same- or
// cross-vault (Phase 44 Part 4). Stages: PickingDest (delegated to
// VaultUnlockPicker) -> PickTarget (a GalleryPickerModel over
// vault::combine_target_galleries) -> Running (progress modal, mirrors
// TransferDialog).
class CombineDialog {
public:
    CombineDialog(vault::Vault& src, std::string src_path, platform::VaultRegistry& registry,
                 platform::FileDialog& dlg, gfx::Window& win);

    void open(std::string src_gallery);   // src_gallery: the currently browsed gallery (nav_.path())
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool job_active() const noexcept { return run_.job.active(); }

    [[nodiscard]] bool handle_event(const SDL_Event& e);
    void update();
    [[nodiscard]] bool consume_completed(CombineOutcome& out);
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const;

private:
    enum class Stage { PickingDest, PickTarget, Running };

    void choose_target();
    void do_combine(const std::string& dst_target);

    vault::Vault&            src_;
    std::string              src_path_;
    platform::VaultRegistry& registry_;
    platform::FileDialog&    dlg_;
    gfx::Window&              win_;

    bool        active_ = false;
    Stage       stage_  = Stage::PickingDest;
    std::string src_gallery_;

    VaultUnlockPicker  picker_dest_;
    GalleryPickerModel picker_target_;

    std::string error_;

    struct Run {
        FileOpJob   job;
        bool        done = false;
        CombineOutcome outcome;
    };
    Run run_;
};

} // namespace ui
