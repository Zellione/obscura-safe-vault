#include "ui/dup_scan.h"

#include <array>
#include <map>
#include <monocypher.h>

#include "crypto/secure_mem.h"
#include "image/decode.h"
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
DupScanOutcome exact_pass(const vault::Vault& v, const std::vector<DupScanItem>& items,
                          std::atomic<bool>& cancel, std::atomic<size_t>& done,
                          [[maybe_unused]] std::atomic<size_t>& total, std::mutex& mtx, std::string& current_,
                          std::vector<bool>& in_exact)
{
    DupScanOutcome out;

    // Build size buckets.
    std::map<std::pair<bool, uint64_t>, std::vector<size_t>> size_buckets;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        size_buckets[std::make_pair(it.is_video, it.bytes)].push_back(i);
    }

    // Process each bucket.
    std::map<Digest, std::vector<size_t>> hash_groups;
    for (auto& [key, indices] : size_buckets) {
        if (indices.size() < 2) continue;  // No duplicates in this bucket

        for (size_t idx : indices) {
            if (cancel.load()) {
                out.cancelled = true;
                return out;
            }

            const auto& it = items[idx];
            {
                const std::lock_guard lk(mtx);
                current_ = it.node_path;
            }

            Digest digest;
            const auto hash_res = hash_item(v, it, digest);
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

            done++;
        }
    }

    // Convert hash groups to DupGroup.
    for (auto& [digest, indices] : hash_groups) {
        if (indices.size() >= 2) {
            DupGroup g;
            g.kind = DupGroup::Kind::Identical;
            g.distance_bits = 0;
            for (size_t idx : indices) {
                g.members.push_back(to_member(items[idx]));
                in_exact[idx] = true;
            }
            out.groups.push_back(std::move(g));
        }
    }

    return out;
}

// Perceptual pass: decode thumbnails and cluster by dHash.
void perceptual_pass(const vault::Vault& v, const std::vector<DupScanItem>& items,
                     const std::vector<bool>& in_exact, bool perceptual,
                     std::atomic<bool>& cancel, std::atomic<size_t>& done,
                     [[maybe_unused]] std::atomic<size_t>& total, std::mutex& mtx, std::string& current_,
                     DupScanOutcome& out)
{
    if (!perceptual) return;

    std::vector<uint64_t> hashes;
    std::vector<size_t> image_indices;

    // Collect decodable images not in exact groups.
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].is_video && items[i].thumb_length > 0 && !in_exact[i]) {
            image_indices.push_back(i);
        }
    }

    // Decode and hash.
    for (size_t idx : image_indices) {
        if (cancel.load()) {
            out.cancelled = true;
            return;
        }

        const auto& it = items[idx];
        {
            const std::lock_guard lk(mtx);
            current_ = it.node_path;
        }

        crypto::SecureBytes thumb;
        const auto read_res = vault::read_thumb_span(v, it.thumb_offset, it.thumb_length, thumb);
        if (read_res == vault::VaultResult::Locked) {
            out.cancelled = true;
            return;
        }
        if (read_res != vault::VaultResult::Ok) {
            out.skipped++;
            done++;
            continue;
        }

        auto decoded = image::decode_from_memory(thumb.as_span());
        if (!decoded) {
            out.skipped++;
            done++;
            continue;
        }

        const auto h = dhash64(std::span(decoded->pixels.data(), decoded->pixels.size()),
                              decoded->width, decoded->height);
        hashes.push_back(h);

        done++;
    }

    // Cluster similar hashes.
    if (hashes.empty()) return;
    auto clusters = cluster_similar(hashes, DUP_SIMILAR_MAX_BITS);

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
            g.members.push_back(to_member(items[image_indices[idx]]));
        }

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
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            if (!it.is_video && it.thumb_length > 0) total_perceptual++;
        }
    }

    total_.store(total_exact + total_perceptual);

    // Track which items ended up in exact groups.
    std::vector<bool> in_exact(items.size(), false);

    // Exact pass.
    auto out = exact_pass(v, items, cancel_, done_, total_, mtx_, current_, in_exact);
    if (out.cancelled) {
        const std::lock_guard lk(mtx_);
        outcome_ = std::move(out);
        running_.store(false);
        return;
    }

    // Perceptual pass.
    perceptual_pass(v, items, in_exact, perceptual, cancel_, done_, total_, mtx_, current_, out);
    if (out.cancelled) {
        const std::lock_guard lk(mtx_);
        outcome_ = std::move(out);
        running_.store(false);
        return;
    }

    // Store outcome.
    {
        const std::lock_guard lk(mtx_);
        outcome_ = std::move(out);
        running_.store(false);
    }
}

} // namespace ui
