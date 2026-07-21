#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "image/decode_worker.h"
#include "ui/combine_dialog.h"
#include "ui/consent_dialog.h"
#include "ui/detail_panel.h"
#include "ui/file_op_job.h"
#include "ui/gallery_session_state.h"
#include "ui/gallery_view.h"
#include "ui/gif_model.h"
#include "ui/gif_playback.h"
#include "ui/nav_model.h"
#include "ui/quick_switch.h"
#include "ui/rename_dialog.h"
#include "ui/screen.h"
#include "ui/search_overlay.h"
#include "ui/secure_text_field.h"
#include "ui/selection_model.h"
#include "ui/tag_editor.h"
#include "ui/transfer_dialog.h"
#include "ui/zip_import_job.h"
#include "ui/zip_plan.h"

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }
namespace platform { class FileDialog; class FolderDialog; class VaultRegistry; }

namespace ui {

// Layout width available to the grid: the window minus whatever the detail panel
// reserves. A free friend for the same cpp:S1448 reason as current_gallery_view.
// Every layout site (render, scroll-to-selection, hit-test) MUST use this rather
// than win_.width(), or picking and drawing disagree once the panel is open.
[[nodiscard]] float content_width(const class GalleryGrid& g);

// Rebuild the panel's cached content when the focused node or selection changes.
// A free friend for the same cpp:S1448 reason as content_width.
void rebuild_detail(class GalleryGrid& g);

// Detail-panel key handling (Ctrl+Up/Down scroll, D toggle), extracted from
// handle_key_down to keep its cognitive complexity under cpp:S3776. Returns
// true when the key was consumed.
bool handle_detail_key(GalleryGrid& g, const SDL_KeyboardEvent& key);

// Where to (re)open the grid: a gallery path (empty = root), the selected
// tile index, and the initial List/Grid view (Phase 39 Part 2 session
// memory). Used to restore position + view mode when returning from the
// image viewer.
struct GridLocation {
    std::string path;
    int         selected = 0;
    GalleryView view     = GalleryView::GridM;
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
                GridVaultCtx vault_ctx, GallerySessionState& session, GridLocation at = {});

    void on_enter() override;
    void on_exit() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;
    // Keep repainting the progress bar and hold off the idle auto-lock while ANY
    // background job runs (import, export/delete, or an in-progress transfer) —
    // a worker thread owns the vault(s) meanwhile, so locking would wipe the key
    // mid-write.
    [[nodiscard]] bool animating() const override { return vault_busy(*this); }
    [[nodiscard]] bool blocks_idle_lock() const override { return vault_busy(*this); }
    [[nodiscard]] std::vector<ui::HelpGroup> help_groups() const override;

private:
    void handle_key_down(const SDL_KeyboardEvent& key);  // browse-mode keys
    void handle_naming_key(const SDL_Event& e);          // new-gallery text entry
    void handle_password_key(const SDL_Event& e);         // Phase 35: archive-password text entry
    void toggle_or_open();                               // Space: select image / open
    void refresh();
    void open_selected();
    void go_up();
    void toggle_select();          // toggle the current item in the export selection
    void start_export();           // open the consent modal for the current selection
    void start_transfer();         // open the move-to-another-vault dialog
    void start_rename();           // R: rename the focused tile
    void start_combine();          // Shift+M: combine nav_.path() into another gallery
    void jump_to_gallery(const std::string& path);   // absolute nav: ascend to root, then descend
    void do_export(const std::filesystem::path& dest);
    void start_import();
    void start_naming();
    void finish_naming();
    void pump_import();            // poll the file dialog while transfer is not active
    void do_zip_import(const std::filesystem::path& zip_path);
    void pump_zip_import();        // poll the zip file dialog while transfer is not active
    bool handle_delete_confirm_key(const SDL_Event& e);   // Del-confirm modal; true if consumed
    bool handle_compact_confirm_key(const SDL_Event& e);  // Shift+C compact modal; true if consumed (Phase 26)
    // Dispatches to whichever full-screen overlay (search/tag editor/transfer/
    // quick switch/export consent) is active; true if one consumed the event.
    // Extracted from handle_event to keep its cognitive complexity bounded (cpp:S3776).
    bool handle_overlay_event(const SDL_Event& e);
    void start_tag_editor(bool import_list);  // G: open tag editor; Shift+G: import a tag list (Phase 21)
    void toggle_favorite_current();  // flip favorite on the focused tile (B)
    void cycle_gallery_sort();       // Shift+S: cycle the open gallery's persisted sort key (Phase 37)
    SDL_Texture*       thumb_texture(const vault::IndexNode& node);
    bool               pump_thumbs();   // upload finished off-thread thumb decodes

