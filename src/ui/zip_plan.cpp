#include "ui/zip_plan.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <utility>

namespace ui {
namespace {

// Transparent comparator so the string sets accept heterogeneous lookups and
// satisfy cpp:S6045.
using StringSet = std::set<std::string, std::less<>>;

std::string to_lower(std::string_view s)
{
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string basename_of(std::string_view path)
{
    const auto slash = path.find_last_of('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string dirname_of(std::string_view path)
{
    const auto slash = path.find_last_of('/');
    return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
}

std::string join_path(std::string_view a, std::string_view b)
{
    if (a.empty()) return std::string(b);
    if (b.empty()) return std::string(a);
    return std::string(a) + "/" + std::string(b);
}

// All ancestor prefixes of `dir` plus `dir` itself, "" excluded.
void add_with_ancestors(std::string_view dir, StringSet& out)
{
    std::string cur;
    size_t start = 0;
    while (start <= dir.size()) {
        const auto slash = dir.find('/', start);
        const auto seg = dir.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        cur = cur.empty() ? std::string(seg) : cur + "/" + std::string(seg);
        if (!cur.empty()) out.insert(cur);
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
}

bool entry_is_dir(const ZipEntry& e)
{
    return e.is_dir || (!e.path.empty() && e.path.back() == '/');
}

struct MediaFile {
    std::string dir;    // zip-relative parent directory ("" = archive root)
    std::string name;   // basename
    size_t      index;  // index back into the caller's entry list
};

// Scan entries for supported media files, recording the directories that
// directly hold media (`media_dirs`) and every gallery directory that must be
// created (`gallery_dirs`). Unsupported / directory entries bump `skipped`.
std::vector<MediaFile> collect_media(const std::vector<ZipEntry>& entries,
                                     StringSet& media_dirs, StringSet& gallery_dirs, int& skipped)
{
    std::vector<MediaFile> media;
    for (size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& e = entries[i];
        if (entry_is_dir(e)) continue;
        std::string name = basename_of(e.path);
        if (!is_supported_media_name(name)) { ++skipped; continue; }
        std::string dir = dirname_of(e.path);
        if (!dir.empty()) media_dirs.insert(dir);
        add_with_ancestors(dir, gallery_dirs);
        media.emplace_back(std::move(dir), std::move(name), i);
    }
    return media;
}

// A media-holding dir is "mixed" (would break the leaf invariant) when it is a
// strict ancestor of another gallery dir, i.e. it has a media-bearing subdirectory.
StringSet find_mixed_dirs(const StringSet& media_dirs, const StringSet& gallery_dirs)
{
    StringSet mixed;
    for (const auto& d : media_dirs) {
        const std::string prefix = d + "/";
        const bool has_media_subdir = std::ranges::any_of(gallery_dirs, [&](std::string_view g) {
            return g.size() > prefix.size() && g.compare(0, prefix.size(), prefix) == 0;
        });
        if (has_media_subdir) mixed.insert(d);
    }
    return mixed;
}

} // namespace

bool is_supported_media_name(std::string_view name)
{
    static constexpr std::array<std::string_view, 15> exts{
        "jpg", "jpeg", "png", "gif", "bmp", "tga", "hdr", "webp", "heic", "avif",
        "mp4", "mkv", "webm", "mov", "m4v"};
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) return false;
    const std::string ext = to_lower(name.substr(dot + 1));
    return std::ranges::find(exts, ext) != exts.end();
}

ZipPlan build_zip_plan(const std::vector<ZipEntry>& entries,
                       ZipDest                      dest,
                       std::string_view             base_gallery,
                       std::string_view             new_gallery_name,
                       ZipConflictPolicy            policy)
{
    ZipPlan plan;

    // ---- Append: flatten every media file into base_gallery, ignore structure. ----
    if (dest == ZipDest::Append) {
        for (size_t i = 0; i < entries.size(); ++i) {
            const ZipEntry& e = entries[i];
            if (entry_is_dir(e)) continue;
            std::string name = basename_of(e.path);
            if (!is_supported_media_name(name)) { ++plan.skipped_unsupported; continue; }
            plan.placements.emplace_back(std::string(base_gallery), std::move(name), i);
        }
        return plan;
    }

    // ---- NewGallery: mirror the tree under base/new_gallery_name. ----
    const std::string root = join_path(base_gallery, new_gallery_name);

    StringSet media_dirs;     // dirs directly holding media
    StringSet gallery_dirs;   // dirs we must create (media subtree)
    const std::vector<MediaFile> media =
        collect_media(entries, media_dirs, gallery_dirs, plan.skipped_unsupported);
    const StringSet mixed = find_mixed_dirs(media_dirs, gallery_dirs);

    if (!mixed.empty() && policy == ZipConflictPolicy::AskUser) {
        plan.needs_resolution = true;
        for (const auto& d : mixed) plan.mixed_dirs.emplace_back(d);
        return plan;
    }

    // Resolve each media file to a gallery, applying the conflict policy to a
    // mixed dir: FlattenMixed routes its direct media into a "Files" child leaf;
    // SkipMixed drops them (counted as skipped).
    StringSet final_galleries;
    for (const MediaFile& m : media) {
        const bool is_mixed = mixed.contains(m.dir);
        if (is_mixed && policy == ZipConflictPolicy::SkipMixed) { ++plan.skipped_unsupported; continue; }
        const std::string target_dir = is_mixed ? m.dir + "/Files" : m.dir;
        const std::string gallery = target_dir.empty() ? root : join_path(root, target_dir);
        add_with_ancestors(gallery, final_galleries);
        plan.placements.emplace_back(gallery, m.name, m.index);
    }
    if (!root.empty() && !plan.placements.empty()) final_galleries.insert(root);

    plan.galleries.assign(final_galleries.begin(), final_galleries.end());
    std::ranges::sort(plan.galleries, [](std::string_view a, std::string_view b) {
        return a.size() < b.size() || (a.size() == b.size() && a < b);
    });
    return plan;
}

} // namespace ui
