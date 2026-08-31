#include "ui/migration_job.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <format>
#include <thread>
#include <utility>
#include <vector>

#include "crypto/secure_mem.h"
#include "image/anim_info.h"
#include "image/decode.h"
#include "image/image.h"
#include "image/thumbnail.h"
#include "media/video_probe.h"
#include "vault/index.h"
#include "vault/migration.h"
#include "vault/vault.h"

namespace ui {
namespace {

// One unit of migration work, holding COPIED chunk refs rather than an
// IndexNode* — the pool added in Task 5 must never hold a tree pointer. The
// refs carry the Phase 99 decrypt context so the any-thread worker can
// authenticate each chunk it reads.
struct Item {
    enum class Kind : uint8_t { VideoProbe, ImageAnimated, ImageThumb, VideoPoster };
    Kind        kind = Kind::ImageAnimated;
    std::string node_path;
    std::vector<vault::ChunkRef> refs;
    uint64_t    bytes      = 0;
    uint32_t    chunk_size = 0;   // videos only
    uint8_t     format     = 0;   // images only
    bool        thumbs_stale = false;  // Phase 75: video poster regen gate
};

// What a worker learned. `ok == false` means the read or decode failed.
struct Result {
    size_t   index = 0;
    bool     ok    = false;
    bool     resolved = false;    // video: a real codec came back
    vault::VideoCodec codec = vault::VideoCodec::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t duration_us = 0;
    crypto::SecureBytes poster_jpeg;
    crypto::SecureBytes thumb_jpeg;     // ImageThumb / VideoPoster output (Phase 75)
    bool     sniff_animated = false;      // ImageThumb also sniffed (format can animate)
    bool     animated = false;    // images only
};

// Build a video item from a video node. Workers never mutate the tree. The
// per-chunk refs carry each chunk's record id + sequence + the node's identity.
void collect_video_item(Item::Kind kind, std::string_view child,
                        const vault::IndexNode& n, bool thumbs_stale,
                        std::vector<Item>& out)
{
    Item it;
    it.kind         = kind;
    it.node_path    = child;
    it.bytes        = n.vmeta.orig_size;
    it.chunk_size   = n.vmeta.chunk_size;
    it.thumbs_stale = thumbs_stale;
    it.refs.reserve(n.vmeta.chunks.size());
    for (size_t i = 0; i < n.vmeta.chunks.size(); ++i)
        it.refs.push_back(vault::video_chunk_ref(n, i));
    out.push_back(std::move(it));
}

// Build an image item. Workers never mutate the tree.
void collect_image_item(Item::Kind kind, std::string_view child,
                        const vault::IndexNode& n, std::vector<Item>& out)
{
    Item it;
    it.kind      = kind;
    it.node_path = child;
    it.bytes     = n.meta.orig_size;
    it.format    = std::to_underlying(n.meta.format);
    it.refs      = {vault::image_data_chunk_ref(n)};
    out.push_back(std::move(it));
}

void collect(const vault::Vault& v, const std::string& path, std::vector<Item>& out,
             bool thumbs_stale)
{
    for (const vault::IndexNode* n : v.list(path)) {
        const std::string child =
            path.empty() ? std::string(n->name.view()) : path + "/" + std::string(n->name.view());
        if (n->is_gallery()) { collect(v, child, out, thumbs_stale); continue; }

        if (n->is_video()) {
            if (n->vmeta.codec == vault::VideoCodec::Unknown) {
                collect_video_item(Item::Kind::VideoProbe, child, *n, thumbs_stale, out);
                continue;
            }
            if (thumbs_stale && n->vmeta.codec != vault::VideoCodec::Unknown) {
                // Phase 75: known-codec videos need poster regen at new budget
                collect_video_item(Item::Kind::VideoPoster, child, *n, thumbs_stale, out);
            }
            continue;
        }

        if (n->is_image()) {
            if (thumbs_stale && n->meta.thumb_length > 0) {
                // Phase 75: existing images with thumbnails need regen at the new budget.
                // This SUBSUMES the animated arm: process() will sniff animated.
                collect_image_item(Item::Kind::ImageThumb, child, *n, out);
            } else if (vault::format_can_animate(n->meta.format) && !n->meta.animated) {
                // Animated arm: detect animated flag regardless of thumbs_stale.
                // During thumb-stale pass, only no-thumb images reach here (those with
                // thumb_length > 0 become ImageThumb above). During thumb-fresh pass,
                // all animatable un-sniffed images reach here.
                collect_image_item(Item::Kind::ImageAnimated, child, *n, out);
            }
        }
    }
}

// Phase 99: every media node whose records still lack the context-bound AEAD
// (a legacy / not-yet-finalized vault). Coordinator rewrites these IN PLACE
// (no decode, no worker pool) — re-encrypt each record under the node's id.
void collect_context_paths(const vault::Vault& v, const std::string& path,
                           std::vector<std::string>& out)
{
    for (const vault::IndexNode* n : v.list(path)) {
        const std::string child =
            path.empty() ? std::string(n->name.view()) : path + "/" + std::string(n->name.view());
        if (n->is_gallery()) {
            collect_context_paths(v, child, out);
            continue;
        }
        const bool bound = n->is_video() ? n->vmeta.context_bound : n->meta.context_bound;
        if (!bound) out.push_back(child);
    }
}

// Concatenate an item's chunk refs into mlock'd memory using ONLY
// vault::read_thumb_span — the any-thread-safe decrypt path. Never touches
// read_fp_ or the tree.
bool read_item(const vault::Vault& v, const Item& it, crypto::SecureBytes& out)
{
    if (it.refs.size() == 1) {
        return vault::read_thumb_span(v, it.refs[0], out) == vault::VaultResult::Ok;
    }
    std::vector<uint8_t> joined;          // wiped below via SecureBytes copy
    joined.reserve(static_cast<size_t>(it.bytes));
    crypto::SecureBytes chunk;
    for (const vault::ChunkRef& ref : it.refs) {
        if (vault::read_thumb_span(v, ref, chunk) != vault::VaultResult::Ok) {
            crypto_wipe(joined.data(), joined.size());
            return false;
        }
        joined.insert(joined.end(), chunk.data(), chunk.data() + chunk.size());
    }
    if (!out.resize(joined.size())) {
        crypto_wipe(joined.data(), joined.size());
        return false;
    }
    std::ranges::copy(joined, out.span().begin());
    crypto_wipe(joined.data(), joined.size());
    return true;
}

// Pure CPU stage. Safe on any thread: reads only through read_thumb_span,
// touches no tree node.
Result process(const vault::Vault& v, const Item& it, size_t index)
{
    Result r;
    r.index = index;

    crypto::SecureBytes data;
    if (!read_item(v, it, data)) return r;   // ok stays false

    using enum Item::Kind;

    if (it.kind == VideoPoster) {
        // Phase 75: regenerate poster at new budget (probe already encodes at THUMB_MAX_SIDE)
        if (media::VideoProbeResult probe;
            media::probe_video(data.as_span(), probe)) {
            r.thumb_jpeg = std::move(probe.poster_jpeg);
        }
        r.ok = true;  // empty poster on probe failure is ok (skip, count as skipped)
        return r;
    }

    if (it.kind == VideoProbe) {
        // Existing VideoProbe arm: decode and capture codec metadata
        if (media::VideoProbeResult probe;
            media::probe_video(data.as_span(), probe) &&
            probe.codec != vault::VideoCodec::Unknown) {
            r.resolved    = true;
            r.codec       = probe.codec;
            r.width       = probe.width;
            r.height      = probe.height;
            r.duration_us = probe.duration_us;
            r.poster_jpeg = std::move(probe.poster_jpeg);
        }
        r.ok = true;   // a clean "still undecodable" is a success, not a failure
        return r;
    }

    if (it.kind == ImageThumb) {
        // Phase 75: decode, regenerate thumbnail at THUMB_MAX_SIDE, sniff animated
        if (auto decoded = image::decode_from_memory(data.as_span())) {
            if (auto thumb = image::make_thumbnail(*decoded, image::THUMB_MAX_SIDE, 85)) {
                r.thumb_jpeg = std::move(*thumb);
            }
            // Sniff animated flag when format can animate
            const auto vfmt = static_cast<vault::ImageFormat>(it.format);
            if (vault::format_can_animate(vfmt)) {
                r.sniff_animated = true;
                r.animated =
                    image::is_animated(static_cast<image::ImageFormat>(it.format),
                                       data.as_span());
            }
        }
        // Empty thumb_jpeg is ok (skip, not failure) — only !ok counts as failure
        r.ok = true;
        return r;
    }

    // ImageAnimated arm (existing behavior)
    r.animated = image::is_animated(static_cast<image::ImageFormat>(it.format),
                                    data.as_span());
    r.ok = true;
    return r;
}

// Bounded result queue. The bound is not hygiene: decoded frames and encoded
// posters live in mlock'd SecureBytes and the process budget is 256 MiB
// (platform::grow_secure_mem_budget), so an unbounded queue would exhaust the
// lockable pool and start failing locks mid-migration.
class ResultQueue {
public:
    explicit ResultQueue(size_t cap) : cap_(cap) {}