    void render_grid(gfx::Renderer& r, float W, float H);
    void render_list(gfx::Renderer& r, float W, float H);

    // Drain a finished background import (called from update()). Kept a free friend
    // (not a member) to keep the class under the cpp:S1448 method cap and to keep
    // update()'s cognitive complexity (S3776) down. poll_file_job / vault_busy /
    // handle_job_input (Phase 25) are free friends for the same reasons.
    friend void poll_import_job(GalleryGrid& g);
    friend void poll_file_job(GalleryGrid& g);          // drain a finished export/delete job
    friend bool vault_busy(const GalleryGrid& g);       // any worker owns the vault(s)?
    friend bool handle_job_input(GalleryGrid& g, const SDL_Event& e);  // job-active Esc→cancel gate
    friend void handle_shift_c_key(GalleryGrid& g, const SDL_KeyboardEvent& key);  // Shift+C compact confirm
    friend void handle_delete_key(GalleryGrid& g);                                   // Del confirm
    friend bool handle_detail_key(GalleryGrid& g, const SDL_KeyboardEvent& key);     // detail panel scroll/toggle
    friend void set_cancelled_import_status(GalleryGrid& g, int imported, const char* noun);  // cancelled import waste hint
    // current_gallery_view is a free friend for the same S1448 reason: App reads it
    // (Phase 39 Part 2) to snapshot the outgoing grid's view mode into GallerySessionState.
    friend GalleryView current_gallery_view(const GalleryGrid& g);
    // The following are free friends for the same S1448/S3776 reasons, extracted
    // from start_transfer() and update() (Phase 44 SonarQube follow-up).
    friend void start_transfer_focused(GalleryGrid& g);
    friend void start_transfer_selection(GalleryGrid& g);
    friend void start_transfer_galleries_selection(GalleryGrid& g);
    friend void start_transfer_images_selection(GalleryGrid& g);
    friend void poll_transfer_and_combine(GalleryGrid& g);
    friend void poll_pending_pickers(GalleryGrid& g);
    friend void update_scroll_to_selection(GalleryGrid& g);
    friend void update_scroll_to_selection_list(GalleryGrid& g, int sel_idx, float H);
    friend void update_scroll_to_selection_grid(GalleryGrid& g, int sel_idx, float H);
    friend float content_width(const GalleryGrid& g);
    friend void  rebuild_detail(GalleryGrid& g);
    void draw_tile_thumb(gfx::Renderer& r, const vault::IndexNode& n,
                         const SDL_FRect& box);
    [[nodiscard]] int  hit_test(float mx, float my) const;  // item under cursor, or -1
    [[nodiscard]] std::string fit_name(std::string_view name, float max_w) const;
    void start_hover_animation(int tile);  // resolve node, check badge, construct playback, check budget

    gfx::Window&            win_;
    gfx::FontAtlas&         font_;
    vault::Vault&           vault_;
    gfx::TextureCache&      cache_;
    GridDialogs             dialogs_;   // file + folder dialogs (bundled, S1820)
    GallerySessionState&    session_;   // Phase 40 Part 2: per-path last-focused-tile memory
    NavModel                nav_;
    SelectionModel          sel_;
    ConsentDialog           consent_;
    SearchOverlay           search_;
    TagEditor               tag_editor_;
    QuickSwitch             quick_switch_;   // declared before transfer_ so it copies
    TransferDialog          transfer_;       // the active path before transfer_ moves it
    RenameDialog            rename_;         // Phase 44 Part 2
    CombineDialog           combine_;        // Phase 44 Part 4
    GridLocation          initial_;   // where to (re)open: path + selected tile
    std::vector<const vault::IndexNode*> children_;
    int                   cols_ = 1;
    GalleryView           view_ = GalleryView::GridM;
    float                 scroll_ = 0.0f;  // vertical scroll offset (pixels scrolled down)
    std::string           error_;
    std::string           status_;   // transient export result message

