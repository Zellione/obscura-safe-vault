#include "vault/transfer.h"

#include <algorithm>

#include "crypto/secure_mem.h"
#include "vault/staging.h"

namespace vault {

void record_failure(TransferTally& t, std::string path, VaultResult code,
                    TransferFailure::Stage stage)
{
    ++t.failed;
    if (t.failures.size() < MAX_TRANSFER_FAILURES)
        t.failures.push_back({std::move(path), code, stage});
}

namespace {

// Shared context for the subtree copy loop (bundled for cpp:S107).
struct CopyCtx {
    Vault&               src;
    Vault&               dst;
    TransferMode         mode;
    crypto::SecureBytes& plain;
    OpProgress*          progress;   // may be null
    TransferTally&       tally;
};

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

// Build destination-side prestaged info from the source node, fetching the
// stored thumbnail/poster blob — Phase 67: no re-decode, no re-probe.
VaultResult prestage_image_info(const Vault& src, const IndexNode& node, StagedThumb& out)
{
    using enum VaultResult;
    out.format   = node.meta.format;
    out.width    = node.meta.width;
    out.height   = node.meta.height;
    out.animated = node.meta.animated;
    out.thumb_jpeg.clear();
    if (node.meta.thumb_length == 0) return Ok;   // source has no thumbnail
    crypto::SecureBytes blob;
    if (VaultResult r = src.read_thumbnail(node, blob); r != Ok) return r;
    out.thumb_jpeg.assign(blob.data(), blob.data() + blob.size());
    return Ok;
}

VaultResult prestage_video_info(const Vault& src, const IndexNode& node, StagedVideoInfo& out)
{
    using enum VaultResult;
    out.container   = node.vmeta.container;
    out.codec       = node.vmeta.codec;
    out.width       = node.vmeta.width;
    out.height      = node.vmeta.height;
    out.duration_us = node.vmeta.duration_us;
    out.poster_jpeg.clear();
    if (node.vmeta.poster_length == 0) return Ok;
    crypto::SecureBytes blob;
    if (VaultResult r = src.read_thumbnail(node, blob); r != Ok) return r;
    out.poster_jpeg.assign(blob.data(), blob.data() + blob.size());
    return Ok;
}

// Phase 65: content from an un-migrated vault carries un-backfilled metadata
// (codec Unknown, animated flag never sniffed). With the lazy repair paths gone,
// nothing would ever fix it if the destination kept claiming to be migrated —
// so the destination inherits the source's lower watermark and re-offers the
// migration at its next unlock.
void lower_dst_watermark(const Vault& src, Vault& dst)
{
    const VaultSettings& s_src = vault_settings(src);
    VaultSettings        s_dst = vault_settings(dst);
    bool lowered = false;
    if (s_src.migrated_index_version < s_dst.migrated_index_version) {
        s_dst.migrated_index_version = s_src.migrated_index_version;
        lowered = true;
    }
    if (s_src.migrated_probe_caps < s_dst.migrated_probe_caps) {
        s_dst.migrated_probe_caps = s_src.migrated_probe_caps;
        lowered = true;
    }
    if (lowered) (void)set_vault_settings(dst, std::move(s_dst));
}

// Copy one media (image or video) `fname` from src/src_gallery into dst/dst_gallery,
// decrypting through the reused mlock'd `plain`. Copy only — source untouched.
// `failed_stage` tracks whether failure occurred on read (source side) or write
// (destination add side).
VaultResult copy_one_media(const Vault& src, std::string_view src_gallery,
                           Vault& dst, std::string_view dst_gallery,
                           std::string_view fname, crypto::SecureBytes& plain,
                           TransferFailure::Stage& failed_stage)
{
    using enum VaultResult;
    failed_stage = TransferFailure::Stage::Read;
    const IndexNode* node = find_image_node(src, src_gallery, fname);
    if (!node) return NotFound;

    // Capture everything needed from `node` BEFORE the destination add: a
    // same-vault add can reallocate the index and dangle the pointer.
    if (node->is_image()) {
        StagedThumb thumb;
        if (VaultResult r = prestage_image_info(src, *node, thumb); r != Ok) return r;
        const uint64_t ts = node->meta.created_ts;
        if (VaultResult r = src.read_image(*node, plain); r != Ok) return r;
        failed_stage = TransferFailure::Stage::Write;
        return add_image_prestaged(dst, dst_gallery, plain.as_span(), fname, thumb, ts);
    }
    if (node->is_video()) {
        StagedVideoInfo info;
        if (VaultResult r = prestage_video_info(src, *node, info); r != Ok) return r;
        const uint64_t ts = node->vmeta.created_ts;
        if (VaultResult r = src.read_video(*node, plain); r != Ok) return r;
        failed_stage = TransferFailure::Stage::Write;
        return add_video_prestaged(dst, dst_gallery, plain.as_span(), fname, info, ts);
    }
    return Ok;
}

// Copy one media via context; records failures and handles per-file Move.
// Never aborts on per-file failures; returns void.
void copy_one_media_ex(CopyCtx& c, std::string_view src_abs, std::string_view dst_gallery,
                       std::string_view fname)
{
    using enum VaultResult;
    auto stage = TransferFailure::Stage::Read;
    if (VaultResult r = copy_one_media(c.src, src_abs, c.dst, dst_gallery, fname, c.plain, stage);
        r != Ok) {
        record_failure(c.tally, child_path(src_abs, fname), r, stage);
    } else if (c.mode == TransferMode::Move) {
        // Per-file Move (Phase 67): the destination add committed, so remove
        // the source item now. A failed remove leaves a recoverable
        // duplicate — reported, not fatal, and not double-counted as done.
        if (VaultResult r2 = c.src.remove_image(src_abs, fname); r2 != Ok)
            record_failure(c.tally, child_path(src_abs, fname), r2, TransferFailure::Stage::Write);
        else
            ++c.tally.done;
    } else {
        ++c.tally.done;
    }
    if (c.progress) c.progress->done.fetch_add(1);
}

// Copy every media named in `images` from `src_abs` into `dst_gallery`.
// Never aborts on per-file failures; records each and continues.
void copy_images(CopyCtx& c, const std::string& src_abs, const std::string& dst_gallery,
                 const std::vector<std::string>& images)
{
    for (const auto& fname : images) {
        if (c.progress && c.progress->cancel.load()) return;
        copy_one_media_ex(c, src_abs, dst_gallery, fname);
    }
}

// Recreate the snapshotted subtree under `dest_root` in `dst` and copy each gallery's
// media (parent-before-child order). Collects failed sub-branch rel-prefixes for the
// pruner. Never aborts on per-file failures; they are recorded in c.tally. snaps are
// depth-first, parent-before-child, so a failed gallery's descendants form a CONTIGUOUS
// run — the `skip_prefix` scan relies on this.
void copy_subtree(CopyCtx& c, std::string_view src_gallery, const std::string& dest_root,
                  const std::vector<GallerySnap>& snaps,
                  std::vector<std::string>& failed_rels)
{
    using enum VaultResult;
    std::string skip_prefix;
    bool skipping = false;
    for (const auto& snap : snaps) {
        if (c.progress && c.progress->cancel.load()) return;
        if (skipping &&
            (snap.rel == skip_prefix || snap.rel.starts_with(skip_prefix + "/"))) {
            // Unreachable branch: media counted failed (no per-file entries —
            // the gallery's own entry covers the branch).
            c.tally.failed += static_cast<int>(snap.images.size());
            if (c.progress)
                c.progress->done.fetch_add(static_cast<int>(snap.images.size()));
            continue;
        }
        skipping = false;

        const std::string dst_gallery = snap.rel.empty() ? dest_root
                                                         : dest_root + "/" + snap.rel;
        const std::string src_abs = snap.rel.empty()
                                        ? std::string(src_gallery)
                                        : std::string(src_gallery) + "/" + snap.rel;
        if (VaultResult r = c.dst.create_gallery(dst_gallery); r != Ok && r != AlreadyExists) {
            record_failure(c.tally, src_abs, r, TransferFailure::Stage::Write);
            c.tally.failed += static_cast<int>(snap.images.size());
            if (c.progress)
                c.progress->done.fetch_add(static_cast<int>(snap.images.size()));
            failed_rels.push_back(snap.rel);
            if (snap.rel.empty()) return;   // subtree ROOT failed: nothing is reachable
            skip_prefix = snap.rel;
            skipping = true;
            continue;
        }
        copy_images(c, src_abs, dst_gallery, snap.images);
    }
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

// Prune empty galleries bottom-up (reverse snapshot order = children before parents),
// skipping failed branches so an empty gallery inside a never-copied branch is not lost.
void prune_moved_galleries(Vault& src, std::string_view src_gallery,
                           const std::vector<GallerySnap>& snaps,
                           const std::vector<std::string>& failed_rels)
{
    auto in_failed_branch = [&](const std::string& rel) {
        for (const auto& p : failed_rels)
            if (rel == p || rel.starts_with(p + "/")) return true;
        return false;
    };
    for (auto it = snaps.rbegin(); it != snaps.rend(); ++it) {
        if (in_failed_branch(it->rel)) continue;
        const std::string abs = it->rel.empty()
                                    ? std::string(src_gallery)
                                    : std::string(src_gallery) + "/" + it->rel;
        if (src.resolve_node(abs) != nullptr && src.list(abs).empty())
            (void)src.remove_gallery(abs);
    }
}

} // namespace

// Internal transfer_image with stage tracking for per-file failure recording.
// Sets `stage_out` to indicate where failure occurred (Read side or Write side).
VaultResult transfer_image_ex(Vault& src, std::string_view src_gallery,
                              std::string_view filename,
                              Vault& dst, std::string_view dst_gallery,
                              TransferMode mode, TransferFailure::Stage& stage_out)
{
    using enum VaultResult;

    crypto::SecureBytes plain;
    if (VaultResult r = copy_one_media(src, src_gallery, dst, dst_gallery, filename, plain, stage_out);
        r != Ok)
        return r;

    lower_dst_watermark(src, dst);

    if (mode == TransferMode::Move) {
        if (VaultResult r = src.remove_image(src_gallery, filename); r != Ok) {
            stage_out = TransferFailure::Stage::Write;
            return r;
        }
    }
    return Ok;
}

VaultResult transfer_image(Vault& src, std::string_view src_gallery,
                           std::string_view filename,
                           Vault& dst, std::string_view dst_gallery,
                           TransferMode mode)
{
    TransferFailure::Stage stage = TransferFailure::Stage::Read;
    return transfer_image_ex(src, src_gallery, filename, dst, dst_gallery, mode, stage);
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
                             TransferMode mode, OpProgress* progress,
                             TransferTally* tally)
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

