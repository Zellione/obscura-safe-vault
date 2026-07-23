#include "index.h"

#include <algorithm>
#include <utility>

#include "byte_io.h"

namespace vault {

namespace {

void write_node(ByteWriter& w, const IndexNode& node)
{
    w.u8(std::to_underlying(node.type));

    // name_len (u16) + name bytes (UTF-8). Names longer than 65535 bytes are
    // clamped — far beyond any real filename or gallery name.
    const std::string& name = node.name;
    const uint16_t name_len = name.size() > 0xFFFF ? 0xFFFF
                                                   : static_cast<uint16_t>(name.size());
    w.u16(name_len);
    w.bytes(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(name.data()), name_len));

    // Write tags (Phase 12): tag_count (u16) + per-tag (tag_len u16 + bytes).
    // Clamp tag_count to INDEX_MAX_TAGS; clamp each tag_len to 0xFFFF.
    const uint16_t tag_count = node.tags.size() > INDEX_MAX_TAGS ? INDEX_MAX_TAGS
                                                                 : static_cast<uint16_t>(node.tags.size());
    w.u16(tag_count);
    for (uint16_t i = 0; i < tag_count; ++i) {
        const auto& tag = node.tags[i];
        const uint16_t tag_len = tag.size() > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(tag.size());
        w.u16(tag_len);
        w.bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(tag.data()), tag_len));
    }

    // Favorite flag (Phase 13): one byte after the tag block, uniform for both
    // node kinds, so the reader parses it version-gated without branching on type.
    w.u8(node.favorite ? 1 : 0);

    // Sort key (Phase 37): one byte after the favorite flag, uniform for every
    // node type (meaningful only for Gallery, ignored on read for Image/Video) —
    // same "uniform, version-gated" shape as the favorite byte above, so the
    // reader never branches on type for this field either.
    w.u8(std::to_underlying(node.sort_key));

    if (node.type == IndexNode::Type::Gallery) {
        w.u32(static_cast<uint32_t>(node.children.size()));
        for (const auto& child : node.children) write_node(w, child);
    } else if (node.type == IndexNode::Type::Image) {
        const ImageMeta& m = node.meta;
        w.u8(std::to_underlying(m.format));
        w.u32(m.width);
        w.u32(m.height);
        w.u64(m.orig_size);
        w.u64(m.created_ts);
        w.u64(m.data_offset);
        w.u64(m.data_length);
        w.u64(m.thumb_offset);
        w.u64(m.thumb_length);
        w.u8(m.animated ? 1 : 0);
    } else if (node.type == IndexNode::Type::Video) {
        const VideoMeta& m = node.vmeta;
        w.u8(std::to_underlying(m.container));
        w.u8(std::to_underlying(m.codec));
        w.u32(m.width);
        w.u32(m.height);
        w.u64(m.duration_us);
        w.u64(m.orig_size);
        w.u64(m.created_ts);
        w.u32(m.chunk_size);
        const uint32_t n = m.chunks.size() > INDEX_MAX_VIDEO_CHUNKS
                               ? INDEX_MAX_VIDEO_CHUNKS
                               : static_cast<uint32_t>(m.chunks.size());
        w.u32(n);
        for (uint32_t i = 0; i < n; ++i) { w.u64(m.chunks[i].offset); w.u64(m.chunks[i].length); }
        w.u64(m.poster_offset);
        w.u64(m.poster_length);
    }
}

// Read a length-prefixed UTF-8 string (u16 len + bytes) into `out`. Returns
// false on truncation/malformed input.
bool read_string(ByteReader& r, std::string& out)
{
    const uint16_t len = r.u16();
    if (!r.ok()) return false;
    out.resize(len);
    if (len > 0) {
        r.bytes(std::span<uint8_t>(reinterpret_cast<uint8_t*>(out.data()), len));
        if (!r.ok()) return false;
    }
    return true;
}

// Read the Phase 12 tag block (u16 count + length-prefixed tags) into `tags`.
// Bounded by INDEX_MAX_TAGS so a hostile count can't drive a huge allocation.
bool read_tags(ByteReader& r, std::vector<std::string>& tags)
{
    tags.clear();
    const uint16_t tag_count = r.u16();
    if (!r.ok() || tag_count > INDEX_MAX_TAGS) return false;
    for (uint16_t i = 0; i < tag_count; ++i) {
        std::string tag;
        if (!read_string(r, tag)) return false;
        tags.push_back(std::move(tag));
    }
    return true;
}

