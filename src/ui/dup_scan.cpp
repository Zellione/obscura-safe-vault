#include "ui/dup_scan.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <span>
#include <monocypher.h>

#include "crypto/secure_mem.h"
#include "image/decode.h"
#include "ui/dup_video_sig.h"
#include "vault/vault.h"

namespace ui {

namespace {

void walk(const vault::Vault& v, const std::string& path,
          std::vector<DupScanItem>& out)
{
    for (const vault::IndexNode* n : v.list(path)) {
        const std::string child = path.empty() ? n->name : path + "/" + n->name;
        if (n->is_gallery()) {
            walk(v, child, out);
            continue;
        }
        DupScanItem it;
        it.node_path   = child;
        it.name        = n->name;
        it.parent_path = path;
        it.is_video    = n->is_video();
        if (n->is_image()) {
            it.bytes        = n->meta.orig_size;
            it.width        = n->meta.width;
            it.height       = n->meta.height;
            it.data_spans   = {{n->meta.data_offset, n->meta.data_length}};
            it.thumb_offset = n->meta.thumb_offset;
            it.thumb_length = n->meta.thumb_length;
        } else {
            it.bytes  = n->vmeta.orig_size;
            it.width  = n->vmeta.width;
            it.height = n->vmeta.height;
            it.data_spans.reserve(n->vmeta.chunks.size());
            for (const vault::VideoChunk& c : n->vmeta.chunks)
                it.data_spans.emplace_back(c.offset, c.length);
            it.thumb_offset = n->vmeta.poster_offset;
            it.thumb_length = n->vmeta.poster_length;
            it.duration_us  = n->vmeta.duration_us;
            it.chunk_size   = n->vmeta.chunk_size;
        }
        out.push_back(std::move(it));
    }
}

using Digest = std::array<uint8_t, 32>;

// Result of a hash operation: Success, Failed (recoverable), or Locked (vault locked under us).
enum class HashResult {
    Success,
    Failed,  // Read error (AuthFailed, NotFound, etc.)
    Locked   // Vault locked under us — must stop immediately
};

DupMember to_member(const DupScanItem& it)
{
    DupMember m;
    m.node_path    = it.node_path;
    m.name         = it.name;
    m.parent_path  = it.parent_path;
    m.is_video     = it.is_video;
    m.bytes        = it.bytes;
    m.width        = it.width;
    m.height       = it.height;
    m.thumb_offset = it.thumb_offset;
    m.thumb_length = it.thumb_length;
    m.data_spans   = it.data_spans;
    return m;
}

// Hash one item's full plaintext through the thread-safe span reader.
// Returns Success on successful hash, Failed on read errors (skip and continue),
// or Locked if vault was locked under us (must stop immediately).
HashResult hash_item(const vault::Vault& v, const DupScanItem& it, Digest& out)
{
    if (it.data_spans.empty()) return HashResult::Failed;
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, out.size());
    crypto::SecureBytes scratch;
    for (const auto& [off, len] : it.data_spans) {
        const auto res = vault::read_thumb_span(v, off, len, scratch);
        if (res == vault::VaultResult::Locked) return HashResult::Locked;
        if (res != vault::VaultResult::Ok) return HashResult::Failed;
        crypto_blake2b_update(&ctx, scratch.data(), scratch.size());
        // scratch is reused; SecureBytes wipes on reassignment/destruction.
    }
    crypto_blake2b_final(&ctx, out.data());
    return HashResult::Success;
}

// Exact pass: hash items and group by hash.
// Shared state threaded through the scan passes (keeps each pass under the
// cpp:S107 parameter cap). Everything is borrowed from DupScanJob::run.
struct PassCtx {
    const vault::Vault&             v;
    const std::vector<DupScanItem>& items;
    const std::atomic<bool>&        cancel;
    std::atomic<size_t>&            done;
    std::atomic<size_t>&            total;
    std::mutex&                     mtx;
    std::string&                    current;
};

DupScanOutcome exact_pass(const PassCtx& ctx, std::vector<bool>& in_exact)
{
    DupScanOutcome out;

    // Build size buckets.
    std::map<std::pair<bool, uint64_t>, std::vector<size_t>> size_buckets;
    for (size_t i = 0; i < ctx.items.size(); ++i) {
        const auto& it = ctx.items[i];
        size_buckets[std::make_pair(it.is_video, it.bytes)].push_back(i);
    }

    // Process each bucket.
    std::map<Digest, std::vector<size_t>> hash_groups;
    for (const auto& [key, indices] : size_buckets) {
        if (indices.size() < 2) continue;  // No duplicates in this bucket

        for (size_t idx : indices) {
            if (ctx.cancel.load()) {
                out.cancelled = true;
                return out;
            }

            const auto& it = ctx.items[idx];
            {
                const std::lock_guard lk(ctx.mtx);
                ctx.current = it.node_path;
            }

            Digest digest;
            const auto hash_res = hash_item(ctx.v, it, digest);
            if (hash_res == HashResult::Locked) {
                out.cancelled = true;
                return out;
            }
            if (hash_res == HashResult::Failed) {
                out.skipped++;
            } else {
                hash_groups[digest].push_back(idx);
                in_exact[idx] = false;  // Mark as potentially exact
            }

            ctx.done++;
        }
    }

    // Convert hash groups to DupGroup.
    for (const auto& [digest, indices] : hash_groups) {
        if (indices.size() >= 2) {
            DupGroup g;
            g.kind = DupGroup::Kind::Identical;
            g.distance_bits = 0;
            for (size_t idx : indices) {
                g.members.push_back(to_member(ctx.items[idx]));
                in_exact[idx] = true;
            }
            out.groups.push_back(std::move(g));
        }
    }

    return out;
}

// Perceptual pass: decode thumbnails and cluster by dHash.
void perceptual_pass(const PassCtx& ctx, const std::vector<bool>& in_exact, bool perceptual,
                     DupScanOutcome& out)
{
    if (!perceptual) return;

    // Parallel vectors: hashes[k] belongs to ctx.items[hashed_indices[k]].
    // A skipped (unreadable/undecodable) thumbnail must not push an entry into
    // either — a mismatch here silently attaches the WRONG file to a group.
    std::vector<uint64_t> hashes;
    std::vector<size_t> hashed_indices;

    for (size_t i = 0; i < ctx.items.size(); ++i) {
        const auto& it = ctx.items[i];
        if (it.is_video || it.thumb_length == 0 || in_exact[i]) continue;

        if (ctx.cancel.load()) {
            out.cancelled = true;
            return;
        }
        {
            const std::lock_guard lk(ctx.mtx);
            ctx.current = it.node_path;
        }

        crypto::SecureBytes thumb;
        const auto read_res = vault::read_thumb_span(ctx.v, it.thumb_offset, it.thumb_length, thumb);
        if (read_res == vault::VaultResult::Locked) {
            out.cancelled = true;
            return;
        }
        if (read_res != vault::VaultResult::Ok) {
            out.skipped++;
            ctx.done++;
            continue;
        }

        auto decoded = image::decode_from_memory(thumb.as_span());
        if (!decoded) {
            out.skipped++;
            ctx.done++;
            continue;
        }

        hashes.push_back(dhash64(std::span(decoded->pixels.data(), decoded->pixels.size()),
                                 decoded->width, decoded->height));
        hashed_indices.push_back(i);
        ctx.done++;
    }

    // Cluster similar hashes.
    if (hashes.empty()) return;
    const auto clusters = cluster_similar(hashes, DUP_SIMILAR_MAX_BITS);

    // Convert clusters to DupGroup.
    for (const auto& cluster : clusters) {
        if (cluster.size() < 2) continue;

        DupGroup g;
        g.kind = DupGroup::Kind::Similar;
        g.distance_bits = 0;

        // Compute max pairwise distance.
        for (size_t i = 0; i < cluster.size(); ++i) {
            for (size_t j = i + 1; j < cluster.size(); ++j) {
                const auto dist = hamming64(hashes[cluster[i]], hashes[cluster[j]]);
                g.distance_bits = std::max(g.distance_bits, dist);
            }
        }

        for (size_t idx : cluster) {
            g.members.push_back(to_member(ctx.items[hashed_indices[idx]]));
        }

        out.groups.push_back(std::move(g));
    }
}

// Byte reader over a video's plaintext stream for compute_video_frame_sig.
// VideoSource::fill_one's chunk mapping re-hosted on the thread-safe
// vault::read_thumb_span — the scan worker must never touch read_fp_. Caches
// the one most-recently-decrypted chunk (mlock'd, wiped on replacement).
struct VideoStream {
    const vault::Vault& v;
    const DupScanItem&  item;
    crypto::SecureBytes cache;
    int64_t             cached_index = -1;
    bool                locked       = false;

