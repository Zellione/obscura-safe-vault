#include "platform/gallery_view_pref.h"

#include <fstream>
#include "platform/safe_print.h"
#include <string>

#include "platform/path_utf8.h"
#include "platform/paths.h"

namespace platform {

GalleryViewPref::GalleryViewPref(std::filesystem::path file) : file_(std::move(file)) {}

GalleryViewPref GalleryViewPref::default_location()
{
    auto dir = config_dir();
    if (dir.empty()) return GalleryViewPref{};                // no config dir → inert pref
    return GalleryViewPref{dir / "gallery_view.conf"};
}

ui::GalleryView GalleryViewPref::load() const
{
    if (file_.empty()) return ui::GalleryView::GridM;

    std::ifstream in(file_, std::ios::binary);
    if (!in) return ui::GalleryView::GridM;         // missing file → default

    std::string slug;
    std::getline(in, slug);
    if (!slug.empty() && slug.back() == '\r') slug.pop_back();   // tolerate CRLF
    return ui::gallery_view_from_slug(slug);                  // unknown slug → default
}

bool GalleryViewPref::save(ui::GalleryView view) const
{
    if (file_.empty()) return false;

    // Atomic replace: write a sibling temp file, then rename over the target so a
    // crash mid-write never leaves a torn value.
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            platform::safe_println(stderr, "[GalleryViewPref] cannot write {}", path_to_utf8(tmp));
            return false;
        }
        out << ui::gallery_view_slug(view) << '\n';
        out.flush();
        if (!out) {
            platform::safe_println(stderr, "[GalleryViewPref] write error on {}", path_to_utf8(tmp));
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        platform::safe_println(stderr, "[GalleryViewPref] rename failed: {}", ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace platform
