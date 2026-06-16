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
#include "ui/screen.h"
#include "ui/search_overlay.h"
#include "ui/selection_model.h"
#include "ui/tag_editor.h"

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }
namespace platform { class FileDialog; class FolderDialog; }

namespace ui {

// Where to (re)open the grid: a gallery path (empty = root) and the selected
// tile index. Used to restore position when returning from the image viewer.
struct GridLocation {
    std::string path;
    int         selected = 0;
};

class GalleryGrid : public Screen {
public:
    GalleryGrid(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                gfx::TextureCache& cache, platform::FileDialog& dlg,
                platform::FolderDialog& folder_dlg, GridLocation at = {});

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;

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
    void do_export(const std::filesystem::path& dest);
    void start_import();
    void start_naming();
    void finish_naming();
    void do_import(const std::filesystem::path& file_path);
    void start_search();       // open the search overlay
    void start_tag_editor();   // open the tag editor for the focused tile
    [[nodiscard]] bool current_allows_images() const;
    [[nodiscard]] bool current_allows_galleries() const;
    SDL_Texture*       thumb_texture(const vault::IndexNode& node);
    bool               pump_thumbs();   // upload finished off-thread thumb decodes

    void render_grid(gfx::Renderer& r, float W, float H);
    void render_list(gfx::Renderer& r, float W, float H);
    void draw_tile_thumb(gfx::Renderer& r, const vault::IndexNode& n,
                         const SDL_FRect& box);
    [[nodiscard]] int  hit_test(float mx, float my) const;  // item under cursor, or -1
    [[nodiscard]] std::string fit_name(std::string_view name, float max_w) const;

    gfx::Window&            win_;
    gfx::FontAtlas&         font_;
    vault::Vault&           vault_;
    gfx::TextureCache&      cache_;
    platform::FileDialog&   dlg_;
    platform::FolderDialog& folder_dlg_;
    NavModel                nav_;
    SelectionModel          sel_;
    ConsentDialog           consent_;
    SearchOverlay           search_;
    TagEditor               tag_editor_;
    GridLocation          initial_;   // where to (re)open: path + selected tile
    std::vector<const vault::IndexNode*> children_;
    int                   cols_ = 1;
    GalleryView           view_ = GalleryView::Grid;
    std::string           error_;
    std::string           status_;   // transient export result message
    bool                  naming_ = false;
    std::string           name_buf_;

    // Off-thread thumbnail decoding, scoped to this grid (its own worker; see the
    // note in ImageViewer for why each screen keeps a separate one). Grouped to
    // keep the field count down.
    struct ThumbDecode {
        image::DecodeWorker          worker{image::decode_wake_event()};
        std::unordered_set<uint64_t> failed;   // thumbs that gave up decoding
    };
    ThumbDecode thumbs_;
};

} // namespace ui