    TransferTally local;
    TransferTally& t = tally ? *tally : local;

    crypto::SecureBytes plain;
    CopyCtx ctx{src, dst, mode, plain, progress, t};
    std::vector<std::string> failed_rels;
    copy_subtree(ctx, src_gallery, dest_root, snaps, failed_rels);

    // The subtree root itself could not be created: structural failure, source
    // untouched (per-file Move never ran — copy_one_media was never reached).
    if (!failed_rels.empty() && failed_rels.front().empty())
        return t.failures.empty() ? IoError : t.failures.front().code;

    lower_dst_watermark(src, dst);

    // A cancel stops cleanly between files: items moved so far live only in the
    // destination (their source copies are already removed per-file), the rest
    // only in the source — no duplicates, nothing lost. Skip pruning: galleries
    // not yet recreated at the destination must survive.
    if (progress && progress->cancel.load()) return Ok;

    if (mode == TransferMode::Move) prune_moved_galleries(src, src_gallery, snaps, failed_rels);
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
        auto stage = TransferFailure::Stage::Read;
        if (VaultResult r = transfer_image_ex(src, src_gallery, fname, dst, dst_gallery,
                                              mode, stage);
            r == Ok)
            ++tally.done;
        else
            record_failure(tally, child_path(src_gallery, fname), r, stage);
        if (progress) progress->done.fetch_add(1);
    }
    return tally;
}

