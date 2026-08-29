#include "vault/transfer.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <ranges>

#include "crypto/secure_mem.h"
#include "vault/combine.h"
#include "vault/index.h"
#include "vault/safe_name.h"
#include "vault/staging.h"

namespace vault {

void record_failure(TransferTally& t, std::string path, VaultResult code,
                    TransferFailure::Stage stage)
{
    ++t.failed;
    if (t.failures.size() < MAX_TRANSFER_FAILURES)
        t.failures.emplace_back(std::move(path), code, stage);
}

std::vector<std::string> effective_tags(const Vault& v, std::string_view node_path)
{
    const IndexNode* node = v.resolve_node(node_path);
    if (!node) return {};
    std::vector<std::string> out;
    for (const auto& t : node->tags)
        out.emplace_back(t.view());
    auto add_ci = [&out](const std::vector<crypto::SecureString>& tags) {
        for (const auto& t : tags)
            if (!std::ranges::any_of(out, [&](const auto& o) { return tag_ci_equal(o, t.view()); }))
                out.emplace_back(t.view());
    };
    // Root tags cascade globally; then each ancestor gallery down to the parent.
    if (const IndexNode* root = v.resolve_node(""); root) add_ci(root->tags);
    std::string prefix;
    for (std::string_view rest = node_path;;) {
        const auto slash = rest.find('/');
        if (slash == std::string_view::npos) break;     // last segment = the node itself
        prefix.append(prefix.empty() ? "" : "/").append(rest.substr(0, slash));
        if (const IndexNode* g = v.resolve_node(prefix); g && g->is_gallery()) add_ci(g->tags);
        rest.remove_prefix(slash + 1);
    }
    return out;
}

namespace {

// Phase 69: destination commits are batched — one durable commit_staged per
// TRANSFER_COMMIT_BATCH files (mirrors the import queue's CommitLane batch
// size) instead of one per file, and Move-mode source removals are deferred
// into one remove_media_batch after the destination is durable.
inline constexpr size_t TRANSFER_COMMIT_BATCH = 32;

// Shared context for the subtree copy loop (bundled for cpp:S107).
struct CopyCtx {
    Vault&               src;
    Vault&               dst;
    TransferMode         mode;
    crypto::SecureBytes& plain;
    OpProgress*          progress;   // may be null
    TransferTally&       tally;
    // Batch state (Phase 69). `pending` holds full source slash-paths of files
    // attached at the destination but not yet durably committed; `moved` holds
    // committed paths awaiting the deferred Move removal. `dirty` tracks
    // uncommitted destination-tree mutations (attaches AND ensured galleries).
    std::vector<std::string> pending{};
    std::vector<std::string> moved{};
    bool                     dirty         = false;
    bool                     commit_failed = false;
};

// Result of collision resolution (bundled for cpp:S107).
struct CollisionResolution {
    VaultResult code;           // Ok, AlreadyExists, or a combine result
    std::string dest_name;      // Name to use at destination (rewritten by Suffix)
    bool        handled;        // True if combine_galleries ran; caller returns immediately
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
            collect_all_galleries(v, child_path(gallery, c->name.view()), out);
        }
    }
}

