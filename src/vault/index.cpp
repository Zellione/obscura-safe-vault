#include "index.h"

#include <algorithm>
#include <array>
#include <utility>

#include "byte_io.h"

namespace vault {

namespace {

void write_node(ByteWriter& w, const IndexNode& node)
{
    w.u8(std::to_underlying(node.type));

    // name_len (u16) + name bytes (UTF-8). Names longer than 65535 bytes are
    // clamped — far beyond any real filename or gallery name.
    const std::string_view name = node.name.view();
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
        const std::string_view tag = node.tags[i].view();
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

    // Phase 99 (OSV-AUD-004): the stable per-node node_id, uniform like the
    // sort key. Always written for the current version; the reader gates on v13.
    w.bytes(node.node_id);

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
        w.u8(m.context_bound ? 1 : 0);
        w.bytes(m.data_id);
        w.bytes(m.thumb_id);
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
        for (uint32_t i = 0; i < n; ++i) {
            w.u64(m.chunks[i].offset);
            w.u64(m.chunks[i].length);
            w.u32(m.chunks[i].sequence);
            w.bytes(m.chunks[i].id);
        }
        w.u64(m.poster_offset);
        w.u64(m.poster_length);
        w.u8(m.context_bound ? 1 : 0);
        w.bytes(m.poster_id);
    }
}

// Read a length-prefixed UTF-8 string (u16 len + bytes) into `out`. Returns
// false on truncation/malformed input or allocation failure (the string's
// storage is a Phase 91 SecureString, so OOM is a deserialisation failure).
bool read_string(ByteReader& r, crypto::SecureString& out)
{
    const uint16_t len = r.u16();
    if (!r.ok()) return false;
    if (!out.resize(len)) return false;
    if (len > 0) {
        r.bytes(std::span<uint8_t>(out.data(), len));
        if (!r.ok()) return false;
    }
    return true;
}

// Read one length-prefixed string within a byte cap (Phase 73 field
// values / template fields). The bound is checked BEFORE any allocation.
// Shared by read_field_values to keep that function's cognitive load low.
bool read_field_str(ByteReader& r, crypto::SecureString& out, uint32_t cap)
{
    const uint16_t len = r.u16();
    if (!r.ok() || len > cap) return false;
    if (!out.resize(len)) return false;
    if (len > 0) {
        r.bytes(std::span<uint8_t>(out.data(), len));
        if (!r.ok()) return false;
    }
    return true;
}

// Read the Phase 12 tag block (u16 count + length-prefixed tags) into `tags`.
// Bounded by INDEX_MAX_TAGS so a hostile count can't drive a huge allocation.
bool read_tags(ByteReader& r, std::vector<crypto::SecureString>& tags)
{
    tags.clear();
    const uint16_t tag_count = r.u16();
    if (!r.ok() || tag_count > INDEX_MAX_TAGS) return false;
    for (uint16_t i = 0; i < tag_count; ++i) {
        crypto::SecureString tag;
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

    // Phase 99: context-bound flag + per-record ids exist only from v13 on.
    // Pre-v13 blobs are all legacy: context_bound == false, zero ids.
    m.context_bound = false;
    m.data_id.fill(0);
    m.thumb_id.fill(0);
    if (version >= 13) {
        const uint8_t cb = r.u8();
        if (!r.ok() || cb > 1) {
            return false;
        }
        m.context_bound = (cb == 1);
        r.bytes(m.data_id);
        r.bytes(m.thumb_id);
        if (!r.ok()) {
            return false;
        }
    }
    return true;
}

// Read VideoMeta from the deserialisation stream (Phase 15 PR2). Returns false
// on malformed input, particularly on a chunk_count that would cause OOM.
// The bound check happens BEFORE any allocation to defend against hostile input.
// `version` gates the Phase 99 sequence/id/context-bound fields (v13+).
bool read_video_meta(ByteReader& r, VideoMeta& m, uint8_t version)
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
        if (version >= 13) {
            c.sequence = r.u32();
            r.bytes(c.id);
        }
        if (!r.ok()) return false;
        m.chunks.push_back(c);
    }
    m.poster_offset = r.u64();
    m.poster_length = r.u64();
    m.context_bound = false;
    m.poster_id.fill(0);
    if (version >= 13) {
        const uint8_t cb = r.u8();
        if (!r.ok() || cb > 1) {
            return false;
        }
        m.context_bound = (cb == 1);
        r.bytes(m.poster_id);
        if (!r.ok()) {
            return false;
        }
    }
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

