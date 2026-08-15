#pragma once

#include <filesystem>

#include "ui/gallery_view.h"

namespace platform {

// A config-dir record of the chosen gallery view (Phase 84). Stores NO secrets —
// only the view's stable slug (one short token), written atomically (temp file
// + rename) just like vault_registry. An unknown or absent value loads as the
// default view, so a corrupt or hand-edited file never breaks startup.
//
// Like VaultRegistry, the only state is the backing-file path; load()/save()
// read/write that file rather than any in-memory selection, so save() is const.
class GalleryViewPref {
public:
    GalleryViewPref() = default;                                 // empty: no backing file
    explicit GalleryViewPref(std::filesystem::path file);

    [[nodiscard]] static GalleryViewPref default_location();     // config_dir()/"gallery_view.conf"

    [[nodiscard]] ui::GalleryView load() const;                  // missing/unknown → default
    bool save(ui::GalleryView view) const;                       // persist; false on I/O failure

    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

private:
    std::filesystem::path file_;
};

} // namespace platform