bool read_image_meta(ByteReader& r, ImageMeta& m, uint8_t version)
{
    m.format       = static_cast<ImageFormat>(r.u8());
    m.width        = r.u32();
    m.height       = r.u32();
    m.orig_size    = r.u64();
    m.created_ts   = r.u64();
    m.data_offset  = r.u64();
    m.data_length  = r.u64();
    m.thumb_offset = r.u64();
    m.thumb_length = r.u64();
    if (!r.ok()) {
        return false;
    }

    // Phase 47: animated flag defaults to false for pre-v7 blobs. Rejected (not
    // clamped) if out of range, matching the Phase 37 sort_key rule.
    m.animated = false;  // default for v1..v6
    if (version >= 7) {
        const uint8_t a = r.u8();
        if (!r.ok() || a > 1) {
            return false;
        }
        m.animated = (a == 1);
    }
    return r.ok();
}

// Read VideoMeta from the deserialisation stream (Phase 15 PR2). Returns false
// on malformed input, particularly on a chunk_count that would cause OOM.
// The bound check happens BEFORE any allocation to defend against hostile input.
bool read_video_meta(ByteReader& r, VideoMeta& m)
{
    m.container   = static_cast<VideoContainer>(r.u8());
    m.codec       = static_cast<VideoCodec>(r.u8());
    m.width       = r.u32();
    m.height      = r.u32();
    m.duration_us = r.u64();
    m.orig_size   = r.u64();
    m.created_ts  = r.u64();
    m.chunk_size  = r.u32();
    const uint32_t n = r.u32();
    if (!r.ok() || n > INDEX_MAX_VIDEO_CHUNKS) return false;  // bound BEFORE allocating
    m.chunks.clear();
    m.chunks.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        VideoChunk c;
        c.offset = r.u64();
        c.length = r.u64();
        if (!r.ok()) return false;
        m.chunks.push_back(c);
    }
    m.poster_offset = r.u64();
    m.poster_length = r.u64();
    return r.ok();
}

// Read and validate sort key for a node (version-aware, Phase 37+).
[[nodiscard]] bool read_sort_key(ByteReader& r, SortKey& sort_key, uint8_t version)
{
    using enum SortKey;
    sort_key = Default;
    if (version >= 6) {
        const uint8_t max_sk = version >= 8 ? std::to_underlying(Insertion)
                                            : std::to_underlying(SizeDesc);
        const uint8_t sk = r.u8();
        if (!r.ok() || sk > max_sk) {
            return false;
        }
        sort_key = static_cast<SortKey>(sk);
    }
    return true;
}

// Returns false on malformed input. Depth-limited to guard against stack
// overflow from a deeply nested blob. Version-aware: reads tags only if version >= 2.
bool read_node(ByteReader& r, IndexNode& node, uint32_t depth, uint8_t version)
{
    if (depth > INDEX_MAX_DEPTH) return false;

    const uint8_t type = r.u8();
    if (!r.ok()) return false;
    if (const bool is_video_type = type == std::to_underlying(IndexNode::Type::Video);
        type != std::to_underlying(IndexNode::Type::Gallery) &&
        type != std::to_underlying(IndexNode::Type::Image)  &&
        !(is_video_type && version >= 4)) {
        return false;  // unknown node type (or a Video node in a pre-v4 blob)
    }
    node.type = static_cast<IndexNode::Type>(type);

    if (!read_string(r, node.name)) return false;

    // Tags (Phase 12) exist only from version 2 on; older blobs carry none.
    node.tags.clear();
    if (version >= 2 && !read_tags(r, node.tags)) return false;

    // Favorite flag (Phase 13) exists only from version 3 on; older blobs read
    // as not-favorited. Any non-zero byte counts as favorited.
    node.favorite = false;
    if (version >= 3) {
        const uint8_t fav = r.u8();
        if (!r.ok()) return false;
        node.favorite = fav != 0;
    }

    // Sort key (Phase 37) exists only from version 6 on; older blobs default to
    // Default (which resolves to the vault-wide default_sort — for a pre-v8
    // vault that is Insertion, i.e. exactly the old behaviour).
    // An out-of-range byte is rejected outright, like an unknown node `type` byte.
    // v6/v7 knew only up to SizeDesc; v8 added Insertion.
    if (!read_sort_key(r, node.sort_key, version)) {
        return false;
    }

    if (node.type == IndexNode::Type::Image) {
        return read_image_meta(r, node.meta, version);
    }

    if (node.type == IndexNode::Type::Video) {
        return read_video_meta(r, node.vmeta);
    }

    const uint32_t child_count = r.u32();
    if (!r.ok()) return false;
    node.children.clear();
    for (uint32_t i = 0; i < child_count; ++i) {
        if (!r.ok()) return false;  // bail early — don't spin on a bogus count
        IndexNode child;
        if (!read_node(r, child, depth + 1, version)) return false;
        node.children.push_back(std::move(child));
    }
    return true;
}