    // Phase 99: the stable node_id exists only from v13 on; pre-v13 blobs read
    // zero (every media record there is legacy / context_bound == false).
    node.node_id.fill(0);
    if (version >= 13) {
        r.bytes(node.node_id);
        if (!r.ok()) return false;
    }

    if (node.type == IndexNode::Type::Image) {
        return read_image_meta(r, node.meta, version);
    }

    if (node.type == IndexNode::Type::Video) {
        return read_video_meta(r, node.vmeta, version);
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
        const std::string_view name = searches[i].name.view();
        const uint16_t name_len = name.size() > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(name.size());
        w.u16(name_len);
        w.bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(name.data()), name_len));

        const auto& q = searches[i].query;
        const uint32_t q_len = q.size() > INDEX_MAX_SAVED_QUERY_BYTES
                                   ? INDEX_MAX_SAVED_QUERY_BYTES
                                   : static_cast<uint32_t>(q.size());
        w.u32(q_len);
        w.bytes(q.as_span().first(q_len));
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
        if (!s.query.resize(q_len)) return false;
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

// Helper: write categories block with template fields (Phase 49, Phase 73).
void write_categories_block(ByteWriter& w, const std::vector<vault::TagCategory>& categories)
{
    const uint16_t count = categories.size() > INDEX_MAX_TAG_CATEGORIES
                               ? INDEX_MAX_TAG_CATEGORIES
                               : static_cast<uint16_t>(categories.size());
    w.u16(count);
    for (uint16_t i = 0; i < count; ++i) {
        const std::string_view name = categories[i].name.view();
        const uint16_t name_len = name.size() > INDEX_MAX_CATEGORY_BYTES
                                      ? INDEX_MAX_CATEGORY_BYTES
                                      : static_cast<uint16_t>(name.size());
        w.u16(name_len);
        w.bytes(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(name.data()), name_len));
        const uint8_t sw = categories[i].swatch;
        w.u8(sw < TAG_SWATCH_COUNT ? sw : 0);

        const auto& fields = categories[i].fields;
        const uint8_t field_count = fields.size() > INDEX_MAX_TEMPLATE_FIELDS
                                        ? INDEX_MAX_TEMPLATE_FIELDS
                                        : static_cast<uint8_t>(fields.size());
        w.u8(field_count);
        for (uint8_t f = 0; f < field_count; ++f) {
            const std::string_view field = fields[f].view();
            const uint16_t flen = field.size() > INDEX_MAX_FIELD_BYTES
                                      ? INDEX_MAX_FIELD_BYTES
                                      : static_cast<uint16_t>(field.size());
            w.u16(flen);
            w.bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(field.data()), flen));
        }
    }
}

// Helper: write tag descriptions block (Phase 49).
void write_descriptions_block(ByteWriter& w, const std::vector<vault::TagDescription>& descriptions)
{
    const uint16_t desc_count = descriptions.size() > INDEX_MAX_TAG_DESCRIPTIONS
                                    ? INDEX_MAX_TAG_DESCRIPTIONS
                                    : static_cast<uint16_t>(descriptions.size());
    w.u16(desc_count);
    for (uint16_t i = 0; i < desc_count; ++i) {
        const std::string_view tag = descriptions[i].tag.view();
        const uint16_t tag_len = tag.size() > 0xFFFFu ? 0xFFFFu
                                                      : static_cast<uint16_t>(tag.size());
        w.u16(tag_len);
        w.bytes(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(tag.data()), tag_len));

        const std::string_view text = descriptions[i].text.view();
        const uint16_t text_len = text.size() > INDEX_MAX_TAG_DESC_BYTES
                                      ? INDEX_MAX_TAG_DESC_BYTES
                                      : static_cast<uint16_t>(text.size());
        w.u16(text_len);
        w.bytes(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(text.data()), text_len));
    }
}