    void push(Result r)
    {
        {
            std::unique_lock lk(m_);
            not_full_.wait(lk, [this] { return q_.size() < cap_ || closed_; });
            q_.push_back(std::move(r));
        }
        not_empty_.notify_one();
    }

    bool pop(Result& out)
    {
        {
            std::unique_lock lk(m_);
            not_empty_.wait(lk, [this] { return !q_.empty() || closed_; });
            if (q_.empty()) return false;
            out = std::move(q_.front());
            q_.pop_front();
        }
        not_full_.notify_one();
        return true;
    }

    void close()
    {
        { std::lock_guard lk(m_); closed_ = true; }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    std::mutex              m_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<Result>      q_;
    size_t                  cap_;
    bool                    closed_ = false;
};

// Apply image thumb result. Coordinator-thread only.
//
// sync=false: a thumb-regen pass applies one of these per image in the vault
// (potentially the whole library), and each used to fsync individually — O(items)
// fsyncs turned a routine upgrade into a very long, hang-looking stall. The
// appended chunk only needs to be durable before the index that references it is
// committed; MigrationJob::run() syncs once, right before commit_migration(),
// which covers every deferred append made since the job started (fsync flushes
// the whole file, not just the most recent write).
void apply_image_thumb_item(vault::Vault& v, const Item& it, const Result& r,
                            MigrationOutcome& out)
{
    // Phase 75: apply the regenerated thumbnail
    if (!r.thumb_jpeg.empty()) {
        if (vault::apply_image_thumb(v, it.node_path, r.thumb_jpeg.as_span(), /*sync=*/false) ==
            vault::VaultResult::Ok) {
            ++out.thumbs_fixed;
        } else {
            ++out.failed;
            return;
        }
    }
    // Also sniff and apply the animated flag if applicable
    if (r.sniff_animated) {
        (void)vault::apply_image_animated(v, it.node_path, r.animated);
        // apply_image_animated only counts in images_fixed if the flag actually changed;
        // here we just apply it for consistency, no separate count needed
    }
}

// Apply video poster result. Coordinator-thread only. sync=false — see
// apply_image_thumb_item.
void apply_video_poster_item(vault::Vault& v, const Item& it, const Result& r,
                             MigrationOutcome& out)
{
    // Phase 75: apply the regenerated poster
    if (!r.thumb_jpeg.empty()) {
        if (vault::apply_video_poster(v, it.node_path, r.thumb_jpeg.as_span(), /*sync=*/false) ==
            vault::VaultResult::Ok) {
            ++out.thumbs_fixed;
        } else {
            ++out.failed;
        }
    } else {
        ++out.videos_skipped;
    }
}

// Apply video probe result. Coordinator-thread only. sync=false — see
// apply_image_thumb_item.
void apply_video_probe_item(vault::Vault& v, const Item& it, const Result& r,
                            MigrationOutcome& out)
{
    // Existing VideoProbe arm: detect codec + optionally replace stale poster
    if (!r.resolved) { ++out.videos_skipped; return; }
    if (const vault::VideoProbeApply apply{
            .codec       = r.codec,
            .width       = r.width,
            .height      = r.height,
            .duration_us = r.duration_us,
            .poster_jpeg = r.poster_jpeg.as_span(),
        };
        vault::apply_video_probe(v, it.node_path, apply, /*sync=*/false) ==
        vault::VaultResult::Ok)
        ++out.videos_fixed;
    else {
        ++out.failed;
        return;
    }

    // Phase 75: if thumbs_stale and we have a new poster, also replace the old one
    // (apply_video_probe only fills EMPTY poster spans)
    if (it.thumbs_stale && !r.poster_jpeg.empty() &&
        vault::apply_video_poster(v, it.node_path, r.poster_jpeg.as_span(), /*sync=*/false) ==
            vault::VaultResult::Ok)
        ++out.thumbs_fixed;
    // If poster replace fails, the probe already succeeded, so don't fail the
    // whole video; just skip the poster regen for this item
}

// Coordinator-thread only: mutates the tree and may append to fp_.
void apply_one(vault::Vault& v, const Item& it, const Result& r, MigrationOutcome& out)
{
    if (!r.ok) { ++out.failed; return; }

    using enum Item::Kind;

    if (it.kind == ImageThumb) {
        apply_image_thumb_item(v, it, r, out);
        return;
    }

    if (it.kind == VideoPoster) {
        apply_video_poster_item(v, it, r, out);
        return;
    }

    if (it.kind == VideoProbe) {
        apply_video_probe_item(v, it, r, out);
        return;
    }

    // ImageAnimated arm (existing behavior)
    if (vault::apply_image_animated(v, it.node_path, r.animated) ==
        vault::VaultResult::Ok)
        ++out.images_fixed;
    else
        ++out.failed;
}

// Fan `items` across a worker pool and apply each result on the CALLING thread.
// Workers only ever read through the any-thread-safe decrypt path; the caller is
// the coordinator, and the only thread that mutates the tree or fp_.
// Precondition: `items` is non-empty (so at least one worker is spawned).
// `done_base` lets the caller run the decode pool AFTER a coordinator-only arm
// (the Phase 99 context rewrite) without clobbering its progress.
void run_pool(vault::Vault& v, const std::vector<Item>& items,
              vault::OpProgress& progress, MigrationOutcome& out, int done_base = 0)
{
    const unsigned hw = std::thread::hardware_concurrency();
    const size_t   workers =
        std::min<size_t>(items.size(), std::max<unsigned>(1u, hw > 1 ? hw - 1 : 1u));

    ResultQueue         results(std::max<size_t>(2, workers * 2));
    std::atomic<size_t> next{0};
    std::atomic<size_t> active_workers{workers};

    std::vector<std::jthread> pool;
    pool.reserve(workers);
    for (size_t w = 0; w < workers; ++w) {
        pool.emplace_back([&v, &items, &progress, &results, &next, &active_workers] {
            for (;;) {
                const size_t i = next.fetch_add(1);
                if (i >= items.size() || progress.cancel.load()) break;
                results.push(process(v, items[i], i));
            }
            // The last worker to leave closes the queue so the coordinator drains.
            if (active_workers.fetch_sub(1) == 1) results.close();
        });
    }

    int        collected = 0;
    const auto expect    = static_cast<int>(items.size());
    Result     r;
    while (collected < expect && results.pop(r)) {
        apply_one(v, items[r.index], r, out);
        ++collected;
        progress.done.store(done_base + collected);
        if (progress.cancel.load()) { out.cancelled = true; break; }
    }
    // Close the queue if the workers have not already (e.g. we broke out early).
    if (active_workers.load() > 0) results.close();
    for (auto& t : pool) if (t.joinable()) t.join();
}

// Reclaim what the repairs orphaned. Runs only when the waste justifies the
// whole-file rewrite: compaction is gated on AUTO_COMPACT_MIN_WASTE (256 KiB)
// alone, with no waste/size ratio term — rewriting to reclaim a few KiB costs
// more I/O than it saves, but large vaults still need the option.
// A failed compact is NOT a failed migration: the repairs are already committed
// and the vault is valid, just larger than ideal.
void maybe_compact(vault::Vault& v, std::atomic<MigrationPhase>& phase,
                   vault::OpProgress& progress, MigrationOutcome& out)
{
    const uint64_t wasted = vault::vault_wasted_bytes(v);
    if (wasted < vault::Vault::AUTO_COMPACT_MIN_WASTE) return;

    phase.store(MigrationPhase::Compacting);
    // Save the repairing-phase progress to restore after compaction. This
    // preserves the report of completed repair work and ensures job.done()
    // reflects the actual repairs finished, not the compaction phase. See
    // migration_job_pool_handles_many_items_without_loss.
    const int done_count  = progress.done.load();
    const int total_count = progress.total.load();
    progress.done.store(0);
    progress.total.store(0);
    if (v.compact(&progress) == vault::VaultResult::Ok) out.reclaimed_bytes = wasted;
    // Restore progress regardless of compact()'s result, so a failed compaction
    // doesn't leave misleading progress state (total == done == 0).
    progress.done.store(done_count);
    progress.total.store(total_count);
}

} // namespace

MigrationProgressText migration_progress_text(MigrationPhase phase, int done, int total)
{
    using enum MigrationPhase;
    MigrationProgressText t;
    switch (phase) {
        case Repairing:  t.title = std::format("Upgrading {} / {}", done, total); break;
        case Committing: t.title = "Saving…"; break;
        case Compacting: t.title = "Reclaiming space…"; break;
        // Done falls out of run() once the job has already finished; showing
        // "Preparing…" here is what read as a hang stuck at the finish line
        // (progress modal still up, counters maxed, but the label saying the
        // opposite of "nearly done"). Idle/Scanning are the only phases where
        // "Preparing…" is actually correct.
        case Done:       t.title = "Finishing…"; break;
        default:         t.title = "Preparing…"; break;   // Idle, Scanning
    }
    t.count_line = total > 0 ? std::format("{} / {}", done, total) : "Preparing…";
    return t;
}

// Cancel before the jthread member's implicit join: a plain join would block
// destruction until the whole migration ran to completion.
MigrationJob::~MigrationJob() { abort_and_join(); }

bool MigrationJob::start(vault::Vault& v)
{
    if (active_.load()) return false;
    progress_.total.store(0);
    progress_.done.store(0);
    progress_.cancel.store(false);
    phase_.store(MigrationPhase::Scanning);
    outcome_ = MigrationOutcome{};
    done_.store(false);
    active_.store(true);
    thread_ = std::jthread([this, &v] { run(v); });
    return true;
}

void MigrationJob::run(vault::Vault& v)
{
    MigrationOutcome out;

    // Phase 75: compute thumbs_stale to decide whether to regenerate thumbnails
    const vault::VaultSettings settings = vault::vault_settings(v);
    const bool thumbs_stale =
        settings.migrated_thumb_side < static_cast<uint16_t>(image::THUMB_MAX_SIDE);
    // Phase 99: a legacy / not-yet-finalized vault owes the context rewrite.
    const bool context_stale = !vault::uses_context_chunks(v);

    std::vector<Item> items;
    collect(v, "", items, thumbs_stale);
    out.total = static_cast<int>(items.size());

    std::vector<std::string> ctx_paths;
    if (context_stale) {
        collect_context_paths(v, "", ctx_paths);
        out.total += static_cast<int>(ctx_paths.size());
    }
    progress_.total.store(out.total);

    phase_.store(MigrationPhase::Repairing);

    // Phase 99: rewrite every v1 record IN PLACE (coordinator-only — pure
    // re-encrypt, no decode). Runs BEFORE the decode pool so the pool's
    // thumb/poster appends (apply_*) go straight to the context-bound AEAD.
    if (context_stale && !ctx_paths.empty()) {
        for (const std::string& p : ctx_paths) {
            if (progress_.cancel.load()) { out.cancelled = true; break; }
            if (vault::apply_context_rewrite(v, p) == vault::VaultResult::Ok)
                ++out.context_fixed;
            else
                ++out.failed;
            progress_.done.store(out.context_fixed + out.failed);
        }
    }

    if (!items.empty() && !out.cancelled)
        run_pool(v, items, progress_, out, out.context_fixed + out.failed);

    phase_.store(MigrationPhase::Committing);
    // The watermark is stamped ONLY on a full pass. A cancel still commits the
    // work already applied — it is correct and durable — but leaves the vault
    // marked un-migrated so the next unlock re-offers it.
    // Re-read cancel before stamping to catch a cancel arriving after loop exit.
    if (progress_.cancel.load()) {
        out.cancelled = true;
    }

    // Phase 99: on a full pass, re-seal the master-key wrap + set the flag so
    // the COMMIT below seals the index blob with the flag's AD and the slot
    // swap persists the flag + wrap atomically (crash → slot-fallback recovers).
    if (!out.cancelled && context_stale &&
        vault::finalize_context_migration(v) != vault::VaultResult::Ok) {
        out.ok    = false;
        out.error = "Context migration could not be finalized; the vault is unchanged.";
        phase_.store(MigrationPhase::Done);
        outcome_ = std::move(out);
        done_.store(true);
        return;
    }

    vault::VaultSettings stamped_settings = vault::vault_settings(v);
    if (!out.cancelled) {
        stamped_settings = vault::stamp_migrated(stamped_settings, media::PROBE_CAPS_GEN,
                                                  static_cast<uint16_t>(image::THUMB_MAX_SIDE));
    }
    if (const auto res = vault::commit_migration(v, stamped_settings);
        res != vault::VaultResult::Ok) {
        out.ok    = false;
        out.error = "Migration could not be saved; the vault is unchanged.";
        phase_.store(MigrationPhase::Done);
        outcome_ = std::move(out);
        done_.store(true);
        return;
    }

    // Phase 3: reclaim what the repairs orphaned. Skipped on cancel — the pass is
    // incomplete and will be re-offered. Progress counters are reused for the
    // compact bar. Waste is measured AFTER commit: the commit itself creates waste
    // (superseded index blobs), which is reclaimable and belongs in this phase.
    if (!out.cancelled) maybe_compact(v, phase_, progress_, out);

    out.ok = true;
    phase_.store(MigrationPhase::Done);
    outcome_ = std::move(out);
    done_.store(true);
}

void MigrationJob::abort_and_join()
{
    if (!active_.load()) return;
    progress_.cancel.store(true);
    if (thread_.joinable()) thread_.join();   // run() exits between items on cancel
    active_.store(false);
    done_.store(false);   // outcome deliberately discarded — no one is left to show it
}

std::optional<MigrationOutcome> MigrationJob::take_outcome()
{
    if (!active_.load() || !done_.load()) return std::nullopt;
    if (thread_.joinable()) thread_.join();
    active_.store(false);
    done_.store(false);
    return std::move(outcome_);
}

} // namespace ui
