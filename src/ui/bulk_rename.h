#pragma once
#include <SDL3/SDL.h>
#include <atomic>
#include <functional>
#include <thread>
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
    BulkRename(const BulkRename&) = delete;
    BulkRename& operator=(const BulkRename&) = delete;
    BulkRename(BulkRename&&) = delete;
    BulkRename& operator=(BulkRename&&) = delete;
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
        return work_.thread.joinable();
    }
    [[nodiscard]] bool consume_completed();

private:
    friend struct BulkRenameTestPeer;
    void launch(std::function<void()> task);
    template <typename Work> void run_work(const Work& task);
    bool handle_key(SDL_Keycode key);
    void render_progress(gfx::Renderer& r, gfx::FontAtlas& font, float w, float h) const;
    [[nodiscard]] bool finish_work();
    void preview();
    void apply();
    vault::Vault& vault_;
    gfx::Window& win_;
    ImportQueue& queue_;
    enum class WorkFailure { None, Memory, Size, System, Bounds };
    struct WorkState {
        std::atomic_bool done{false};
        WorkFailure failure = WorkFailure::None;  // Read only after joining.
        std::jthread thread;
    };
    WorkState work_;
    vault::OpProgress progress_;
    std::vector<vault::RenameId> ids_;
    vault::RenamePreview preview_;
    vault::RenameOptions options_;
    struct RenameField {
        SecureTextInput input{4096};
        TextFieldChrome chrome;
    };
    RenameField pattern_;
    RenameField replacement_;
    bool active_ = false;
    bool exclusive_ = false;
    bool applying_ = false;
    bool completed_ = false;
    bool second_field_ = false;
    bool preview_ready_ = false;
    bool details_ = false;
    size_t first_row_ = 0;
    vault::VaultResult result_ = vault::VaultResult::Ok;
    std::string error_;
};
}  // namespace ui
