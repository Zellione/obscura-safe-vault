#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

#include "ui/nav_model.h"
#include "ui/screen.h"

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }
namespace platform { class FileDialog; }

namespace ui {

class GalleryGrid : public Screen {
public:
    GalleryGrid(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                gfx::TextureCache& cache, platform::FileDialog& dlg);

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;

private:
    void refresh();
    void open_selected();
    void go_up();
    void start_import();
    void start_naming();
    void finish_naming();
    void do_import(const std::filesystem::path& file_path);
    [[nodiscard]] bool current_allows_images() const;
    [[nodiscard]] bool current_allows_galleries() const;
    SDL_Texture*       thumb_texture(const vault::IndexNode& node);

    gfx::Window&          win_;
    gfx::FontAtlas&       font_;
    vault::Vault&         vault_;
    gfx::TextureCache&    cache_;
    platform::FileDialog& dlg_;
    NavModel              nav_;
    std::vector<const vault::IndexNode*> children_;
    int                   cols_ = 1;
    std::string           error_;
    bool                  naming_ = false;
    std::string           name_buf_;
};

} // namespace ui
