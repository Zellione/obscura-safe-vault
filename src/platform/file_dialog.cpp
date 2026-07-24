#include "platform/file_dialog.h"

#include "platform/paths.h"

#include <array>
#include <print>

namespace platform {

void SDLCALL FileDialog::on_files(void* userdata, const char* const* filelist, int)
{
    auto* self = static_cast<FileDialog*>(userdata);
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
                std::println(stderr, "[Platform] ignoring unusable path from file dialog");
        }
    } else {
        std::println(stderr, "[Platform] File dialog error: {}", SDL_GetError());
    }
    self->state_ = St::Done;
}

bool FileDialog::begin_open(Purpose purpose)
{
    std::lock_guard lk(mtx_);
    if (state_ == St::Open) return false;
    state_ = St::Open;
    purpose_ = purpose;
    paths_.clear();
    return true;
}

void FileDialog::open_vault(SDL_Window* parent)
{
    if (!begin_open(Purpose::Vault)) return;
    static constexpr std::array f{SDL_DialogFileFilter{"OSV Vault", "osv"},
                                  SDL_DialogFileFilter{"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f.data(),
                           static_cast<int>(f.size()), nullptr, /*allow_many*/ false);
}

void FileDialog::open_images(SDL_Window* parent)
{
    if (!begin_open(Purpose::Images)) return;
    static constexpr std::array f{
        SDL_DialogFileFilter{"Images & video",
                             "jpg;jpeg;png;gif;bmp;tga;hdr;webp;heic;avif;mp4;mkv;webm;mov;m4v"},
        SDL_DialogFileFilter{"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f.data(),
                           static_cast<int>(f.size()), nullptr, /*allow_many*/ true);
}

void FileDialog::open_keyfile(SDL_Window* parent)
{
    if (!begin_open(Purpose::Keyfile)) return;
    static constexpr std::array f{SDL_DialogFileFilter{"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f.data(),
                           static_cast<int>(f.size()), nullptr, /*allow_many*/ false);
}

void FileDialog::open_zip(SDL_Window* parent)
{
    if (!begin_open(Purpose::Zip)) return;
    // 7z/rar/tar(+gz/xz)/cbr/cb7/cbt (Phase 34) route through
    // ui::import_archive/import_archive_cbz instead of the miniz zip/cbz path;
    // see GalleryGrid's do_zip_import. Offered regardless of
    // OSV_VENDORED_ARCHIVE — a build without it reports a graceful "not
    // supported" error on pick instead of hiding the option.
    static constexpr std::array f{
        SDL_DialogFileFilter{"Zip & comic archives", "zip;cbz;7z;rar;tar;gz;xz;cbr;cb7;cbt"},
        SDL_DialogFileFilter{"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f.data(),
                           static_cast<int>(f.size()), nullptr, /*allow_many*/ true);
}

void FileDialog::open_tag_list(SDL_Window* parent)
{
    if (!begin_open(Purpose::TagList)) return;
    static constexpr std::array f{
        SDL_DialogFileFilter{"Text files", "txt"},
        SDL_DialogFileFilter{"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f.data(),
                           static_cast<int>(f.size()), nullptr, /*allow_many*/ false);
}

void FileDialog::save_keyfile(SDL_Window* parent)
{
    if (!begin_open(Purpose::SaveKeyfile)) return;
    static constexpr std::array f{SDL_DialogFileFilter{"Keyfile", "key"},
                                  SDL_DialogFileFilter{"All files", "*"}};
    SDL_ShowSaveFileDialog(on_files, this, parent, f.data(),
                           static_cast<int>(f.size()), nullptr);
}

void FileDialog::save_vault(SDL_Window* parent)
{
    if (!begin_open(Purpose::SaveVault)) return;
    static constexpr std::array f{SDL_DialogFileFilter{"OSV Vault", "osv"},
                                  SDL_DialogFileFilter{"All files", "*"}};
    SDL_ShowSaveFileDialog(on_files, this, parent, f.data(),
                           static_cast<int>(f.size()), nullptr);
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
    purpose_ = Purpose::None;
    return std::move(paths_);
}

std::optional<std::vector<std::string>> FileDialog::take_result(Purpose want)
{
    std::lock_guard lk(mtx_);
    // Leave a result tagged for a different purpose untouched so the handler that
    // opened it can still claim it (regression: a [Z] zip pick must not be drained
    // by GalleryGrid's [I] image-import poller).
    if (state_ != St::Done || purpose_ != want) return std::nullopt;
    state_ = St::Idle;
    purpose_ = Purpose::None;
    return std::move(paths_);
}

} // namespace platform
