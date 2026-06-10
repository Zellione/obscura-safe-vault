#include "platform/file_dialog.h"

#include <print>

namespace platform {

void SDLCALL FileDialog::on_files(void* userdata, const char* const* filelist, int)
{
    auto* self = static_cast<FileDialog*>(userdata);
    std::lock_guard lk(self->mtx_);
    self->paths_.clear();
    if (filelist) {
        for (const char* const* p = filelist; *p != nullptr; ++p)
            self->paths_.emplace_back(*p);
    } else {
        std::println(stderr, "[Platform] File dialog error: {}", SDL_GetError());
    }
    self->state_ = St::Done;
}

void FileDialog::open_vault(SDL_Window* parent)
{
    { std::lock_guard lk(mtx_); if (state_ == St::Open) return; state_ = St::Open; paths_.clear(); }
    static const SDL_DialogFileFilter f[] = {{"OSV Vault", "osv"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f, 2, nullptr, /*allow_many*/ false);
}

void FileDialog::open_images(SDL_Window* parent)
{
    { std::lock_guard lk(mtx_); if (state_ == St::Open) return; state_ = St::Open; paths_.clear(); }
    static const SDL_DialogFileFilter f[] = {{"Images", "jpg;jpeg;png;gif;bmp;tga;hdr"},
                                             {"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f, 2, nullptr, /*allow_many*/ true);
}

void FileDialog::open_keyfile(SDL_Window* parent)
{
    { std::lock_guard lk(mtx_); if (state_ == St::Open) return; state_ = St::Open; paths_.clear(); }
    static const SDL_DialogFileFilter f[] = {{"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f, 1, nullptr, /*allow_many*/ false);
}

bool FileDialog::busy() const noexcept
{
    std::lock_guard lk(mtx_);
    return state_ == St::Open;
}

std::optional<std::vector<std::string>> FileDialog::take_result()
{
    std::lock_guard lk(mtx_);
    if (state_ != St::Done) return std::nullopt;
    state_ = St::Idle;
    return std::move(paths_);
}

} // namespace platform
