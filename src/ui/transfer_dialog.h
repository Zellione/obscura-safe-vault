#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

#include "ui/file_op_job.h"   // background transfer executor (Phase 25)
#include "ui/gallery_picker.h"
#include "ui/text_input_model.h"
#include "ui/widgets.h"
#include "ui/vault_unlock_picker.h"
#include "vault/transfer.h"   // vault::TransferMode, vault::TransferFailure
#include "vault/vault.h"      // owns the source vault

namespace gfx { class Renderer; class FontAtlas; class Window; }
namespace platform { class VaultRegistry; class FileDialog; }
namespace ui { class SecondVaultSession; }

namespace ui {

// Result of a completed transfer, including per-item failures for dialog display.
struct TransferCompletion {
    std::string status;
    int failed_total = 0;
    std::vector<vault::TransferFailure> failures;
};

// Modal that moves OR copies the grid's selected images / a gallery subtree to
// another vault — or within the active vault (Phase 14 PR2/3/4). Stages: choose
// Move/Copy -> pick a destination vault ("This vault" or a registry entry) ->
// unlock it (password + optional keyfile; skipped for "This vault") -> pick a
// destination gallery (or create one) -> run vault::transfer_image /
// transfer_gallery per item -> re-lock the destination.
//
// A cross-vault destination is unlocked only for the transfer and its key is wiped
// on every exit (close()/completion); ~Vault is the backstop. The active vault
// (src_) is owned by App and is never locked here.
class TransferDialog {
public:
    // `src_path` is the active vault's file path — used to exclude it from the
    // destination-vault candidate list (the registry stores paths, not handles).
    TransferDialog(vault::Vault& src, std::string src_path,
                   platform::VaultRegistry& registry,
                   platform::FileDialog& dlg, gfx::Window& win, SecondVaultSession* second);

    // Activate to move `filenames` (image names within `src_gallery`).
    void open(std::string src_gallery, std::vector<std::string> filenames);

    // Activate to move a whole GALLERY subtree (`src_gallery`, a gallery path).
    void open_gallery(std::string src_gallery);

    // Activate to move a LIST of whole GALLERY subtrees at once (multi-select).
    void open_galleries(std::vector<std::string> src_paths);

    // Activate for a MIXED multi-selection (Phase 68): media names within
    // `src_gallery` land inside the picked target; each gallery subtree in
    // `gallery_paths` lands under it. One Mode/Dest/Target pick for both.
    void open_mixed(std::string src_gallery, std::vector<std::string> media_names,
                    std::vector<std::string> gallery_paths);

    // Activate for a COLLECTION-screen selection (Phase 68): media grouped by
    // their source parent (favorites/tags/search hits span galleries) plus
    // whole gallery subtrees, in one Mode/Dest/Target pick.
    void open_collection(std::vector<ParentGroup> media_groups,
                         std::vector<std::string> gallery_paths);

    void close();                                   // wipes dest_.vault key, deactivates
    [[nodiscard]] bool active() const noexcept { return active_; }

    // True while the background move/copy worker owns the vault(s). The host grid
    // uses this to draw only the progress modal (no thumbnail decrypt) and to hold
    // off the idle auto-lock. Distinct from active(), which is also true during the
    // dialog's pick-vault/unlock/pick-gallery stages (Phase 25).
    [[nodiscard]] bool job_active() const noexcept { return run_.job.active(); }

    [[nodiscard]] bool handle_event(const SDL_Event& e);   // true if consumed
    void update();                                          // poll the keyfile dialog + the transfer job
    [[nodiscard]] bool consume_completed(TransferCompletion& out);
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);

private:
    // Running: the background move/copy worker owns the vault(s); the dialog stays
    // active (keeping the unlocked destination alive) and shows a progress modal
    // until the job completes, then closes (Phase 25).
    enum class Stage { Mode, PickingDest, PickGallery, Conflict, Running };

    void choose_gallery();    // PickGallery Enter: move into the selected target (or "New")
    void do_move(std::string_view dst_gallery);   // pre-scan for conflicts; route to Conflict or launch_transfer
    void launch_transfer(std::string_view dst_target, vault::CollisionPolicy policy);   // run the transfer + re-lock
    void rebuild_targets();   // image_target_galleries(dest_vault()) + the "New gallery…" row
    void render_body(gfx::Renderer& r, gfx::FontAtlas& font,
                     float ix, float iy, float mw, float mh, float my);  // per-stage body
    void render_mode_body(gfx::Renderer& r, gfx::FontAtlas& font,
                          float ix, float iy, float mw) const;
    void render_conflict_body(gfx::Renderer& r, gfx::FontAtlas& font,
                              float ix, float iy, float mw) const;
    void render_pick_gallery_body(gfx::Renderer& r, gfx::FontAtlas& font,
                                  float ix, float iy, float mw, float mh, float my);

    bool handle_mode_key(SDL_Keycode k);         // Mode stage: toggle Move/Copy
    bool handle_conflict_key(SDL_Keycode k);     // Conflict stage: choose policy
    vault::Vault& dest_vault() noexcept;         // src_ when same-vault, else picker_dest_'s vault
    bool handle_gallery_key(SDL_Keycode k);
    bool handle_naming_event(const SDL_Event& e);   // new-gallery name overlay
    [[nodiscard]] std::vector<std::string> galleries_for_conflict_scan() const;   // galleries to check for collisions
    // handle_event() sub-handlers, extracted to keep its cognitive complexity
    // (S3776) and nesting depth (S134) bounded.
    bool handle_picking_dest_event(const SDL_Event& e);
    bool handle_gallery_filter_event(const SDL_Event& e);
    bool handle_stage_key(const SDL_KeyboardEvent& key);

    vault::Vault&            src_;
    std::string              src_path_;            // active vault's path (excluded as a dest)
    gfx::Window&             win_;

    bool        active_ = false;
    Stage       stage_  = Stage::Mode;
    vault::TransferMode mode_ = vault::TransferMode::Move;
    std::string src_gallery_;
    std::vector<std::string> filenames_;
    std::vector<std::string> src_galleries_;   // Source::Galleries payload
    std::vector<ParentGroup> media_groups_;    // Source::Collection payload (Phase 68)

    enum class Source { Images, Gallery, Galleries, Collection };   // Galleries: Phase 44 Part 3; Collection: Phase 68
    Source      source_ = Source::Images;

    VaultUnlockPicker picker_dest_;                     // PickingDest: destination vault selection/unlock
    GalleryPickerModel picker_;                         // PickGallery: filterable/scrollable list
    bool        naming_ = false;
    TextInputModel  name_buf_;
    TextFieldChrome name_buf_chrome_;   // caret/scroll view state, advanced by render()

    std::string error_;

    // Conflict stage state (Phase 71): dst target + collision count + selected policy option.
    std::string pending_target_;
    int         conflict_count_ = 0;
    int         conflict_sel_   = 0;

    // Background transfer run — the worker-thread job plus its finished outcome,
    // bundled to keep the field count ≤20 (S1820). `done`/`completion` are set when the
    // job completes and drained by consume_completed.
    struct Run {
        FileOpJob         job;
        bool              done = false;
        TransferCompletion completion;
    };
    Run         run_;
};

} // namespace ui