TransferTally transfer_galleries(Vault& src, const std::vector<std::string>& src_paths,
                                 Vault& dst, std::string_view dst_parent,
                                 TransferMode mode, OpProgress* progress)
{
    using enum VaultResult;
    if (progress) progress->total.store(static_cast<int>(src_paths.size()));

    TransferTally out;
    for (const auto& path : src_paths) {
        if (progress && progress->cancel.load()) break;
        TransferTally sub;
        if (transfer_gallery(src, path, dst, dst_parent, mode, nullptr, &sub) == Ok) {
            ++out.done;
            // Merge per-file failures from this subtree into the output.
            for (auto& f : sub.failures) {
                if (out.failures.size() < MAX_TRANSFER_FAILURES)
                    out.failures.push_back(std::move(f));
            }
            // per-file failures inside a structurally-Ok subtree do not change the
            // SUBTREE done/failed counts, but the entries surface in the dialog
        } else {
            ++out.failed;
            // Structural failure: record as a gallery-level entry.
            if (out.failures.size() < MAX_TRANSFER_FAILURES)
                out.failures.push_back({std::string(path), VaultResult::Ok,
                                       TransferFailure::Stage::Write});
        }
        if (progress) progress->done.fetch_add(1);
    }
    return out;
}

} // namespace vault