    // New-gallery naming + zip-import flow state — bundled to fix S1820 (>20 data members).
    struct PendingZip {
        std::filesystem::path path;
        std::string           gallery_name;
        bool                  active = false;  // import in flight (survives a password round-trip)
        bool                  cbz = false;     // .cbz/.cbr/.cb7/.cbt comic import: fixed one-leaf plan
        // .7z/.rar/.tar(+.gz/.xz)/.cbr/.cb7/.cbt (Phase 34) route through
        // ZipImportJob::start_archive/start_archive_cbz (libarchive) instead of
        // start_zip/start_cbz (miniz); orthogonal to `cbz` above, which only
        // selects the one-leaf-gallery plan vs the mirror/append plan.
        bool                  archive_backend = false;
        // Phase 35: a ZIP/CBZ detected as password-protected at pick time
        // (ui::zip_is_encrypted) — forces archive_backend above, and gates
        // whether do_zip_import threads naming_.password.buf through to the job.
        bool                  needs_password = false;
    };
    struct Naming {
        bool         active = false;   // manual new-gallery text entry
        std::string  buf;
        PendingZip   zip;              // zip import descriptor in flight
        // Phase 35: masked password entry for an encrypted zip/cbz. Mirrors
        // naming_.active's text-entry lifecycle (handle_password_key /
        // rendered as its own veiled modal), kept separate from `buf` since
        // both a gallery name and a password can be needed for the same
        // import (name first, then — if the archive turns out encrypted —
        // the password) without clobbering each other.
        struct PasswordPrompt {
            bool            active = false;
            bool            retry  = false;   // true after a wrong password (changes the modal's message)
            SecureTextField buf{256};
        };
        PasswordPrompt password;
        ZipImportJob import_job;       // background executor for the zip/cbz import (Phase 24)
        FileOpJob    file_op;          // background executor for export/delete/compact (Phase 25/26)
        bool         confirm_delete = false;  // Del on a media tile: awaiting Y/N confirm
        bool         confirm_compact = false; // Shift+C on the gallery: awaiting Y/N compact confirm (Phase 26)
        std::string  tag_target;       // gallery path awaiting a tag-list import (Shift+G, Phase 21)
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

    // Phase 48 detail panel. Bundled into a single member to stay under the
    // cpp:S1820 data-member cap. `key` is the cache key described in
    // rebuild_detail(): rebuilding walks a gallery subtree, so it happens only
    // when the focused node or the selection actually changes.
    struct DetailState {
        DetailPanelState panel;
        DetailContent    content;
        std::string      key;
        float            content_h = 0.0f;   // last drawn height, for scroll clamping
    };
    DetailState detail_;

    // Hover animation (Phase 47 Task 10). At most one animation at a time across
    // the whole screen. The gate tracks dwell time; playback renders the animation.
    GifHoverGate                 hover_gate_;
    std::unique_ptr<GifPlayback> hover_gif_;
    int                          hover_gif_tile_ = -1;
};

// Free friends of GalleryGrid (see the in-class declarations): poll_import_job /
// poll_file_job drain a finished background import / export-delete and update the
// grid's status + listing; vault_busy reports whether any worker owns the vault(s);
// handle_job_input swallows input (Esc→cancel) while a job runs; handle_shift_c_key /
// handle_delete_key extract key handlers to reduce handle_key_down's cognitive complexity;
// set_cancelled_import_status handles cancelled import waste hints; content_width and
// rebuild_detail manage detail panel lifecycle and layout (Phase 48).
void poll_import_job(GalleryGrid& g);
void poll_file_job(GalleryGrid& g);
[[nodiscard]] bool vault_busy(const GalleryGrid& g);
[[nodiscard]] bool handle_job_input(GalleryGrid& g, const SDL_Event& e);
void handle_shift_c_key(GalleryGrid& g, const SDL_KeyboardEvent& key);
void handle_delete_key(GalleryGrid& g);
void set_cancelled_import_status(GalleryGrid& g, int imported, const char* noun);
[[nodiscard]] GalleryView current_gallery_view(const GalleryGrid& g);

} // namespace ui
