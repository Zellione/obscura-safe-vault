#include "index.h"

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

void read_image_meta(ByteReader& r, ImageMeta& m)
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

    if (node.type == IndexNode::Type::Image) {
        read_image_meta(r, node.meta);
        return r.ok();
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

} // namespace

void serialize_index(const IndexNode& root, std::vector<uint8_t>& out)
{
    out.clear();
    ByteWriter w(out);
    w.u8(INDEX_VERSION);
    write_node(w, root);
}

bool deserialize_index(std::span<const uint8_t> in, IndexNode& out)
{
    ByteReader r(in);
    const uint8_t version = r.u8();
    if (!r.ok()) return false;
    // Accept version 1 (no tags) and version 2 (with tags). Reject unknown versions.
    if (version < 1 || version > INDEX_VERSION) return false;
    if (!read_node(r, out, 0, version)) return false;
    // Trailing bytes after a well-formed tree indicate corruption.
    return r.remaining() == 0;
}

} // namespace vault
