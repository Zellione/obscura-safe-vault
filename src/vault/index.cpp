#include "index.h"

#include "byte_io.h"

namespace vault {

namespace {

void write_node(ByteWriter& w, const IndexNode& node)
{
    w.u8(static_cast<uint8_t>(node.type));

    // name_len (u16) + name bytes (UTF-8). Names longer than 65535 bytes are
    // clamped — far beyond any real filename or gallery name.
    const std::string& name = node.name;
    const uint16_t name_len = name.size() > 0xFFFF ? 0xFFFF
                                                   : static_cast<uint16_t>(name.size());
    w.u16(name_len);
    w.bytes(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(name.data()), name_len));

    if (node.type == IndexNode::Type::Gallery) {
        w.u32(static_cast<uint32_t>(node.children.size()));
        for (const auto& child : node.children) write_node(w, child);
    } else {
        const ImageMeta& m = node.meta;
        w.u8(static_cast<uint8_t>(m.format));
        w.u32(m.width);
        w.u32(m.height);
        w.u64(m.orig_size);
        w.u64(m.created_ts);
        w.u64(m.data_offset);
        w.u64(m.data_length);
        w.u64(m.thumb_offset);
        w.u64(m.thumb_length);
    }
}

// Returns false on malformed input. Depth-limited to guard against stack
// overflow from a deeply nested blob.
bool read_node(ByteReader& r, IndexNode& node, uint32_t depth)
{
    if (depth > INDEX_MAX_DEPTH) return false;

    const uint8_t type = r.u8();
    if (!r.ok()) return false;
    if (type != static_cast<uint8_t>(IndexNode::Type::Gallery) &&
        type != static_cast<uint8_t>(IndexNode::Type::Image)) {
        return false;  // unknown node type
    }
    node.type = static_cast<IndexNode::Type>(type);

    const uint16_t name_len = r.u16();
    if (!r.ok()) return false;
    node.name.resize(name_len);
    if (name_len > 0) {
        r.bytes(std::span<uint8_t>(reinterpret_cast<uint8_t*>(node.name.data()), name_len));
        if (!r.ok()) return false;
    }

    if (node.type == IndexNode::Type::Gallery) {
        const uint32_t child_count = r.u32();
        if (!r.ok()) return false;
        node.children.clear();
        for (uint32_t i = 0; i < child_count; ++i) {
            if (!r.ok()) return false;  // bail early — don't spin on a bogus count
            IndexNode child;
            if (!read_node(r, child, depth + 1)) return false;
            node.children.push_back(std::move(child));
        }
    } else {
        ImageMeta& m = node.meta;
        m.format       = static_cast<ImageFormat>(r.u8());
        m.width        = r.u32();
        m.height       = r.u32();
        m.orig_size    = r.u64();
        m.created_ts   = r.u64();
        m.data_offset  = r.u64();
        m.data_length  = r.u64();
        m.thumb_offset = r.u64();
        m.thumb_length = r.u64();
        if (!r.ok()) return false;
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
    if (!r.ok() || version != INDEX_VERSION) return false;
    if (!read_node(r, out, 0)) return false;
    // Trailing bytes after a well-formed tree indicate corruption.
    return r.remaining() == 0;
}

} // namespace vault
