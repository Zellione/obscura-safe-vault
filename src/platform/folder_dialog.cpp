#include "platform/folder_dialog.h"

#include "platform/safe_print.h"

namespace platform {

void SDLCALL FolderDialog::on_folder(void* userdata, const char* const* filelist, int)
{
    auto* self = static_cast<FolderDialog*>(userdata);
    std::lock_guard lk(self->mtx_);
    self->path_.clear();
    if (filelist) {
        if (*filelist != nullptr) self->path_ = *filelist;   // first (only) folder
    } else {
        platform::safe_println(stderr, "[Platform] Folder dialog error: {}", SDL_GetError());
    }
    self->state_ = St::Done;
}

void FolderDialog::open(SDL_Window* parent)
{
    {
        std::lock_guard lk(mtx_);
        if (state_ == St::Open) return;
        state_ = St::Open;
        path_.clear();
    }
    SDL_ShowOpenFolderDialog(on_folder, this, parent, nullptr, /*allow_many*/ false);
}

bool FolderDialog::busy() const noexcept
{
    std::lock_guard lk(mtx_);
    return state_ == St::Open;
}

std::optional<std::string> FolderDialog::take_result()
{
    std::lock_guard lk(mtx_);
    if (state_ != St::Done) return std::nullopt;
    state_ = St::Idle;
    return std::move(path_);
}

} // namespace platform
