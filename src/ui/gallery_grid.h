#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "image/decode_worker.h"
#include "ui/consent_dialog.h"
#include "ui/nav_model.h"
#include "ui/quick_switch.h"
#include "ui/screen.h"
#include "ui/search_overlay.h"
#include "ui/selection_model.h"
#include "ui/tag_editor.h"
#include "ui/transfer_dialog.h"
#include "ui/zip_import_job.h"
#include "ui/zip_plan.h"

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }
namespace platform { class FileDialog; class FolderDialog; class VaultRegistry; }

namespace ui {

// Where to (re)open the grid: a gallery path (empty = root) and the selected
// tile index. Used to restore position when returning from the image viewer.
struct GridLocation {
    std::string path;
    int         selected = 0;
};

class GalleryGrid : public Screen {
public:
    // Dialog pair for import/export file/folder selection.
    struct GridDialogs {
        platform::FileDialog&   file;
        platform::FolderDialog& folder;
    };

    // Vault context: registry and active vault path (used to construct TransferDialog).
    struct GridVaultCtx {
        platform::VaultRegistry& registry;
        std::string              active_vault_path;
    };

    GalleryGrid(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                gfx::TextureCache& cache, GridDialogs dialogs,
                GridVaultCtx vault_ctx, GridLocation at = {});

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;
    // Keep repainting the progress bar and hold off the idle auto-lock while a
    // background CBZ import runs (the worker thread owns the vault meanwhile).
    [[nodiscard]] bool animating() const override { return import_job_.active(); }
    [[nodiscard]] bool blocks_idle_lock() const override { return import_job_.active(); }

private:
    enum class GalleryView { Grid, List };

    void handle_key_down(const SDL_KeyboardEvent& key);  // browse-mode keys
    void handle_naming_key(const SDL_Event& e);          // new-gallery text entry
    void toggle_or_open();                               // Space: select image / open
    void refresh();
    void open_selected();
    void go_up();
    void toggle_select();          // toggle the current item in the export selection
    void start_export();           // open the consent modal for the current selection
    void start_transfer();         // open the move-to-another-vault dialog
    void do_export(const std::filesystem::path& dest);
    void start_import();
    void start_naming();
    void finish_naming();
    void do_import(const std::filesystem::path& file_path);
    void pump_import();            // poll the file dialog while transfer is not active
    void do_zip_import(const std::filesystem::path& zip_path, ui::ZipConflictPolicy policy);
    void pump_zip_import();        // poll the zip file dialog while transfer is not active
    bool handle_zip_conflict_key(const SDL_Event& e);  // Flatten/Skip/Esc modal; true if consumed
    bool handle_delete_confirm_key(const SDL_Event& e);  // Del-confirm modal; true if consumed
    void start_tag_editor(bool import_list);  // G: open tag editor; Shift+G: import a tag list (Phase 21)
    void toggle_favorite_current();  // flip favorite on the focused tile (B)
    [[nodiscard]] bool current_allows_images() const;
    [[nodiscard]] bool current_allows_galleries() const;
    SDL_Texture*       thumb_texture(const vault::IndexNode& node);
    bool               pump_thumbs();   // upload finished off-thread thumb decodes

    void render_grid(gfx::Renderer& r, float W, float H);
    void render_list(gfx::Renderer& r, float W, float H);
    void render_import_progress(gfx::Renderer& r, float W, float H);  // background-import modal
    void poll_import_job();        // drain a finished background import (update())
    void draw_tile_thumb(gfx::Renderer& r, const vault::IndexNode& n,
                         const SDL_FRect& box);
    [[nodiscard]] int  hit_test(float mx, float my) const;  // item under cursor, or -1
    [[nodiscard]] std::string fit_name(std::string_view name, float max_w) const;

    gfx::Window&            win_;
    gfx::FontAtlas&         font_;
    vault::Vault&           vault_;
    gfx::TextureCache&      cache_;
    GridDialogs             dialogs_;   // file + folder dialogs (bundled, S1820)
    NavModel                nav_;
    SelectionModel          sel_;
    ConsentDialog           consent_;
    SearchOverlay           search_;
    TagEditor               tag_editor_;
    QuickSwitch             quick_switch_;   // declared before transfer_ so it copies
    TransferDialog          transfer_;       // the active path before transfer_ moves it
    GridLocation          initial_;   // where to (re)open: path + selected tile
    std::vector<const vault::IndexNode*> children_;
    int                   cols_ = 1;
    GalleryView           view_ = GalleryView::Grid;
    std::string           error_;
    std::string           status_;   // transient export result message

    // New-gallery naming + zip-import flow state — bundled to fix S1820 (>20 data members).
    struct PendingZip {
        std::filesystem::path path;
        std::string           gallery_name;
        ui::ZipDest           dest = ui::ZipDest::NewGallery;
        bool                  active = false;  // awaiting conflict resolution (Flatten/Skip)
        bool                  cbz = false;     // .cbz comic import (Phase 24): fixed one-leaf plan
    };
    struct Naming {
        bool        active = false;   // manual new-gallery text entry
        std::string buf;
        PendingZip  zip;              // zip import in flight
        bool        confirm_delete = false;  // Del on a media tile: awaiting Y/N confirm
        std::string tag_target;       // gallery path awaiting a tag-list import (Shift+G, Phase 21)
    };
    Naming naming_;

    // Off-thread thumbnail decoding, scoped to this grid (its own worker; see the
    // note in ImageViewer for why each screen keeps a separate one). Grouped to
    // keep the field count down.
    struct ThumbDecode {
        image::DecodeWorker          worker{image::decode_wake_event()};
        std::unordered_set<uint64_t> failed;   // thumbs that gave up decoding
    };
    ThumbDecode thumbs_;

    // Background CBZ import (Phase 24 fix). While active() the worker thread owns
    // the vault's single-thread file handle, so update()/render()/handle_event()
    // deliberately avoid touching the vault (no thumbnail reads) and show only a
    // progress modal until the import completes.
    ZipImportJob import_job_;
};

} // namespace ui