// Helper: write tag field values block (Phase 73).
void write_field_values_block(ByteWriter& w, const std::vector<vault::TagFieldValue>& values)
{
    const uint16_t val_count = values.size() > INDEX_MAX_TAG_FIELD_VALUES
                                   ? INDEX_MAX_TAG_FIELD_VALUES
                                   : static_cast<uint16_t>(values.size());
    w.u16(val_count);
    for (uint16_t i = 0; i < val_count; ++i) {
        const auto& e = values[i];
        auto write_str = [&w](std::string_view str, size_t cap) {
            const uint16_t len = str.size() > cap ? static_cast<uint16_t>(cap)
                                                  : static_cast<uint16_t>(str.size());
            w.u16(len);
            w.bytes(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(str.data()), len));
        };
        write_str(e.tag.view(), 0xFFFFu);
        write_str(e.field.view(), INDEX_MAX_FIELD_BYTES);
        write_str(e.value.view(), INDEX_MAX_FIELD_VALUE_BYTES);
    }
}

// Write the Phase 49 settings block: default_sort u8, tiles_show_tags u8,
// cat_count u16, then per-entry { name (u16 len + bytes), swatch u8 }. Counts,
// lengths and the swatch are clamped so a pathological in-memory value can't
// emit a blob its own reader would reject.
void write_settings(ByteWriter& w, const VaultSettings& s)
{
    w.u8(std::to_underlying(s.default_sort));
    w.u8(s.tiles_show_tags ? 1 : 0);

    write_categories_block(w, s.categories);
    write_descriptions_block(w, s.tag_descriptions);

    // Phase 65 watermark. Clamped on write so a pathological in-memory value
    // can never emit a blob this reader would reject (the write_settings rule).
    w.u8(s.migrated_index_version > INDEX_VERSION ? 0 : s.migrated_index_version);
    w.u16(s.migrated_probe_caps);

    write_field_values_block(w, s.tag_field_values);

    // Phase 75 thumb watermark — LAST in the block (version-gated reads rely
    // on stable prefix order).
    w.u16(s.migrated_thumb_side);
}

// Helper: read the field sub-block for a category (v11+ only).
// Returns false on malformed input. Duplicates dropped ci, first occurrence kept.
bool read_category_fields_v11(ByteReader& r, TagCategory& c)
{
    const uint8_t field_count = r.u8();
    if (!r.ok() || field_count > INDEX_MAX_TEMPLATE_FIELDS) return false;
    for (uint8_t f = 0; f < field_count; ++f) {
        const uint16_t flen = r.u16();
        if (!r.ok() || flen > INDEX_MAX_FIELD_BYTES) return false;
        crypto::SecureString field;
        if (!field.resize(flen)) return false;
        if (flen > 0) {
            r.bytes(std::span<uint8_t>(field.data(), flen));
            if (!r.ok()) return false;
        }
        if (const bool fdupe = std::ranges::any_of(c.fields,
                                                   [&field](const crypto::SecureString& e) {
                                                       return category_name_eq(e.view(),
                                                                               field.view());
                                                   });
            !fdupe) {
            c.fields.push_back(std::move(field));
        }
    }
    return true;
}

// Helper: read category block. Bounds-checked before allocation, duplicates
// dropped case-insensitively, keeping first occurrence.
bool read_categories(ByteReader& r, std::vector<TagCategory>& categories, uint8_t version)
{
    const uint16_t count = r.u16();
    if (!r.ok() || count > INDEX_MAX_TAG_CATEGORIES) return false;  // bound before alloc
    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t name_len = r.u16();
        if (!r.ok() || name_len > INDEX_MAX_CATEGORY_BYTES) return false;
        TagCategory c;
        if (!c.name.resize(name_len)) return false;
        if (name_len > 0) {
            r.bytes(std::span<uint8_t>(c.name.data(), name_len));
            if (!r.ok()) return false;
        }
        c.swatch = r.u8();
        if (!r.ok() || c.swatch >= TAG_SWATCH_COUNT) return false;

        // Phase 73: the field sub-block exists only from v11 on.
        if (version >= 11 && !read_category_fields_v11(r, c)) return false;

        if (const bool dupe = std::ranges::any_of(categories,
                                                  [&c](const TagCategory& e) {
                                                      return category_name_eq(e.name.view(),
                                                                              c.name.view());
                                                  });
            !dupe) {
            categories.push_back(std::move(c));
        }
    }
    return true;
}

