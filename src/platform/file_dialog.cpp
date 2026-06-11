#include "platform/file_dialog.h"

#include <array>
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

bool FileDialog::begin_open()
{
    std::lock_guard lk(mtx_);
    if (state_ == St::Open) return false;
    state_ = St::Open;
    paths_.clear();
    return true;
}

void FileDialog::open_vault(SDL_Window* parent)
{
    if (!begin_open()) return;
    static constexpr std::array f{SDL_DialogFileFilter{"OSV Vault", "osv"},
                                  SDL_DialogFileFilter{"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f.data(),
                           static_cast<int>(f.size()), nullptr, /*allow_many*/ false);
}

void FileDialog::open_images(SDL_Window* parent)
{
    if (!begin_open()) return;
    static constexpr std::array f{SDL_DialogFileFilter{"Images", "jpg;jpeg;png;gif;bmp;tga;hdr"},
                                  SDL_DialogFileFilter{"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f.data(),
                           static_cast<int>(f.size()), nullptr, /*allow_many*/ true);
}

void FileDialog::open_keyfile(SDL_Window* parent)
{
    if (!begin_open()) return;
    static constexpr std::array f{SDL_DialogFileFilter{"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f.data(),
                           static_cast<int>(f.size()), nullptr, /*allow_many*/ false);
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
