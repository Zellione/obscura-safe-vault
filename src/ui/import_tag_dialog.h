#pragma once
#include <SDL3/SDL.h>
#include <optional>
#include "ui/import_tag_review.h"
#include "ui/text_input_model.h"
#include "ui/widgets.h"

namespace gfx { class Window; class Renderer; class FontAtlas; }
namespace vault { class Vault; }
namespace ui {
[[nodiscard]] std::vector<std::string> import_tag_vocabulary(vault::Vault& vault);
struct ImportTagDialogLayout {
    SDL_FRect box;
    SDL_FRect list;
    float pitch;
    float controls_y;
};
[[nodiscard]] ImportTagDialogLayout import_tag_dialog_layout(float font_px, float width, float height);

class ImportTagDialog {
public:
    explicit ImportTagDialog(gfx::Window& window) : window_(window) {}
    void open(ImportTagReview review, std::string title = "Review imported tags");
    void close();
    void set_error(std::string error) { error_ = std::move(error); }
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] std::optional<ImportTagReview> take_result();
    void handle_event(const SDL_Event& e);
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float width, float height);
private:
    enum class Mode { Browse, Filter, Rename, Replace };
    void rebuild();
    void edit(Mode mode);
    void accept_edit();
    void browse_key(SDL_Keycode key);
    void handle_key(SDL_Keycode key);
    void handle_pointer(const SDL_Event& event);
    void render_rows(gfx::Renderer& r, gfx::FontAtlas& font);
    void select_row(int delta);
    gfx::Window& window_;
    ImportTagReview review_;
    std::optional<ImportTagReview> result_;
    bool active_ = false;
    bool restore_text_input_ = false;
    bool skip_opening_text_ = false;
    Mode mode_ = Mode::Browse;
    TextInputModel input_;
    TextFieldChrome chrome_;
    std::string title_;
    std::string filter_;
    std::string error_;
    std::vector<size_t> visible_;
    int selected_ = 0;
    size_t editing_ = 0;
    SDL_FRect list_box_{};
    float pitch_ = 1;
    int first_ = 0;
};
} // namespace ui