    int64_t read(uint64_t offset, std::span<uint8_t> dst)
    {
        if (item.chunk_size == 0 || offset >= item.bytes) return offset >= item.bytes ? 0 : -1;
        const auto idx = static_cast<int64_t>(offset / item.chunk_size);
        if (idx >= static_cast<int64_t>(item.data_spans.size())) return 0;
        if (idx != cached_index) {
            const auto& [off, len] = item.data_spans[static_cast<size_t>(idx)];
            if (const auto res = vault::read_thumb_span(v, off, len, cache);
                res != vault::VaultResult::Ok) {
                locked = res == vault::VaultResult::Locked;
                cached_index = -1;
                return -1;
            }
            cached_index = idx;
        }
        const uint64_t in_chunk = offset - static_cast<uint64_t>(idx) * item.chunk_size;
        if (in_chunk >= cache.size()) return 0;
        const size_t n = std::min(dst.size(), cache.size() - static_cast<size_t>(in_chunk));
        std::memcpy(dst.data(), cache.data() + in_chunk, n);
        return static_cast<int64_t>(n);
    }
};

// Poster dHash for one video candidate; leaves poster_ok false on any failure.
// Returns Locked only when the vault locked underneath the read.
vault::VaultResult poster_hash(const PassCtx& ctx, const DupScanItem& it, VideoSig& sig)
{
    using enum vault::VaultResult;
    if (it.thumb_length == 0) return Ok;
    crypto::SecureBytes poster;
    if (const auto res = vault::read_thumb_span(ctx.v, it.thumb_offset, it.thumb_length, poster);
        res != Ok)
        return res;
    const auto decoded = image::decode_from_memory(poster.as_span());
    if (!decoded) return Ok;
    sig.poster_hash = dhash64(std::span(decoded->pixels.data(), decoded->pixels.size()),
                              decoded->width, decoded->height);
    sig.poster_ok = true;
    return Ok;
}

// True when i/j are plausible partners on the cheap evidence alone: duration
// close AND poster-close (or a poster missing on either side).
bool plausible_pair(const std::vector<DupScanItem>& items, std::span<const VideoSig> sigs,
                    std::span<const size_t> cand, size_t i, size_t j)
{
    if (!duration_close(items[cand[i]].duration_us, items[cand[j]].duration_us)) return false;
    if (!sigs[i].poster_ok || !sigs[j].poster_ok) return true;
    return hamming64(sigs[i].poster_hash, sigs[j].poster_hash) <= DUP_VID_POSTER_MAX_BITS;
}

// Stage 2 — poster dHash per candidate. sigs[k] parallels cand[k].
// False = cancelled (user or vault lock); out.cancelled is already set.
bool poster_stage(const PassCtx& ctx, std::span<const size_t> cand,
                  std::vector<VideoSig>& sigs, DupScanOutcome& out)
{
    for (size_t k = 0; k < cand.size(); ++k) {
        if (ctx.cancel.load()) {
            out.cancelled = true;
            return false;
        }
        const auto& it = ctx.items[cand[k]];
        {
            const std::lock_guard lk(ctx.mtx);
            ctx.current = it.node_path;
        }
        if (poster_hash(ctx, it, sigs[k]) == vault::VaultResult::Locked) {
            out.cancelled = true;
            return false;
        }
        ctx.done++;
    }
    return true;
}

// Stage 3 — sampled-frame signatures for flagged candidates. False = cancelled.
bool frame_stage(const PassCtx& ctx, std::span<const size_t> cand,
                 std::span<const char> wants_frames, std::vector<VideoSig>& sigs,
                 DupScanOutcome& out)
{
    for (size_t k = 0; k < cand.size(); ++k) {
        if (!wants_frames[k]) continue;
        if (ctx.cancel.load()) {
            out.cancelled = true;
            return false;
        }
        const auto& it = ctx.items[cand[k]];
        {
            const std::lock_guard lk(ctx.mtx);
            ctx.current = it.node_path;
        }
        VideoStream stream{.v = ctx.v, .item = it, .cache = {}};
        if (const DupStreamRead reader = [&stream](uint64_t off, std::span<uint8_t> dst) {
                return stream.read(off, dst);
            };
            !compute_video_frame_sig(reader, it.bytes, sigs[k]) && stream.locked) {
            out.cancelled = true;
            return false;
        }
        ctx.done++;
    }
    return true;
}

// Phase 62: perceptual video pass — duration gate (index metadata only),
// poster dHash prefilter, then sampled-frame confirm for surviving pairs.
void video_perceptual_pass(const PassCtx& ctx, const std::vector<bool>& in_exact,
                           bool perceptual, DupScanOutcome& out)
{
    if (!perceptual) return;

    // Stage 1 — duration gate, zero I/O: a video proceeds only when another
    // non-exact video is duration-close to it.
    std::vector<size_t> cand;
    for (size_t i = 0; i < ctx.items.size(); ++i)
        if (ctx.items[i].is_video && !in_exact[i]) cand.push_back(i);
    std::erase_if(cand, [&cand, &ctx](size_t i) {
        return std::ranges::none_of(cand, [&ctx, i](size_t j) {
            return j != i && duration_close(ctx.items[i].duration_us, ctx.items[j].duration_us);
        });
    });
    if (cand.size() < 2) return;
    ctx.total += cand.size();

    std::vector<VideoSig> sigs(cand.size());
    if (!poster_stage(ctx, cand, sigs, out)) return;

    // Flag videos with at least one plausible partner for frame decoding.
    std::vector<char> wants_frames(cand.size(), 0);
    for (size_t i = 0; i < cand.size(); ++i)
        for (size_t j = i + 1; j < cand.size(); ++j)
            if (plausible_pair(ctx.items, sigs, cand, i, j)) {
                wants_frames[i] = 1;
                wants_frames[j] = 1;
            }

    ctx.total += static_cast<size_t>(std::ranges::count(wants_frames, char{1}));
    if (!frame_stage(ctx, cand, wants_frames, sigs, out)) return;

    // Stage 4 — cluster and emit. durations[k] parallels sigs[k]/cand[k].
    std::vector<uint64_t> durations(cand.size());
    for (size_t k = 0; k < cand.size(); ++k) durations[k] = ctx.items[cand[k]].duration_us;
    for (const auto& cluster : cluster_video_sigs(sigs, durations)) {
        DupGroup g;
        g.kind = DupGroup::Kind::SimilarVideo;
        g.distance_bits = 0;
        for (size_t pos : cluster) g.members.push_back(to_member(ctx.items[cand[pos]]));
        out.groups.push_back(std::move(g));
    }
}

} // namespace

