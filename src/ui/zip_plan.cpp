#include "ui/zip_plan.h"

#include "ui/natural_sort.h"
#include "vault/safe_name.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <set>
#include <span>
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

// Archive entry names are attacker-controlled: an entry called "../../.bashrc",
// "/etc/cron.d/x" or "..\\..\\evil.jpg" must import as an inert file, never as
// something that can later steer an export write out of the user's chosen
// folder. Defanging here — at the one place every media name is derived — means
// the plan only ever carries names Vault::add_image will accept.
std::string basename_of(std::string_view path)
{
    const auto slash = path.find_last_of('/');
    return vault::sanitize_node_name(
        slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string dirname_of(std::string_view path)
{
    const auto slash = path.find_last_of('/');
    return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
}

// An archive-relative directory becomes a chain of gallery names, so every
// component of it needs the same treatment as a filename. '/' still separates.
std::string sanitize_dir_path(std::string_view dir)
{
    std::string out;
    size_t i = 0;
    while (i < dir.size()) {
        while (i < dir.size() && dir[i] == '/') ++i;
        const size_t start = i;
        while (i < dir.size() && dir[i] != '/') ++i;
        if (i > start) {
            if (!out.empty()) out += '/';
            out += vault::sanitize_node_name(dir.substr(start, i - start));
        }
    }
    return out;
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
// created (`gallery_dirs`). Unsupported / directory entries bump `skipped`;
// a top-level meta.json (`meta_idx`) is excluded without counting (Phase 27).
std::vector<MediaFile> collect_media(const std::vector<ZipEntry>& entries,
                                     std::optional<size_t> meta_idx,
                                     StringSet& media_dirs, StringSet& gallery_dirs, int& skipped)
{
    std::vector<MediaFile> media;
    for (size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& e = entries[i];
        if (entry_is_dir(e) || i == meta_idx) continue;
        std::string name = basename_of(e.path);
        if (!is_supported_media_name(name)) { ++skipped; continue; }
        std::string dir = sanitize_dir_path(dirname_of(e.path));
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

namespace {

// Lower-cased filename extension is one of `exts`.
bool ext_in(std::string_view name, std::span<const std::string_view> exts)
{
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) return false;
    const std::string ext = to_lower(name.substr(dot + 1));
    return std::ranges::find(exts, ext) != exts.end();
}

constexpr std::array<std::string_view, 10> kImageExts{
    "jpg", "jpeg", "png", "gif", "bmp", "tga", "hdr", "webp", "heic", "avif"};
constexpr std::array<std::string_view, 5> kVideoExts{"mp4", "mkv", "webm", "mov", "m4v"};

} // namespace

std::optional<size_t> find_meta_entry(const std::vector<ZipEntry>& entries)
{
    for (size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& e = entries[i];
        if (!entry_is_dir(e) && to_lower(e.path) == "meta.json") return i;
    }
    return std::nullopt;
}

bool is_supported_image_name(std::string_view name)
{
    return ext_in(name, kImageExts);
}

bool is_supported_media_name(std::string_view name)
{
    return ext_in(name, kImageExts) || ext_in(name, kVideoExts);
}

ZipPlan build_cbz_plan(const std::vector<ZipEntry>& entries,
                       std::string_view             base_gallery,
                       std::string_view             gallery_name)
{
    ZipPlan plan;
    const std::string gallery = join_path(base_gallery, gallery_name);

    // Collect supported image entries; non-image files (videos/other) are
    // skipped + counted, directory entries and a top-level meta.json (consumed
    // by the importer, Phase 27) are ignored silently.
    const std::optional<size_t> meta_idx = find_meta_entry(entries);
    struct Page { std::string path; std::string name; size_t index; };
    std::vector<Page> pages;
    for (size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& e = entries[i];
        if (entry_is_dir(e) || i == meta_idx) continue;
        std::string name = basename_of(e.path);
        if (!is_supported_image_name(name)) { ++plan.skipped_unsupported; continue; }
        pages.emplace_back(e.path, std::move(name), i);
    }

    // Natural reading order over the FULL archive path so pages from sub-chapters
    // interleave correctly (chapter1/… before chapter2/…). stable_sort keeps the
    // archive order for entries that compare equal (case-only differences).
    std::ranges::stable_sort(pages, [](const Page& a, const Page& b) {
        return natural_less(a.path, b.path);
    });

    // Flattening collapses subfolders into one gallery, so two pages can share a
    // basename (e.g. ch1/01.jpg and ch2/01.jpg). add_image would reject the second
    // as a duplicate, silently losing a page; disambiguate by prefixing the source
    // directory (slashes → '_'), falling back to a counter. Unique names are left
    // untouched so the common flat/single-folder comic keeps clean page names.
    StringSet used;
    for (const Page& p : pages) {
        std::string fname = p.name;   // already safe: basename_of sanitized it
        if (used.contains(fname)) {
            // The prefix comes from the archive's directory names, so it needs
            // defanging too — and the join can overrun the 255-byte component
            // limit, which sanitize_node_name truncates. The counter therefore
            // goes FIRST: truncation only ever eats the tail, so distinct n keep
            // producing distinct names and the loop is guaranteed to terminate.
            std::string prefix = dirname_of(p.path);
            std::ranges::replace(prefix, '/', '_');
            std::string cand = vault::sanitize_node_name(
                prefix.empty() ? fname : std::format("{}_{}", prefix, fname));
            int n = 1;
            while (used.contains(cand))
                cand = vault::sanitize_node_name(std::format("{}_{}_{}", n++, prefix, fname));
            fname = std::move(cand);
        }
        used.insert(fname);
        plan.placements.emplace_back(gallery, std::move(fname), p.index);
    }
    if (!gallery.empty() && !plan.placements.empty())
        plan.galleries.push_back(gallery);
    return plan;
}

ZipPlan build_zip_plan(const std::vector<ZipEntry>& entries,
                       std::string_view             base_gallery,
                       std::string_view             new_gallery_name,
                       ZipConflictPolicy            policy)
{
    const std::optional<size_t> meta_idx = find_meta_entry(entries);

    // ---- Mirror the tree under base_gallery/new_gallery_name. ----
    ZipPlan plan;
    const std::string root = join_path(base_gallery, new_gallery_name);

    StringSet media_dirs;     // dirs directly holding media
    StringSet gallery_dirs;   // dirs we must create (media subtree)
    const std::vector<MediaFile> media =
        collect_media(entries, meta_idx, media_dirs, gallery_dirs, plan.skipped_unsupported);
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