// Helper: read tag description block. Bounds-checked before allocation, duplicates
// dropped case-insensitively, keeping first occurrence.
bool read_descriptions(ByteReader& r, std::vector<TagDescription>& descriptions)
{
    const uint16_t desc_count = r.u16();
    if (!r.ok() || desc_count > INDEX_MAX_TAG_DESCRIPTIONS) return false;  // bound before alloc
    for (uint16_t i = 0; i < desc_count; ++i) {
        const uint16_t tag_len = r.u16();
        if (!r.ok()) return false;
        TagDescription d;
        if (!d.tag.resize(tag_len)) return false;
        if (tag_len > 0) {
            r.bytes(std::span<uint8_t>(d.tag.data(), tag_len));
            if (!r.ok()) return false;
        }
        const uint16_t text_len = r.u16();
        if (!r.ok() || text_len > INDEX_MAX_TAG_DESC_BYTES) return false;
        if (!d.text.resize(text_len)) return false;
        if (text_len > 0) {
            r.bytes(std::span<uint8_t>(d.text.data(), text_len));
            if (!r.ok()) return false;
        }
        const bool dupe = std::ranges::any_of(descriptions, [&d](const TagDescription& e) {
            return category_name_eq(e.tag.view(), d.tag.view());
        });
        if (!dupe) descriptions.push_back(std::move(d));
    }
    return true;
}

// Helper: read the Phase 73 tag-field-values block. Bounds-checked before
// allocation; duplicate (tag, field) pairs dropped ci, keeping the first.
bool read_field_values(ByteReader& r, std::vector<TagFieldValue>& values)
{
    const uint16_t count = r.u16();
    if (!r.ok() || count > INDEX_MAX_TAG_FIELD_VALUES) return false;  // bound before alloc
    for (uint16_t i = 0; i < count; ++i) {
        TagFieldValue e;
        if (!read_field_str(r, e.tag, 0xFFFFu)) return false;
        if (!read_field_str(r, e.field, INDEX_MAX_FIELD_BYTES)) return false;
        if (!read_field_str(r, e.value, INDEX_MAX_FIELD_VALUE_BYTES)) return false;

        const bool dupe = std::ranges::any_of(values, [&e](const TagFieldValue& x) {
            return category_name_eq(x.tag.view(), e.tag.view()) &&
                   category_name_eq(x.field.view(), e.field.view());
        });
        if (!dupe) values.push_back(std::move(e));
    }
    return true;
}

// Read the settings block (v8+). Every field is bounds-checked BEFORE any
// allocation, and an out-of-range value is REJECTED, not clamped (the Phase 37 /
// Phase 47 rule). Duplicate category names are dropped case-insensitively,
// keeping the first occurrence's casing and swatch.
bool read_settings(ByteReader& r, VaultSettings& s, uint8_t version)
{
    s.categories.clear();
    s.tag_descriptions.clear();
    s.tag_field_values.clear();

    const uint8_t sort = r.u8();
    if (!r.ok() || sort > std::to_underlying(SortKey::Insertion)) return false;
    s.default_sort = static_cast<SortKey>(sort);

    const uint8_t tiles = r.u8();
    if (!r.ok() || tiles > 1) return false;
    s.tiles_show_tags = (tiles == 1);

    if (!read_categories(r, s.categories, version)) return false;

    // The description sub-block exists only from v9 on; a v8 blob ends after
    // the categories and must not be read past.
    if (version < 9) return true;

    if (!read_descriptions(r, s.tag_descriptions)) return false;

    // The watermark sub-block exists only from v10 on; a v9 blob ends after the
    // descriptions and must not be read past.
    if (version < 10) return true;

    const uint8_t migrated = r.u8();
    // Rejected, not clamped: a blob claiming a backfill from a future build is
    // malformed input, and silently accepting it would suppress a migration
    // this build genuinely still needs to run.
    if (!r.ok() || migrated > INDEX_VERSION) return false;
    s.migrated_index_version = migrated;

    s.migrated_probe_caps = r.u16();
    if (!r.ok()) return false;

    // The field-values sub-block exists only from v11 on; a v10 blob ends after
    // the watermark and must not be read past.
    if (version < 11) return true;

    if (!read_field_values(r, s.tag_field_values)) return false;

    // The thumb-side sub-block exists only from v12 on.
    if (version < 12) return true;
    s.migrated_thumb_side = r.u16();
    if (!r.ok()) return false;
    return true;
}

} // namespace

