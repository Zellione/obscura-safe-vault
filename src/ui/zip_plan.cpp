#include "ui/zip_plan.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>

namespace ui {
namespace {

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
void add_with_ancestors(const std::string& dir, std::set<std::string>& out)
{
    std::string cur;
    size_t start = 0;
    while (start <= dir.size()) {
        const auto slash = dir.find('/', start);
        const auto seg = dir.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        cur = cur.empty() ? seg : cur + "/" + seg;
        if (!cur.empty()) out.insert(cur);
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
}

} // namespace

bool is_supported_media_name(std::string_view name)
{
    static constexpr std::array<std::string_view, 14> exts{
        "jpg", "jpeg", "png", "gif", "bmp", "tga", "hdr", "webp", "heic", "avif",
        "mp4", "mkv", "webm", "mov"};
    // also m4v
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) return false;
    const std::string ext = to_lower(name.substr(dot + 1));
    if (ext == "m4v") return true;
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

ZipPlan build_zip_plan(const std::vector<ZipEntry>& entries,
                       ZipDest                      dest,
                       std::string_view             base_gallery,
                       std::string_view             new_gallery_name,
                       ZipConflictPolicy            policy)
{
    ZipPlan plan;

    // ---- Append: flatten everything into base_gallery, ignore structure. ----
    if (dest == ZipDest::Append) {
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            if (e.is_dir || (!e.path.empty() && e.path.back() == '/')) continue;
            const std::string name = basename_of(e.path);
            if (!is_supported_media_name(name)) { ++plan.skipped_unsupported; continue; }
            plan.placements.push_back({std::string(base_gallery), name, i});
        }
        return plan;
    }

    // ---- NewGallery: mirror the tree under base/new_gallery_name. ----
    const std::string root = join_path(base_gallery, new_gallery_name);

    // Pass 1: collect media files (zip-relative dir + basename + index) and the
    // set of dirs that directly hold media vs. dirs that have media-bearing subdirs.
    struct Media { std::string dir; std::string name; size_t index; };
    std::vector<Media> media;
    std::set<std::string> media_dirs;     // dirs directly holding media
    std::set<std::string> gallery_dirs;   // dirs we must create (media subtree)

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (e.is_dir || (!e.path.empty() && e.path.back() == '/')) continue;
        const std::string name = basename_of(e.path);
        if (!is_supported_media_name(name)) { ++plan.skipped_unsupported; continue; }
        const std::string dir = dirname_of(e.path);
        media.push_back({dir, name, i});
        if (!dir.empty()) media_dirs.insert(dir);
        add_with_ancestors(dir, gallery_dirs);
    }

    // A dir is "mixed" if it directly holds media AND is a strict ancestor of
    // another gallery dir (has a media-bearing subdirectory).
    auto is_ancestor_of_some_gallery = [&](const std::string& d) {
        const std::string prefix = d + "/";
        for (const auto& g : gallery_dirs)
            if (g.size() > prefix.size() && g.compare(0, prefix.size(), prefix) == 0) return true;
        return false;
    };
    std::set<std::string> mixed;
    for (const auto& d : media_dirs)
        if (is_ancestor_of_some_gallery(d))
            mixed.insert(d);

    if (!mixed.empty() && policy == ZipConflictPolicy::AskUser) {
        plan.needs_resolution = true;
        for (const auto& d : mixed) plan.mixed_dirs.push_back(d);
        return plan;
    }

    // Resolve mixed dirs per policy, building the final dir for each media file.
    auto target_dir_for = [&](const Media& m) -> std::string {
        if (mixed.count(m.dir)) {
            if (policy == ZipConflictPolicy::FlattenMixed) return m.dir + "/Files";
            return std::string("\x01skip");   // sentinel: SkipMixed
        }
        return m.dir;
    };

    std::set<std::string> final_galleries;
    for (const auto& m : media) {
        const std::string td = target_dir_for(m);
        if (td == "\x01skip") { ++plan.skipped_unsupported; continue; }
        const std::string gallery = td.empty() ? root : join_path(root, td);
        add_with_ancestors(gallery, final_galleries);   // ensure root + ancestors
        if (!root.empty()) final_galleries.insert(root);
        plan.placements.push_back({gallery, m.name, m.index});
    }
    if (!root.empty() && !plan.placements.empty()) final_galleries.insert(root);

    plan.galleries.assign(final_galleries.begin(), final_galleries.end());
    std::sort(plan.galleries.begin(), plan.galleries.end(),
              [](const std::string& a, const std::string& b) { return a.size() < b.size() || (a.size() == b.size() && a < b); });
    return plan;
}

} // namespace ui