// Write the Phase 18 saved-searches block: u16 count + per-entry { name
// (u16 len + bytes), query (u32 len + bytes) }. Counts/lengths are clamped to
// their bounds so a pathological in-memory list can't emit an unreadable blob.
void write_saved_searches(ByteWriter& w, const std::vector<SavedSearch>& searches)
{
    const uint16_t count = searches.size() > INDEX_MAX_SAVED_SEARCHES
                               ? INDEX_MAX_SAVED_SEARCHES
                               : static_cast<uint16_t>(searches.size());
    w.u16(count);
    for (uint16_t i = 0; i < count; ++i) {
        const std::string& name = searches[i].name;
        const uint16_t name_len = name.size() > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(name.size());
        w.u16(name_len);
        w.bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(name.data()), name_len));

        const auto& q = searches[i].query;
        const uint32_t q_len = q.size() > INDEX_MAX_SAVED_QUERY_BYTES
                                   ? INDEX_MAX_SAVED_QUERY_BYTES
                                   : static_cast<uint32_t>(q.size());
        w.u32(q_len);
        w.bytes(std::span<const uint8_t>(q.data(), q_len));
    }
}

// Read the saved-searches block (v5+). Bounds checked BEFORE any allocation so a
// hostile count/length can't drive OOM. Returns false on truncation/over-large.
bool read_saved_searches(ByteReader& r, std::vector<SavedSearch>& searches)
{
    searches.clear();
    const uint16_t count = r.u16();
    if (!r.ok() || count > INDEX_MAX_SAVED_SEARCHES) return false;
    for (uint16_t i = 0; i < count; ++i) {
        SavedSearch s;
        if (!read_string(r, s.name)) return false;
        const uint32_t q_len = r.u32();
        if (!r.ok() || q_len > INDEX_MAX_SAVED_QUERY_BYTES) return false;  // bound before alloc
        s.query.resize(q_len);
        if (q_len > 0) {
            r.bytes(std::span<uint8_t>(s.query.data(), q_len));
            if (!r.ok()) return false;
        }
        searches.push_back(std::move(s));
    }
    return true;
}

// Case-insensitive ASCII equality for category names. Category names are opaque
// UTF-8; only ASCII case is folded, which is all the built-in categories need.
bool category_name_eq(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    auto lower = [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c);
    };
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// Write the Phase 49 settings block: default_sort u8, tiles_show_tags u8,
// cat_count u16, then per-entry { name (u16 len + bytes), swatch u8 }. Counts,
// lengths and the swatch are clamped so a pathological in-memory value can't
// emit a blob its own reader would reject.
void write_settings(ByteWriter& w, const VaultSettings& s)
{
    w.u8(std::to_underlying(s.default_sort));
    w.u8(s.tiles_show_tags ? 1 : 0);

    const uint16_t count = s.categories.size() > INDEX_MAX_TAG_CATEGORIES
                               ? INDEX_MAX_TAG_CATEGORIES
                               : static_cast<uint16_t>(s.categories.size());
    w.u16(count);
    for (uint16_t i = 0; i < count; ++i) {
        const std::string& name = s.categories[i].name;
        const uint16_t name_len = name.size() > INDEX_MAX_CATEGORY_BYTES
                                      ? INDEX_MAX_CATEGORY_BYTES
                                      : static_cast<uint16_t>(name.size());
        w.u16(name_len);
        w.bytes(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(name.data()), name_len));
        const uint8_t sw = s.categories[i].swatch;
        w.u8(sw < TAG_SWATCH_COUNT ? sw : 0);
    }
}

