#include "vault/migration.h"

#include <string>

#include "vault/vault.h"

namespace vault {
namespace {

void walk(const Vault& v, const std::string& path, MigrationScan& out)
{
    for (const IndexNode* n : v.list(path)) {
        const std::string child = path.empty() ? n->name : path + "/" + n->name;
        if (n->is_gallery()) {
            walk(v, child, out);
            continue;
        }
        if (n->is_video()) {
            if (n->vmeta.codec == VideoCodec::Unknown) {
                ++out.videos;
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
        }
    }
}

} // namespace

bool migration_pending(const VaultSettings& s, uint16_t probe_caps_gen) noexcept
{
    return s.migrated_index_version < MIGRATION_INDEX_VERSION ||
           s.migrated_probe_caps    < probe_caps_gen;
}

VaultSettings stamp_migrated(VaultSettings s, uint16_t probe_caps_gen) noexcept
{
    s.migrated_index_version = MIGRATION_INDEX_VERSION;
    s.migrated_probe_caps    = probe_caps_gen;
    return s;
}

MigrationScan scan_migration(const Vault& v)
{
    MigrationScan scan;
    walk(v, "", scan);
    return scan;
}

} // namespace vault