// One snapshotted gallery: its path relative to the moved subtree root ("" = the
// moved gallery itself) and the media (image/video) filenames it directly holds.
struct GallerySnap {
    std::string              rel;
    std::vector<std::string> images;   // names of media children (images and videos)
    std::vector<std::string> tags;     // the gallery's OWN tags (root snap: effective tags — set by the driver)
    bool                     favorite = false;
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
        if (c->is_media())
            snap.images.emplace_back(c->name.view());
        else
            subgalleries.emplace_back(c->name.view());
    }
    // Capture the gallery's own tags and favorite at snapshot time
    if (const IndexNode* self = src.resolve_node(abs); self && self->is_gallery()) {
        snap.tags.clear();
        for (const auto& t : self->tags)
            snap.tags.emplace_back(t.view());
        snap.favorite = self->favorite;
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

// Longest prefix of `s` that fits max_bytes without splitting a UTF-8 codepoint.
std::string trim_utf8(std::string_view s, size_t max_bytes)
{
    if (s.size() <= max_bytes) return std::string(s);
    size_t end = max_bytes;
    while (end > 0 && (static_cast<std::byte>(s[end]) & std::byte{0xC0}) == std::byte{0x80}) --end;
    return std::string(s.substr(0, end));
}

// First free "name_2", "name_3", ... among dst_parent's children, trimmed to
// MAX_NODE_NAME_BYTES on a codepoint boundary. The candidate always ends in
// ASCII digits, so is_safe_node_name properties (no trailing dot/space) hold.
// nullopt after _9999 — a bound, not a scenario.
std::optional<std::string> unique_child_name(const Vault& dst, std::string_view dst_parent,
                                             std::string_view name)
{
    auto taken = [&](std::string_view n) {
        return std::ranges::contains(dst.list(dst_parent), n,
                                     [](const IndexNode* c) { return c->name.view(); });
    };
    for (int i = 2; i <= 9999; ++i) {
        const std::string suffix = std::format("_{}", i);
        const std::string cand =
            trim_utf8(name, MAX_NODE_NAME_BYTES - suffix.size()) + suffix;
        if (!taken(cand)) return cand;
    }
    return std::nullopt;
}

// Resolve a same-named child at dst_parent per `policy`: Fail/combine-into-media
// -> AlreadyExists; Suffix -> rewrites dest_name; Combine (gallery) -> runs the
// merge and reports via handled. Returns collision resolution with dest_name and
// handled flag; if handled=true, combine_galleries ran and caller returns immediately.
// Semantics preserved: dispatch before the progress->total store; combine tally
// mapping media_moved→done, media_skipped→skipped. S107: returns struct instead of
// output params to fit within 7-parameter cap.
CollisionResolution resolve_dest_collision(Vault& src, std::string_view src_gallery, Vault& dst,
                                           std::string_view dst_parent, TransferMode mode,
                                           const GalleryTransferOpts& opts, std::string_view name)
{
    using enum VaultResult;
    for (const auto* c : dst.list(dst_parent)) {
        if (c->name != name) continue;
        if (opts.policy == CollisionPolicy::Suffix) {
            auto fresh = unique_child_name(dst, dst_parent, name);
            if (!fresh) return {AlreadyExists, {}, false};   // probe bound exhausted
            return {Ok, std::move(*fresh), false};
        }
        if (opts.policy == CollisionPolicy::Combine && c->is_gallery()) {
            OpProgress* progress = opts.progress;
            TransferTally* tally = opts.tally;
            CombineTally ct;
            const VaultResult r = combine_galleries(
                src, src_gallery, dst, child_path(dst_parent, name), ct, progress, mode);
            if (tally) {
                tally->done    += ct.media_moved;
                tally->skipped += ct.media_skipped;
            }
            return {r, {}, true};
        }
        return {AlreadyExists, {}, false};   // Fail policy, or Combine into a non-gallery child
    }
    return {Ok, {}, false};   // no collision
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
    // Phase 96 (OSV-AUD-003): move straight from the decrypted SecureBytes into
    // the destination's SecureBytes — never through an ordinary vector copy.
    if (!out.thumb_jpeg.assign(blob.as_span())) return IoError;
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
    if (!out.poster_jpeg.assign(blob.as_span())) return IoError;
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
    if (s_src.migrated_thumb_side < s_dst.migrated_thumb_side) {
        s_dst.migrated_thumb_side = s_src.migrated_thumb_side;
        lowered = true;
    }
    if (lowered) (void)set_vault_settings(dst, std::move(s_dst));
}

// Make the current batch durable in ONE commit. On success the batch's files
// count done (Copy) or queue for the deferred source removal (Move). On
// failure NONE of them is durable: each is recorded failed and the transfer
// hard-stops (commit_failed) — already-committed batches are unaffected.
void flush_dst(CopyCtx& c)
{
    using enum VaultResult;
    if (!c.dirty) return;
    if (const VaultResult r = commit_staged(c.dst); r != Ok) {
        for (auto& p : c.pending)
            record_failure(c.tally, std::move(p), r, TransferFailure::Stage::Write);
        c.pending.clear();
        c.commit_failed = true;
        return;
    }
    c.dirty = false;
    if (c.mode == TransferMode::Move) {
        c.moved.insert(c.moved.end(), std::make_move_iterator(c.pending.begin()),
                       std::make_move_iterator(c.pending.end()));
    } else {
        c.tally.done += static_cast<int>(c.pending.size());
    }
    c.pending.clear();
}

// Record a successful copy (attach only — not yet durable) and flush the batch
// when it reaches TRANSFER_COMMIT_BATCH files.
void note_copied(CopyCtx& c, std::string src_path)
{
    c.pending.push_back(std::move(src_path));
    c.dirty = true;
    if (c.pending.size() >= TRANSFER_COMMIT_BATCH) flush_dst(c);
}

// Post-copy epilogue (runs on completion AND on cancel): flush the last batch,
// persist the migration watermark, then apply the deferred Move removals in
// ONE remove_media_batch — a single source commit + a single auto-reclaim pass
// instead of one of each per file. Destination durability strictly precedes
// any source mutation, so a crash anywhere leaves recoverable duplicates,
// never a lost file.
void finish_copies(CopyCtx& c)
{
    using enum VaultResult;
    flush_dst(c);
    lower_dst_watermark(c.src, c.dst);
    if (c.mode != TransferMode::Move || c.moved.empty()) return;
    if (const VaultResult r = remove_media_batch(c.src, c.moved, nullptr); r != Ok) {
        // The copies are durable in dst; a failed remove leaves recoverable
        // duplicates — reported per file, not fatal (Phase 67 contract).
        for (auto& p : c.moved)
            record_failure(c.tally, std::move(p), r, TransferFailure::Stage::Write);
    } else {
        c.tally.done += static_cast<int>(c.moved.size());
    }
    c.moved.clear();
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
    const std::string src_path = child_path(src_gallery, fname);
    const auto eff_tags = effective_tags(src, src_path);
    const bool favorite = node->favorite;
    const NodeExtras extras{.tags = eff_tags, .favorite = favorite};

    if (node->is_image()) {
        StagedThumb thumb;
        if (VaultResult r = prestage_image_info(src, *node, thumb); r != Ok) return r;
        const uint64_t ts = node->meta.created_ts;
        if (VaultResult r = src.read_image(*node, plain); r != Ok) return r;
        failed_stage = TransferFailure::Stage::Write;
        return attach_image_prestaged(dst, dst_gallery, plain.as_span(), fname, thumb, ts, &extras);
    }
    if (node->is_video()) {
        StagedVideoInfo info;
        if (VaultResult r = prestage_video_info(src, *node, info); r != Ok) return r;
        const uint64_t ts = node->vmeta.created_ts;
        if (VaultResult r = src.read_video(*node, plain); r != Ok) return r;
        failed_stage = TransferFailure::Stage::Write;
        return attach_video_prestaged(dst, dst_gallery, plain.as_span(), fname, info, ts, &extras);
    }
    return Ok;
}

// Copy one media via context; records failures. A success is batched
// (Phase 69): the file counts done — and, for Move, leaves the source — only
// after its batch's destination commit lands (flush_dst / finish_copies).
// Never aborts on per-file failures; returns void.
void copy_one_media_ex(CopyCtx& c, std::string_view src_abs, std::string_view dst_gallery,
                       std::string_view fname)
{
    using enum VaultResult;
    auto stage = TransferFailure::Stage::Read;
    if (VaultResult r = copy_one_media(c.src, src_abs, c.dst, dst_gallery, fname, c.plain, stage);
        r == AlreadyExists && stage == TransferFailure::Stage::Write) {
        ++c.tally.skipped;   // same-named file at dst: skip, keep the source copy
    } else if (r != Ok) {
        record_failure(c.tally, child_path(src_abs, fname), r, stage);
    } else {
        note_copied(c, child_path(src_abs, fname));
    }
    if (c.progress) c.progress->done.fetch_add(1);
}

// Copy every media named in `images` from `src_abs` into `dst_gallery`.
// Never aborts on per-file failures; records each and continues. A failed
// batch commit (commit_failed) is the one hard stop.
void copy_images(CopyCtx& c, const std::string& src_abs, const std::string& dst_gallery,
                 const std::vector<std::string>& images)
{
    for (const auto& fname : images) {
        if (c.progress && c.progress->cancel.load()) return;
        if (c.commit_failed) return;
        copy_one_media_ex(c, src_abs, dst_gallery, fname);
    }
}

// Union `tags` onto the gallery at `dst_path` (ci-deduped, existing casing kept)
// and OR the favorite flag. Called after ensure_gallery_path so a pre-existing
// destination gallery keeps its tags and a re-run stays idempotent. Tree-only
// mutation — rides the batch commit.
void apply_gallery_extras(Vault& dst, std::string_view dst_path,
                          const std::vector<std::string>& tags, bool favorite)
{
    IndexNode* g = dst.resolve_node(dst_path);
    if (!g || !g->is_gallery()) return;
    for (const auto& t : tags) {
        if (g->tags.size() >= INDEX_MAX_TAGS) break;
        if (!std::ranges::any_of(g->tags, [&](const auto& o) { return tag_ci_equal(o.view(), t); }))
            g->tags.emplace_back(t);
    }
    if (favorite) g->favorite = true;
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
        if (c.commit_failed) return;
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
        // Phase 69: ensure_gallery_path (idempotent, NO per-gallery commit) —
        // the created galleries ride the next batched commit.
        if (VaultResult r = ensure_gallery_path(c.dst, dst_gallery); r != Ok) {
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
        c.dirty = true;   // ensured galleries need the next commit too
        apply_gallery_extras(c.dst, dst_gallery, snap.tags, snap.favorite);
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

// Merge per-file failures from a sub-tally into the output, respecting the cap.
void merge_failures(TransferTally& out, TransferTally&& sub)
{
    for (auto& f : sub.failures) {
        if (out.failures.size() < MAX_TRANSFER_FAILURES)
            out.failures.push_back(std::move(f));
    }
}

// Prune empty galleries bottom-up (reverse snapshot order = children before parents),
// skipping failed branches so an empty gallery inside a never-copied branch is not lost.
void prune_moved_galleries(Vault& src, std::string_view src_gallery,
                           const std::vector<GallerySnap>& snaps,
                           const std::vector<std::string>& failed_rels)
{
    auto in_failed_branch = [&](std::string_view rel) {
        return std::ranges::any_of(failed_rels, [rel](std::string_view p) {
            return rel == p || rel.starts_with(std::string(p) + "/");
        });
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

std::vector<std::string> all_galleries(const Vault& v)
{
    std::vector<std::string> out;
    // Walk from root, excluding the root "" itself
    for (const auto* c : v.list(""))
        if (c->is_gallery()) collect_all_galleries(v, c->name.view(), out);
    return out;
}

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

    // Single-file path: one commit right away (batch loops use CopyCtx instead).
    stage_out = TransferFailure::Stage::Write;
    if (VaultResult r = commit_staged(dst); r != Ok) return r;

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
                             TransferMode mode, GalleryTransferOpts opts)
{
    using enum VaultResult;

    OpProgress* progress = opts.progress;
    TransferTally* tally = opts.tally;

    if (src_gallery.empty()) return InvalidArg;       // can't transfer the root itself
    // Refuse a same-vault move/copy of a gallery into itself or a descendant.
    if (is_same_vault_cycle(src, dst, src_gallery, dst_parent)) return InvalidArg;
    const std::string name = last_segment(src_gallery);

    // Source must be an existing gallery.
    bool src_is_gallery = false;
    for (const auto* c : src.list(parent_path(src_gallery)))
        if (c->is_gallery() && c->name == name) { src_is_gallery = true; break; }
    if (!src_is_gallery) return NotFound;

    std::string dest_name = name;
    const CollisionResolution collision = resolve_dest_collision(
        src, src_gallery, dst, dst_parent, mode, opts, name);
    if (collision.handled) return collision.code;
    if (collision.code != Ok) return collision.code;
    if (!collision.dest_name.empty()) dest_name = collision.dest_name;
    const std::string dest_root = dst_parent.empty() ? dest_name
                                                     : std::string(dst_parent) + "/" + dest_name;

    // Snapshot the source subtree (parent-before-child), then recreate + copy.
    std::vector<GallerySnap> snaps;
    snapshot_subtree(src, src_gallery, "", snaps);

    // Materialize the ROOT snap's tags with effective tags (ancestors cascade in).
    if (!snaps.empty()) snaps[0].tags = effective_tags(src, src_gallery);

    // Total media across the subtree drives the progress bar ("N / M files").
    if (progress) {
        int media = 0;
        for (const auto& snap : snaps) media += static_cast<int>(snap.images.size());
        progress->total.store(media);
    }

    TransferTally local;
    TransferTally& t = tally ? *tally : local;

    crypto::SecureBytes plain;
    CopyCtx ctx{.src = src, .dst = dst, .mode = mode, .plain = plain,
                .progress = progress, .tally = t};
    std::vector<std::string> failed_rels;
    copy_subtree(ctx, src_gallery, dest_root, snaps, failed_rels);

    // The subtree root itself could not be created: structural failure, source
    // untouched (nothing was copied — copy_one_media was never reached).
    if (!failed_rels.empty() && failed_rels.front().empty())
        return t.failures.empty() ? IoError : t.failures.front().code;

    // Flush the last batch, persist the watermark, apply deferred Move
    // removals (one source commit). Runs on cancel too, so a cancel still
    // leaves items moved so far only in the destination — no duplicates,
    // nothing lost.
    finish_copies(ctx);

    // Skip pruning on cancel: galleries not yet recreated at the destination
    // must survive.
    if (progress && progress->cancel.load()) return Ok;

    if (mode == TransferMode::Move) prune_moved_galleries(src, src_gallery, snaps, failed_rels);
    return Ok;
}

TransferTally transfer_images(Vault& src, std::string_view src_gallery,
                              const std::vector<std::string>& filenames,
                              Vault& dst, std::string_view dst_gallery,
                              TransferMode mode, TransferProgress prog)
{
    using enum VaultResult;
    OpProgress* progress = prog.progress;
    if (progress && prog.set_total)
        progress->total.store(static_cast<int>(filenames.size()));

    TransferTally tally;
    crypto::SecureBytes plain;
    CopyCtx ctx{.src = src, .dst = dst, .mode = mode, .plain = plain,
                .progress = progress, .tally = tally};
    for (const auto& fname : filenames) {
        // Stop between files on a user cancel (clean partial) or a destination
        // commit failure (hard stop).
        if ((progress && progress->cancel.load()) || ctx.commit_failed) break;
        copy_one_media_ex(ctx, src_gallery, dst_gallery, fname);
    }
    finish_copies(ctx);   // one dst commit + one deferred source removal batch
    return tally;
}

TransferTally transfer_galleries(Vault& src, const std::vector<std::string>& src_paths,
                                 Vault& dst, std::string_view dst_parent,
                                 TransferMode mode, OpProgress* progress,
                                 CollisionPolicy policy)
{
    using enum VaultResult;
    if (progress) progress->total.store(static_cast<int>(src_paths.size()));

    TransferTally out;
    for (const auto& path : src_paths) {
        if (progress && progress->cancel.load()) break;
        TransferTally sub;
        if (const VaultResult r = transfer_gallery(src, path, dst, dst_parent, mode,
                                                   {.tally = &sub, .policy = policy}); r == Ok) {
            ++out.done;
            // Merge per-file failures from this subtree into the output.
            // per-file failures inside a structurally-Ok subtree do not change the
            // SUBTREE done/failed counts, but the entries surface in the dialog
            merge_failures(out, std::move(sub));
        } else {
            // Structural failure: record as a gallery-level entry.
            record_failure(out, std::string(path), r, TransferFailure::Stage::Write);
        }
        if (progress) progress->done.fetch_add(1);
    }
    return out;
}

std::vector<std::string> colliding_galleries(const Vault& dst, std::string_view dst_parent,
                                             std::span<const std::string> src_paths)
{
    std::vector<std::string> out;
    if (!dst.is_unlocked()) return out;
    for (const auto& path : src_paths) {
        const std::string name = last_segment(path);
        for (const auto* c : dst.list(dst_parent)) {
            if (c->name == name) { out.push_back(name); break; }
        }
    }
    return out;
}

} // namespace vault
