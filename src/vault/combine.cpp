#include "vault/combine.h"

#include "vault/transfer.h"

namespace vault {

namespace {

std::string child_path(std::string_view gallery, std::string_view name)
{
    std::string p(gallery);
    if (!p.empty()) p += '/';
    p += name;
    return p;
}

std::string parent_path(std::string_view gallery)
{
    const auto slash = gallery.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string(gallery.substr(0, slash));
}

std::string last_segment(std::string_view gallery)
{
    const auto slash = gallery.rfind('/');
    return slash == std::string_view::npos ? std::string(gallery)
                                           : std::string(gallery.substr(slash + 1));
}

const IndexNode* find_child(const Vault& v, std::string_view parent, std::string_view name)
{
    for (const auto* c : v.list(parent)) if (c->name == name) return c;
    return nullptr;
}

bool holds_media(const Vault& v, std::string_view gallery)
{
    for (const auto* c : v.list(gallery)) if (c->is_media()) return true;
    return false;
}

bool holds_subgalleries(const Vault& v, std::string_view gallery)
{
    for (const auto* c : v.list(gallery)) if (c->is_gallery()) return true;
    return false;
}

bool types_compatible(const Vault& src, std::string_view src_gallery,
                      const Vault& dst, std::string_view dst_gallery)
{
    if (holds_media(src, src_gallery) && holds_subgalleries(dst, dst_gallery)) return false;
    if (holds_subgalleries(src, src_gallery) && holds_media(dst, dst_gallery)) return false;
    return true;
}

// True only when dst_gallery is src_gallery itself or lives inside it — the
// one direction that would orphan the destination once the source is
// deleted. The reverse (src nested inside dst) is a legitimate "flatten
// upward" and is NOT a cycle. Always false across two different vaults.
bool would_cycle(const Vault& src, const Vault& dst, std::string_view src_gallery,
                 std::string_view dst_gallery)
{
    if (&src != &dst) return false;
    if (dst_gallery == src_gallery) return true;
    return std::string(dst_gallery).starts_with(std::string(src_gallery) + "/");
}

int count_media(const Vault& v, std::string_view gallery)
{
    int n = 0;
    for (const auto* c : v.list(gallery)) {
        if (c->is_media())        ++n;
        else if (c->is_gallery()) n += count_media(v, child_path(gallery, c->name));
    }
    return n;
}

void walk_all_galleries(const Vault& v, std::string_view gallery, std::vector<std::string>& out)
{
    out.emplace_back(gallery);
    for (const auto* c : v.list(gallery))
        if (c->is_gallery()) walk_all_galleries(v, child_path(gallery, c->name), out);
}

bool is_inside_folder(const Vault& v, std::string_view path)
{
    std::string parent = parent_path(path);
    while (!parent.empty()) {
        if (holds_subgalleries(v, parent)) {
            return true;
        }
        parent = parent_path(parent);
    }
    return false;
}

// The recursive worker: does the actual merge. Never touches progress->total
// (the public entry point below sets that once, up front) and never forwards
// `progress` into transfer_gallery (which would overwrite total with ITS OWN
// subtree's count) — a wholesale sub-gallery move instead bumps `done` by
// that subtree's whole media count in one step.
VaultResult combine_impl(Vault& src, std::string_view src_gallery,
                         Vault& dst, std::string_view dst_gallery,
                         CombineTally& tally, OpProgress* progress)
{
    using enum VaultResult;
    if (would_cycle(src, dst, src_gallery, dst_gallery)) return InvalidArg;

    const IndexNode* src_node = find_child(src, parent_path(src_gallery), last_segment(src_gallery));
    if (!src_node || !src_node->is_gallery()) return NotFound;

    if (!dst_gallery.empty()) {
        const IndexNode* dst_node = find_child(dst, parent_path(dst_gallery), last_segment(dst_gallery));
        if (!dst_node || !dst_node->is_gallery()) return NotFound;
    }

    if (!types_compatible(src, src_gallery, dst, dst_gallery)) return InvalidArg;

    for (const auto& t : src_node->tags) (void)dst.add_tag(dst_gallery, t);

    if (holds_media(src, src_gallery)) {
        std::vector<std::string> names;
        for (const auto* c : src.list(src_gallery)) if (c->is_media()) names.push_back(c->name);

        for (const auto& fname : names) {
            if (progress && progress->cancel.load()) return Ok;
            if (transfer_image(src, src_gallery, fname, dst, dst_gallery, TransferMode::Move) == Ok)
                ++tally.media_moved;
            else
                ++tally.media_skipped;
            if (progress) progress->done.fetch_add(1);
        }
    } else if (holds_subgalleries(src, src_gallery)) {
        std::vector<std::string> names;
        for (const auto* c : src.list(src_gallery)) if (c->is_gallery()) names.push_back(c->name);

        for (const auto& name : names) {
            if (progress && progress->cancel.load()) return Ok;
            const std::string child_src = child_path(src_gallery, name);
            const std::string child_dst = child_path(dst_gallery, name);

            if (find_child(dst, dst_gallery, name)) {
                CombineTally sub;
                if (VaultResult r = combine_impl(src, child_src, dst, child_dst, sub, progress);
                    r != Ok)
                    return r;
                tally.media_moved      += sub.media_moved;
                tally.media_skipped    += sub.media_skipped;
                tally.galleries_merged += sub.galleries_merged + 1;
                tally.galleries_moved  += sub.galleries_moved;
            } else {
                const int subtree_media = count_media(src, child_src);
                if (transfer_gallery(src, child_src, dst, dst_gallery, TransferMode::Move) == Ok) {
                    ++tally.galleries_moved;
                    if (progress) progress->done.fetch_add(subtree_media);   // OpProgress::done is atomic<int>
                }
            }
        }
    }

    if (src.list(src_gallery).empty()) {
        if (VaultResult r = src.remove_gallery(src_gallery); r != Ok) return r;
    }
    return Ok;
}

} // namespace

VaultResult combine_galleries(Vault& src, std::string_view src_gallery,
                              Vault& dst, std::string_view dst_gallery,
                              CombineTally& tally, OpProgress* progress)
{
    using enum VaultResult;
    if (!src.is_unlocked() || !dst.is_unlocked()) return Locked;
    if (src_gallery.empty()) return InvalidArg;

    if (progress) progress->total.store(count_media(src, src_gallery));
    return combine_impl(src, src_gallery, dst, dst_gallery, tally, progress);
}

std::vector<std::string> combine_target_galleries(const Vault& dst, const Vault& src,
                                                   std::string_view src_gallery)
{
    std::vector<std::string> out;
    if (!dst.is_unlocked() || !src.is_unlocked()) return out;

    const bool src_is_leaf = !holds_subgalleries(src, src_gallery);

    std::vector<std::string> all;
    walk_all_galleries(dst, "", all);

    const std::string src_prefix = std::string(src_gallery) + "/";
    for (auto& path : all) {
        if (&dst == &src && (path == src_gallery || path.starts_with(src_prefix))) continue;
        if (!types_compatible(src, src_gallery, dst, path)) continue;
        // If source is a leaf (holds media), exclude any path that's inside a folder
        if (src_is_leaf && is_inside_folder(dst, path)) continue;
        out.push_back(std::move(path));
    }
    return out;
}

} // namespace vault
