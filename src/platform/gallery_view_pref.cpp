#include "platform/gallery_view_pref.h"

#include <fstream>
#include <string>

#include "platform/atomic_write.h"
#include "platform/path_utf8.h"
#include "platform/paths.h"
#include "platform/safe_print.h"

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
    return platform::atomic_write_file(file_, std::string(ui::gallery_view_slug(view)) + "\n",
                                       "GalleryViewPref");
}

} // namespace platform