VaultSettings VaultSettings::seeded()
{
    // The nhentai-style metadata categories, each on a distinct swatch. Users
    // add, rename and remove rows freely from the settings overlay.
    VaultSettings s;
    std::vector<TagCategory> seeded_categories;
    seeded_categories.reserve(8);
    for (const auto& [name, swatch] : std::array<std::pair<const char*, uint8_t>, 8>{{
             {"artist", 0},
             {"character", 1},
             {"parody", 2},
             {"group", 3},
             {"language", 4},
             {"series", 5},
             {"male", 6},
             {"female", 7},
         }}) {
        seeded_categories.push_back(
            TagCategory{.name = crypto::SecureString(name), .swatch = swatch, .fields = {}});
    }
    s.categories = std::move(seeded_categories);
    return s;
}

std::string_view find_tag_description(const VaultSettings& s, std::string_view tag)
{
    for (const auto& d : s.tag_descriptions)
        if (category_name_eq(d.tag.view(), tag)) return d.text.view();
    return {};
}

void set_tag_description(VaultSettings& s, std::string_view tag, std::string_view text)
{
    for (auto it = s.tag_descriptions.begin(); it != s.tag_descriptions.end(); ++it) {
        if (!category_name_eq(it->tag.view(), tag)) continue;
        if (text.empty()) s.tag_descriptions.erase(it);
        else
            it->text = text;
        return;
    }
    if (text.empty()) return;                                   // nothing to remove
    if (s.tag_descriptions.size() >= INDEX_MAX_TAG_DESCRIPTIONS) return;
    s.tag_descriptions.emplace_back(crypto::SecureString(tag), crypto::SecureString(text));
}

std::string_view tag_category_prefix(std::string_view tag)
{
    const size_t colon = tag.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= tag.size()) return {};
    return tag.substr(0, colon);
}

namespace {
TagCategory* find_category(VaultSettings& s, std::string_view category)
{
    for (auto& c : s.categories)
        if (category_name_eq(c.name.view(), category)) return &c;
    return nullptr;
}
// True when `tag` belongs to `category` by prefix (ci).
bool tag_in_category(std::string_view tag, std::string_view category)
{
    return category_name_eq(tag_category_prefix(tag), category);
}
}  // namespace

std::span<const crypto::SecureString> category_template(const VaultSettings& s,
                                                        std::string_view category)
{
    for (const auto& c : s.categories)
        if (category_name_eq(c.name.view(), category)) return c.fields;
    return {};
}

bool set_category_template(VaultSettings& s, std::string_view category,
                           std::vector<std::string> fields)
{
    if (TagCategory* c = find_category(s, category); !c) {
        return false;
    } else {
        std::vector<crypto::SecureString> cleaned;
        for (auto& f : fields) {
            if (f.empty()) continue;
            if (f.size() > INDEX_MAX_FIELD_BYTES) f.resize(INDEX_MAX_FIELD_BYTES);
            if (const bool dupe = std::ranges::any_of(
                    cleaned,
                    [&f](const crypto::SecureString& e) { return category_name_eq(e.view(), f); });
                !dupe) {
                cleaned.emplace_back(f);
            }
            if (cleaned.size() >= INDEX_MAX_TEMPLATE_FIELDS) break;
        }
        c->fields = std::move(cleaned);
        return true;
    }
}

