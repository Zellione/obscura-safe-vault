#include "vault/migration.h"

#include <string>

#include "vault/vault.h"

namespace vault {
namespace {

void walk(const Vault& v, const std::string& path, MigrationScan& out, bool thumbs_stale)
{
    for (const IndexNode* n : v.list(path)) {
        const std::string child =
            path.empty() ? std::string(n->name.view()) : path + "/" + std::string(n->name.view());
        if (n->is_gallery()) {
            walk(v, child, out, thumbs_stale);
            continue;
        }
        if (n->is_video()) {
            if (n->vmeta.codec == VideoCodec::Unknown) {
                ++out.videos;
                out.bytes += n->vmeta.orig_size;
            } else if (thumbs_stale && n->vmeta.codec != VideoCodec::Unknown) {
                // Phase 75: known-codec videos that need poster regen count toward thumbs.
                // Regenerating the poster requires reading the original (orig_size).
                ++out.thumbs;
                out.bytes += n->vmeta.orig_size;
            }
            continue;
        }
        // The animated arm deliberately over-counts: a genuinely static GIF is
        // indistinguishable from an un-backfilled one without decrypting it, so
        // it IS work. The watermark, not this walk, prevents recurrence.
        if (n->is_image() && format_can_animate(n->meta.format) && !n->meta.animated) {
            ++out.images;
            out.bytes += n->meta.orig_size;
        } else if (thumbs_stale && n->is_image() && n->meta.thumb_length > 0) {
            // Phase 75: existing images with thumbnails need regen at the new budget.
            ++out.thumbs;
            out.bytes += n->meta.orig_size;
        }
    }
}

} // namespace

bool migration_pending(const VaultSettings& s, uint16_t probe_caps_gen,
                       uint16_t thumb_side) noexcept
{
    return s.migrated_index_version < MIGRATION_INDEX_VERSION ||
           s.migrated_probe_caps    < probe_caps_gen ||
           s.migrated_thumb_side    < thumb_side;
}

VaultSettings stamp_migrated(VaultSettings s, uint16_t probe_caps_gen,
                             uint16_t thumb_side) noexcept
{
    s.migrated_index_version = MIGRATION_INDEX_VERSION;
    s.migrated_probe_caps    = probe_caps_gen;
    s.migrated_thumb_side    = thumb_side;
    return s;
}

MigrationScan scan_migration(const Vault& v, bool thumbs_stale)
{
    MigrationScan scan;
    walk(v, "", scan, thumbs_stale);
    return scan;
}

} // namespace vault
