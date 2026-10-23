#pragma once
#include <SDL3/SDL.h>
#include <future>
#include "ui/secure_text_input.h"
#include "ui/widgets.h"
#include "vault/rename_batch.h"
namespace gfx {
class Window;
class Renderer;
class FontAtlas;
}  // namespace gfx
namespace ui {
class ImportQueue;
class BulkRename {
public:
    BulkRename(vault::Vault& v, gfx::Window& w, ImportQueue& q) : vault_(v), win_(w), queue_(q) {}
    ~BulkRename();
    void open(std::vector<vault::RenameId> ids);
    void close();
    void update();
    bool handle_event(const SDL_Event& e);
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float w, float h);
    [[nodiscard]] bool active() const
    {
        return active_;
    }
    [[nodiscard]] bool busy() const
    {
        return work_.valid();
    }
    [[nodiscard]] bool consume_completed();

private:
    void preview();
    void apply();
    vault::Vault& vault_;
    gfx::Window& win_;
    ImportQueue& queue_;
    std::future<void> work_;
    vault::OpProgress progress_;
    std::vector<vault::RenameId> ids_;
    vault::RenamePreview preview_;
    vault::RenameOptions options_;
    SecureTextInput pattern_{4096}, replacement_{4096};
    TextFieldChrome pattern_chrome_, replacement_chrome_;
    bool active_ = false, exclusive_ = false, applying_ = false, completed_ = false;
    bool second_field_ = false, preview_ready_ = false, details_ = false;
    size_t first_row_ = 0;
    vault::VaultResult result_ = vault::VaultResult::Ok;
    std::string error_;
};
}  // namespace ui