std::string_view find_tag_field_value(const VaultSettings& s, std::string_view tag,
                                      std::string_view field)
{
    for (const auto& e : s.tag_field_values)
        if (category_name_eq(e.tag.view(), tag) && category_name_eq(e.field.view(), field))
            return e.value.view();
    return {};
}

void set_tag_field_value(VaultSettings& s, std::string_view tag,
                         std::string_view field, std::string_view value)
{
    for (auto it = s.tag_field_values.begin(); it != s.tag_field_values.end(); ++it) {
        if (!category_name_eq(it->tag.view(), tag) || !category_name_eq(it->field.view(), field))
            continue;
        if (value.empty()) s.tag_field_values.erase(it);
        else
            it->value = value;
        return;
    }
    if (value.empty()) return;                                       // nothing to remove
    if (s.tag_field_values.size() >= INDEX_MAX_TAG_FIELD_VALUES) return;
    std::string v(value);
    if (v.size() > INDEX_MAX_FIELD_VALUE_BYTES) v.resize(INDEX_MAX_FIELD_VALUE_BYTES);
    s.tag_field_values.emplace_back(crypto::SecureString(tag), crypto::SecureString(field),
                                    crypto::SecureString(std::string_view(v)));
}

bool rename_template_field(VaultSettings& s, std::string_view category,
                           std::string_view old_field, std::string_view new_field)
{
    if (new_field.empty() || new_field.size() > INDEX_MAX_FIELD_BYTES) return false;
    if (TagCategory* c = find_category(s, category); !c) {
        return false;
    } else {
        auto it = std::ranges::find_if(c->fields, [old_field](const crypto::SecureString& f) {
            return category_name_eq(f.view(), old_field);
        });
        if (it == c->fields.end()) return false;
        const auto it_addr = std::to_address(it);
        if (const bool clash = std::ranges::any_of(c->fields,
                                                   [&](const crypto::SecureString& f) {
                                                       return std::to_address(&f) != it_addr &&
                                                              category_name_eq(f.view(), new_field);
                                                   });
            clash) {
            return false;
        }

        *it = new_field;
        for (auto& e : s.tag_field_values)
            if (tag_in_category(e.tag.view(), category) &&
                category_name_eq(e.field.view(), old_field))
                e.field = new_field;
        return true;
    }
}

bool remove_template_field(VaultSettings& s, std::string_view category,
                           std::string_view field)
{
    if (TagCategory* c = find_category(s, category); !c) {
        return false;
    } else {
        if (const auto removed = std::erase_if(c->fields,
                                               [field](const crypto::SecureString& f) {
                                                   return category_name_eq(f.view(), field);
                                               });
            removed == 0) {
            return false;
        }
        std::erase_if(s.tag_field_values, [&category, &field](const TagFieldValue& e) {
            return tag_in_category(e.tag.view(), category) &&
                   category_name_eq(e.field.view(), field);
        });
        return true;
    }
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

template <typename Alloc>
void serialize_index_to(const IndexNode& root, const std::vector<SavedSearch>& searches,
                        const VaultSettings& settings, std::vector<uint8_t, Alloc>& out)
{
    out.clear();
    ByteWriter w(out);
    w.u8(INDEX_VERSION);
    write_node(w, root);
    write_saved_searches(w, searches);
    write_settings(w, settings);
}

void serialize_index(const IndexNode& root, const std::vector<SavedSearch>& searches,
                     const VaultSettings& settings, std::vector<uint8_t>& out)
{
    serialize_index_to(root, searches, settings, out);
}

void serialize_index(const IndexNode& root, const std::vector<SavedSearch>& searches,
                     const VaultSettings& settings, crypto::WipingBytes& out)
{
    serialize_index_to(root, searches, settings, out);
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
        if (!read_settings(r, settings, version)) return false;
    } else {
        settings = VaultSettings::seeded();
    }
    // Trailing bytes after a well-formed blob indicate corruption.
    return r.remaining() == 0;
}

bool tag_ci_equal(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        auto to_lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
        if (to_lower(a[i]) != to_lower(b[i])) return false;
    }
    return true;
}

} // namespace vault
