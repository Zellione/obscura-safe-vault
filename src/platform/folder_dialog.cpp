#include "platform/folder_dialog.h"

#include "platform/paths.h"

#include <print>

namespace platform {

void SDLCALL FolderDialog::on_folder(void* userdata, const char* const* filelist, int)
{
    auto* self = static_cast<FolderDialog*>(userdata);
    std::lock_guard lk(self->mtx_);
    self->paths_.clear();
    if (filelist) {
        // The single choke point through which every externally-chosen path
        // enters the program. Normalize here, before any of it can reach fopen();
        // a path we cannot make sense of is dropped rather than passed on.
        for (const char* const* p = filelist; *p != nullptr; ++p) {
            if (auto norm = normalize_user_path(*p))
                self->paths_.push_back(norm->string());
            else
                std::println(stderr, "[Platform] ignoring unusable path from folder dialog");
        }
    } else {
        std::println(stderr, "[Platform] Folder dialog error: {}", SDL_GetError());
    }
    self->state_ = St::Done;
}

bool FolderDialog::begin_open(Purpose purpose)
{
    std::lock_guard lk(mtx_);
    if (state_ == St::Open) return false;
    state_ = St::Open;
    purpose_ = purpose;
    paths_.clear();
    return true;
}

void FolderDialog::open(SDL_Window* parent, Purpose purpose, bool allow_many)
{
    if (!begin_open(purpose)) return;
    SDL_ShowOpenFolderDialog(on_folder, this, parent, nullptr, allow_many);
}

bool FolderDialog::busy() const noexcept
{
    std::lock_guard lk(mtx_);
    return state_ == St::Open;
}

std::optional<std::vector<std::string>> FolderDialog::take_result()
{
    std::lock_guard lk(mtx_);
    if (state_ != St::Done) return std::nullopt;
    state_ = St::Idle;
    purpose_ = Purpose::None;
    return std::move(paths_);
}

std::optional<std::vector<std::string>> FolderDialog::take_result(Purpose want)
{
    std::lock_guard lk(mtx_);
    // Leave a result tagged for a different purpose untouched so the handler that
    // opened it can still claim it (regression: an import-folder pick must not be
    // drained by the export handler).
    if (state_ != St::Done || purpose_ != want) return std::nullopt;
    state_ = St::Idle;
    purpose_ = Purpose::None;
    return std::move(paths_);
}

} // namespace platform
