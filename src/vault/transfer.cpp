#include "vault/transfer.h"

#include <algorithm>

#include "crypto/secure_mem.h"

namespace vault {

namespace {

// Append `gallery`'s slash-path child name; "" stays "".
std::string child_path(std::string_view gallery, std::string_view name)
{
    std::string p(gallery);
    if (!p.empty()) p += '/';
    p.append(name);
    return p;
}

// Every gallery in the subtree (self + all descendants) — since Phase 46,
// any gallery can accept either media or a sub-gallery.
void collect_all_galleries(const Vault& v, std::string_view gallery,
                           std::vector<std::string>& out)
{
    out.emplace_back(gallery);
    for (const auto* c : v.list(gallery)) {
        if (c->is_gallery()) {
            collect_all_galleries(v, child_path(gallery, c->name), out);
        }
    }
}

// One snapshotted gallery: its path relative to the moved subtree root ("" = the
// moved gallery itself) and the media (image/video) filenames it directly holds.
struct GallerySnap {
    std::string              rel;
    std::vector<std::string> images;  // names of media children (images and videos)
};

// Walk `abs` (a gallery in `src`) parent-before-child, recording each gallery's
// relative path + its media filenames. `rel` is the path relative to the subtree root.
void snapshot_subtree(const Vault& src, std::string_view abs, const std::string& rel,
                      std::vector<GallerySnap>& out)
{
    GallerySnap snap;
    snap.rel = rel;
    std::vector<std::string> subgalleries;
    for (const auto* c : src.list(abs)) {
        if (c->is_media())   snap.images.push_back(c->name);
        else                 subgalleries.push_back(c->name);
    }
    out.push_back(std::move(snap));
    for (const auto& name : subgalleries) {
        const std::string child_rel = rel.empty() ? name : rel + "/" + name;
        snapshot_subtree(src, child_path(abs, name), child_rel, out);
    }
}

// The slash-path of `gallery`'s parent ("" if `gallery` has no slash).
std::string parent_path(std::string_view gallery)
{
    const auto slash = gallery.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string(gallery.substr(0, slash));
}

// The final path segment (the gallery's own name).
std::string last_segment(std::string_view gallery)
{
    const auto slash = gallery.rfind('/');
    return slash == std::string_view::npos ? std::string(gallery)
                                           : std::string(gallery.substr(slash + 1));
}


// Locate a media node (image or video) by name in `gallery` (nullptr if absent).
const IndexNode* find_image_node(const Vault& v, std::string_view gallery,
                                 std::string_view name)
{
    for (const auto* c : v.list(gallery))
        if (c->is_media() && c->name == name) return c;
    return nullptr;
}

// Copy one media (image or video) `fname` from src/src_gallery into dst/dst_gallery,
// decrypting through the reused mlock'd `plain`. Copy only — source untouched.
VaultResult copy_one_media(const Vault& src, std::string_view src_gallery,
                           Vault& dst, std::string_view dst_gallery,
                           std::string_view fname, crypto::SecureBytes& plain)
{
    using enum VaultResult;
    const IndexNode* node = find_image_node(src, src_gallery, fname);
    if (!node) return NotFound;

    // Branch on media type: image → read_image/add_image, video → read_video/add_video.
    if (node->is_image()) {
        if (VaultResult r = src.read_image(*node, plain); r != Ok) return r;
        return dst.add_image(dst_gallery, plain.as_span(), fname);
    }
    if (node->is_video()) {
        if (VaultResult r = src.read_video(*node, plain); r != Ok) return r;
        return dst.add_video(dst_gallery, plain.as_span(), fname);
    }
    return Ok;
}

// Copy every media named in `images` from `src`/`src_gallery` into `dst`/`dst_gallery`.
// `progress` (optional) is bumped per file; a set cancel flag stops the copy between
// files (returning Ok) — the caller detects the cancel via progress->cancel and leaves
// the source intact.
VaultResult copy_images(const Vault& src, std::string_view src_gallery,
                        Vault& dst, std::string_view dst_gallery,
                        const std::vector<std::string>& images, crypto::SecureBytes& plain,
                        OpProgress* progress)
{
    using enum VaultResult;
    for (const auto& fname : images) {
        if (progress && progress->cancel.load()) return Ok;   // clean partial: stop between files
        if (VaultResult r = copy_one_media(src, src_gallery, dst, dst_gallery, fname, plain);
            r != Ok)
            return r;
        if (progress) progress->done.fetch_add(1);
    }
    return Ok;
}

// Recreate the snapshotted subtree under `dest_root` in `dst` and copy each gallery's
// media (parent-before-child order). Stops early (returning Ok) on cancel; the caller
// checks progress->cancel to decide whether to remove the source. Returns the first error.
VaultResult copy_subtree(const Vault& src, std::string_view src_gallery, Vault& dst,
                         const std::string& dest_root, const std::vector<GallerySnap>& snaps,
                         OpProgress* progress)
{
    using enum VaultResult;
    crypto::SecureBytes plain;
    for (const auto& snap : snaps) {
        const std::string dst_gallery = snap.rel.empty() ? dest_root
                                                         : dest_root + "/" + snap.rel;
        const std::string src_abs = snap.rel.empty() ? std::string(src_gallery)
                                                      : std::string(src_gallery) + "/" + snap.rel;

        if (VaultResult r = dst.create_gallery(dst_gallery); r != Ok && r != AlreadyExists)
            return r;
        if (VaultResult r = copy_images(src, src_abs, dst, dst_gallery, snap.images, plain, progress);
            r != Ok)
            return r;
        if (progress && progress->cancel.load()) return Ok;   // cancelled between galleries
    }
    return Ok;
}

// A same-vault transfer forms a cycle when the destination parent is the moved
// gallery itself or any descendant of it (moving a subtree into itself). Always
// false across two different vaults.
bool is_same_vault_cycle(const Vault& src, const Vault& dst,
                         std::string_view src_gallery, std::string_view dst_parent)
{
    if (&src != &dst) return false;
    if (dst_parent == src_gallery) return true;
    return dst_parent.starts_with(std::string(src_gallery) + "/");
}

} // namespace

VaultResult transfer_image(Vault& src, std::string_view src_gallery,
                           std::string_view filename,
                           Vault& dst, std::string_view dst_gallery,
                           TransferMode mode)
{
    using enum VaultResult;

    // Locate the source media (image or video) node.
    const IndexNode* node = find_image_node(src, src_gallery, filename);
    if (!node) return NotFound;

    // Decrypt into mlock'd memory, then re-encrypt into the destination.
    crypto::SecureBytes plain;

    // Branch on media type: image → read_image/add_image, video → read_video/add_video
    if (node->is_image()) {
        if (VaultResult r = src.read_image(*node, plain); r != Ok) return r;
    } else if (node->is_video()) {
        if (VaultResult r = src.read_video(*node, plain); r != Ok) return r;
    }

    // dst commits first (crash-safe: a crash before the source remove leaves a
    // recoverable duplicate, never a loss). A failed add leaves the source intact.
    // `node` is not used past this point, so a same-vault add that reallocates the
    // index cannot dangle it.
    if (node->is_image()) {
        if (VaultResult r = dst.add_image(dst_gallery, plain.as_span(), filename); r != Ok)
            return r;
    } else if (node->is_video()) {
        if (VaultResult r = dst.add_video(dst_gallery, plain.as_span(), filename); r != Ok)
            return r;
    }

    if (mode == TransferMode::Move) return src.remove_image(src_gallery, filename);
    return Ok;
}

std::vector<std::string> image_target_galleries(const Vault& v)
{
    std::vector<std::string> out;
    if (!v.is_unlocked()) return out;
    collect_all_galleries(v, "", out);
    return out;
}

std::vector<std::string> gallery_target_parents(const Vault& v)
{
    std::vector<std::string> out;
    if (!v.is_unlocked()) return out;
    collect_all_galleries(v, "", out);
    return out;
}

VaultResult transfer_gallery(Vault& src, std::string_view src_gallery,
                             Vault& dst, std::string_view dst_parent,
                             TransferMode mode, OpProgress* progress)
{
    using enum VaultResult;

    if (src_gallery.empty()) return InvalidArg;       // can't transfer the root itself
    // Refuse a same-vault move/copy of a gallery into itself or a descendant.
    if (is_same_vault_cycle(src, dst, src_gallery, dst_parent)) return InvalidArg;
    const std::string name = last_segment(src_gallery);

    // Source must be an existing gallery.
    bool src_is_gallery = false;
    for (const auto* c : src.list(parent_path(src_gallery)))
        if (c->is_gallery() && c->name == name) { src_is_gallery = true; break; }
    if (!src_is_gallery) return NotFound;

    const std::string dest_root = dst_parent.empty() ? name
                                                      : std::string(dst_parent) + "/" + name;

    // Validate destination up front so collisions/ineligibility leave nothing partial.
    for (const auto* c : dst.list(dst_parent))
        if (c->name == name) return AlreadyExists;

    // Snapshot the source subtree (parent-before-child), then recreate + copy.
    std::vector<GallerySnap> snaps;
    snapshot_subtree(src, src_gallery, "", snaps);

    // Total media across the subtree drives the progress bar ("N / M files").
    if (progress) {
        int media = 0;
        for (const auto& snap : snaps) media += static_cast<int>(snap.images.size());
        progress->total.store(media);
    }

    if (VaultResult r = copy_subtree(src, src_gallery, dst, dest_root, snaps, progress); r != Ok)
        return r;

    // A cancel leaves the source intact even for Move — the partial copy in dst is a
    // recoverable duplicate, never a loss.
    if (progress && progress->cancel.load()) return Ok;

    // Everything copied into dst — for a Move, drop the source subtree (copy-then-delete).
    if (mode == TransferMode::Move) return src.remove_gallery(src_gallery);
    return Ok;
}

TransferTally transfer_images(Vault& src, std::string_view src_gallery,
                              const std::vector<std::string>& filenames,
                              Vault& dst, std::string_view dst_gallery,
                              TransferMode mode, OpProgress* progress)
{
    using enum VaultResult;
    if (progress) progress->total.store(static_cast<int>(filenames.size()));

    TransferTally tally;
    for (const auto& fname : filenames) {
        if (progress && progress->cancel.load()) break;   // clean partial: stop between files
        if (transfer_image(src, src_gallery, fname, dst, dst_gallery, mode) == Ok) ++tally.done;
        else                                                                       ++tally.failed;
        if (progress) progress->done.fetch_add(1);
    }
    return tally;
}

TransferTally transfer_galleries(Vault& src, const std::vector<std::string>& src_paths,
                                 Vault& dst, std::string_view dst_parent,
                                 TransferMode mode, OpProgress* progress)
{
    if (progress) progress->total.store(static_cast<int>(src_paths.size()));

    TransferTally tally;
    for (const auto& path : src_paths) {
        if (progress && progress->cancel.load()) break;
        if (transfer_gallery(src, path, dst, dst_parent, mode) == VaultResult::Ok) ++tally.done;
        else                                                                       ++tally.failed;
        if (progress) progress->done.fetch_add(1);
    }
    return tally;
}

} // namespace vault