std::vector<DupScanItem> collect_scan_items(const vault::Vault& v)
{
    std::vector<DupScanItem> out;
    walk(v, "", out);
    return out;
}

// --- DupScanJob implementation ---

DupScanJob::~DupScanJob()
{
    cancel();
    if (thread_.joinable()) thread_.join();
}

std::string DupScanJob::current_name() const
{
    const std::lock_guard lk(mtx_);
    return current_;
}

std::optional<DupScanOutcome> DupScanJob::take_outcome()
{
    const std::lock_guard lk(mtx_);
    auto out = std::move(outcome_);
    outcome_.reset();
    return out;
}

void DupScanJob::start(const vault::Vault& v, std::vector<DupScanItem> items, bool perceptual)
{
    done_.store(0);
    total_.store(0);
    cancel_.store(false);
    running_.store(true);
    {
        const std::lock_guard lk(mtx_);
        current_.clear();
        outcome_.reset();
    }

    thread_ = std::jthread([this, &v, items = std::move(items), perceptual]() mutable {
        run(v, std::move(items), perceptual);
    });
}

void DupScanJob::run(const vault::Vault& v, std::vector<DupScanItem> items, bool perceptual)
{
    // Calculate total: candidates with size-bucket >= 2 + image items with thumbs.
    std::map<std::pair<bool, uint64_t>, std::vector<size_t>> size_buckets;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        size_buckets[std::make_pair(it.is_video, it.bytes)].push_back(i);
    }

    size_t total_exact = 0;
    for (const auto& [key, indices] : size_buckets) {
        if (indices.size() >= 2) total_exact += indices.size();
    }

    size_t total_perceptual = 0;
    if (perceptual) {
        for (const auto& it : items) {
            if (!it.is_video && it.thumb_length > 0) total_perceptual++;
        }
    }

    total_.store(total_exact + total_perceptual);

    // Track which items ended up in exact groups.
    std::vector<bool> in_exact(items.size(), false);

    const PassCtx ctx{.v = v, .items = items, .cancel = cancel_, .done = done_,
                      .total = total_, .mtx = mtx_, .current = current_};

    // Exact pass.
    auto out = exact_pass(ctx, in_exact);
    if (!out.cancelled) {
        // Perceptual passes append their groups to the same outcome.
        perceptual_pass(ctx, in_exact, perceptual, out);
    }
    if (!out.cancelled) {
        video_perceptual_pass(ctx, in_exact, perceptual, out);
    }

    {
        const std::lock_guard lk(mtx_);
        outcome_ = std::move(out);
        running_.store(false);
    }
}

} // namespace ui