// Read the settings block (v8+). Every field is bounds-checked BEFORE any
// allocation, and an out-of-range value is REJECTED, not clamped (the Phase 37 /
// Phase 47 rule). Duplicate category names are dropped case-insensitively,
// keeping the first occurrence's casing and swatch.
bool read_settings(ByteReader& r, VaultSettings& s)
{
    s.categories.clear();

    const uint8_t sort = r.u8();
    if (!r.ok() || sort > std::to_underlying(SortKey::Insertion)) return false;
    s.default_sort = static_cast<SortKey>(sort);

    const uint8_t tiles = r.u8();
    if (!r.ok() || tiles > 1) return false;
    s.tiles_show_tags = (tiles == 1);

    const uint16_t count = r.u16();
    if (!r.ok() || count > INDEX_MAX_TAG_CATEGORIES) return false;  // bound before alloc
    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t name_len = r.u16();
        if (!r.ok() || name_len > INDEX_MAX_CATEGORY_BYTES) return false;
        TagCategory c;
        c.name.resize(name_len);
        if (name_len > 0) {
            r.bytes(std::span<uint8_t>(reinterpret_cast<uint8_t*>(c.name.data()), name_len));
            if (!r.ok()) return false;
        }
        c.swatch = r.u8();
        if (!r.ok() || c.swatch >= TAG_SWATCH_COUNT) return false;

        const bool dupe = std::ranges::any_of(s.categories, [&c](const TagCategory& e) {
            return category_name_eq(e.name, c.name);
        });
        if (!dupe) s.categories.push_back(std::move(c));
    }
    return true;
}

} // namespace

VaultSettings VaultSettings::seeded()
{
    // The nhentai-style metadata categories, each on a distinct swatch. Users
    // add, rename and remove rows freely from the settings overlay.
    VaultSettings s;
    s.categories = {
        {"artist", 0}, {"character", 1}, {"parody", 2},   {"group", 3},
        {"language", 4}, {"series", 5},  {"male", 6},     {"female", 7},
    };
    return s;
}

void serialize_index(const IndexNode& root, std::vector<uint8_t>& out)
{
    serialize_index(root, {}, VaultSettings{}, out);
}

void serialize_index(const IndexNode& root, const std::vector<SavedSearch>& searches,
                     std::vector<uint8_t>& out)
{
    serialize_index(root, searches, VaultSettings{}, out);
}

void serialize_index(const IndexNode& root, const std::vector<SavedSearch>& searches,
                     const VaultSettings& settings, std::vector<uint8_t>& out)
{
    out.clear();
    ByteWriter w(out);
    w.u8(INDEX_VERSION);
    write_node(w, root);
    write_saved_searches(w, searches);
    write_settings(w, settings);
}

bool deserialize_index(std::span<const uint8_t> in, IndexNode& out)
{
    std::vector<SavedSearch> ignored_s;
    VaultSettings            ignored_v;
    return deserialize_index(in, out, ignored_s, ignored_v);
}

bool deserialize_index(std::span<const uint8_t> in, IndexNode& out,
                       std::vector<SavedSearch>& searches)
{
    VaultSettings ignored;
    return deserialize_index(in, out, searches, ignored);
}

bool deserialize_index(std::span<const uint8_t> in, IndexNode& out,
                       std::vector<SavedSearch>& searches, VaultSettings& settings)
{
    searches.clear();
    settings = VaultSettings{};
    ByteReader r(in);
    const uint8_t version = r.u8();
    if (!r.ok()) return false;
    // Accept versions 1..INDEX_VERSION; older fields default to empty.
    if (version < 1 || version > INDEX_VERSION) return false;
    if (!read_node(r, out, 0, version)) return false;
    // The saved-searches block exists only from v5 on; older blobs end there.
    if (version >= 5 && !read_saved_searches(r, searches)) return false;
    // The settings block exists only from v8 on. A vault that has never stored
    // one — pre-v8 — comes back seeded; a v8 blob's own list is authoritative,
    // including an empty one.
    if (version >= 8) {
        if (!read_settings(r, settings)) return false;
    } else {
        settings = VaultSettings::seeded();
    }
    // Trailing bytes after a well-formed blob indicate corruption.
    return r.remaining() == 0;
}

} // namespace vault
