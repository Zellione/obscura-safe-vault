#include "ui/migration_job.h"

#include <cstring>
#include <utility>
#include <vector>

#include "crypto/secure_mem.h"
#include "image/anim_info.h"
#include "image/image.h"
#include "media/video_probe.h"
#include "vault/index.h"
#include "vault/migration.h"
#include "vault/vault.h"

namespace ui {
namespace {

// One unit of migration work, holding COPIED chunk spans rather than an
// IndexNode* — the pool added in Task 5 must never hold a tree pointer.
struct Item {
    std::string node_path;
    bool        is_video = false;
    std::vector<std::pair<uint64_t, uint64_t>> data_spans;
    uint64_t    bytes      = 0;
    uint32_t    chunk_size = 0;   // videos only
    uint8_t     format     = 0;   // images only
};

// What a worker learned. `ok == false` means the read or decode failed.
struct Result {
    size_t   index = 0;
    bool     ok    = false;
    bool     resolved = false;    // video: a real codec came back
    vault::VideoCodec codec = vault::VideoCodec::Unknown;
    uint32_t width = 0, height = 0;
    uint64_t duration_us = 0;
    std::vector<uint8_t> poster_jpeg;
    bool     animated = false;    // images only
};

void collect(const vault::Vault& v, const std::string& path, std::vector<Item>& out)
{
    for (const vault::IndexNode* n : v.list(path)) {
        const std::string child = path.empty() ? n->name : path + "/" + n->name;
        if (n->is_gallery()) { collect(v, child, out); continue; }

        if (n->is_video() && n->vmeta.codec == vault::VideoCodec::Unknown) {
            Item it;
            it.node_path  = child;
            it.is_video   = true;
            it.bytes      = n->vmeta.orig_size;
            it.chunk_size = n->vmeta.chunk_size;
            it.data_spans.reserve(n->vmeta.chunks.size());
            for (const vault::VideoChunk& c : n->vmeta.chunks)
                it.data_spans.emplace_back(c.offset, c.length);
            out.push_back(std::move(it));
            continue;
        }
        if (n->is_image() && vault::format_can_animate(n->meta.format) &&
            !n->meta.animated) {
            Item it;
            it.node_path  = child;
            it.is_video   = false;
            it.bytes      = n->meta.orig_size;
            it.format     = static_cast<uint8_t>(n->meta.format);
            it.data_spans = {{n->meta.data_offset, n->meta.data_length}};
            out.push_back(std::move(it));
        }
    }
}

// Concatenate an item's chunk spans into mlock'd memory using ONLY
// vault::read_thumb_span — the any-thread-safe decrypt path. Never touches
// read_fp_ or the tree.
bool read_item(const vault::Vault& v, const Item& it, crypto::SecureBytes& out)
{
    if (it.data_spans.size() == 1) {
        const auto& [off, len] = it.data_spans[0];
        return vault::read_thumb_span(v, off, len, out) == vault::VaultResult::Ok;
    }
    std::vector<uint8_t> joined;          // wiped below via SecureBytes copy
    joined.reserve(static_cast<size_t>(it.bytes));
    crypto::SecureBytes chunk;
    for (const auto& [off, len] : it.data_spans) {
        if (vault::read_thumb_span(v, off, len, chunk) != vault::VaultResult::Ok) {
            crypto_wipe(joined.data(), joined.size());
            return false;
        }
        joined.insert(joined.end(), chunk.data(), chunk.data() + chunk.size());
    }
    if (!out.resize(joined.size())) {
        crypto_wipe(joined.data(), joined.size());
        return false;
    }
    std::copy(joined.begin(), joined.end(), out.span().begin());
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

    if (it.is_video) {
        media::VideoProbeResult probe;
        if (media::probe_video(data.as_span(), probe) &&
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

    r.animated = image::is_animated(static_cast<image::ImageFormat>(it.format),
                                    data.as_span());
    r.ok = true;
    return r;
}

} // namespace

MigrationJob::~MigrationJob() = default;

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

    std::vector<Item> items;
    collect(v, "", items);
    out.total = static_cast<int>(items.size());
    progress_.total.store(out.total);

    phase_.store(MigrationPhase::Repairing);
    for (size_t i = 0; i < items.size(); ++i) {
        if (progress_.cancel.load()) { out.cancelled = true; break; }

        const Result r = process(v, items[i], i);
        if (!r.ok) {
            ++out.failed;
        } else if (items[i].is_video) {
            if (r.resolved) {
                const vault::VideoProbeApply apply{
                    .codec       = r.codec,
                    .width       = r.width,
                    .height      = r.height,
                    .duration_us = r.duration_us,
                    .poster_jpeg = std::span<const uint8_t>(r.poster_jpeg),
                };
                if (vault::apply_video_probe(v, items[i].node_path, apply) ==
                    vault::VaultResult::Ok) {
                    ++out.videos_fixed;
                } else {
                    ++out.failed;
                }
            } else {
                ++out.videos_skipped;
            }
        } else {
            if (vault::apply_image_animated(v, items[i].node_path, r.animated) ==
                vault::VaultResult::Ok) {
                ++out.images_fixed;
            } else {
                ++out.failed;
            }
        }
        progress_.done.store(static_cast<int>(i + 1));
    }

    phase_.store(MigrationPhase::Committing);
    // The watermark is stamped ONLY on a full pass. A cancel still commits the
    // work already applied — it is correct and durable — but leaves the vault
    // marked un-migrated so the next unlock re-offers it.
    // Re-read cancel before stamping to catch a cancel arriving after loop exit.
    if (progress_.cancel.load()) {
        out.cancelled = true;
    }
    vault::VaultSettings settings = vault::vault_settings(v);
    if (!out.cancelled) {
        settings = vault::stamp_migrated(settings, media::PROBE_CAPS_GEN);
    }
    if (const auto res = vault::commit_migration(v, settings);
        res != vault::VaultResult::Ok) {
        out.ok    = false;
        out.error = "Migration could not be saved; the vault is unchanged.";
        phase_.store(MigrationPhase::Done);
        outcome_ = std::move(out);
        done_.store(true);
        return;
    }

    out.ok = true;
    phase_.store(MigrationPhase::Done);
    outcome_ = std::move(out);
    done_.store(true);
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
