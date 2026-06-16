#pragma once

// In-memory gallery tree + its binary serialisation.
//
// The vault's directory structure is a tree of IndexNodes. A node is either a
// Gallery (named, holding child nodes) or an Image (named, holding the metadata
// and the data/thumbnail chunk locations). Per CLAUDE.md a gallery holds either
// sub-galleries OR images, never a mix; that invariant is enforced by the Vault
// layer, not here — the tree itself can represent any shape.
//
// The serialised form is a hand-rolled, versioned little-endian blob (see the
// node layout in CLAUDE.md / ROADMAP.md). Deserialisation is fully bounds-checked
// and depth-limited so a corrupt or hostile blob can never crash the parser.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vault {

// On-disk image format tag. Values 0-8 match the spec; Unknown is used in Phase 2
// where images are stored without decoding (real format filled in Phase 3).
enum class ImageFormat : uint8_t {
    JPEG    = 0,
    PNG     = 1,
    GIF     = 2,
    BMP     = 3,
    TGA     = 4,
    HDR     = 5,
    WebP    = 6,
    HEIC    = 7,
    AVIF    = 8,
    Unknown = 0xFF,
};

// Metadata + chunk locations for one stored image. A thumb_length of 0 means no
// thumbnail is stored yet (Phase 2 stores images without thumbnails; Phase 3
// wires thumbnail generation into Vault::add_image).
struct ImageMeta {
    ImageFormat format       = ImageFormat::Unknown;
    uint32_t    width        = 0;
    uint32_t    height       = 0;
    uint64_t    orig_size    = 0;  // plaintext bytes of the original image
    uint64_t    created_ts   = 0;  // Unix seconds
    uint64_t    data_offset  = 0;  // chunk start in the data region
    uint64_t    data_length  = 0;  // on-disk chunk length (nonce|cipher|tag)
    uint64_t    thumb_offset = 0;
    uint64_t    thumb_length = 0;
};

struct IndexNode {
    enum class Type : uint8_t { Gallery = 0, Image = 1 };

    Type                       type = Type::Gallery;
    std::string                name;
    std::vector<std::string>   tags;  // per-node tags (Phase 12)
    bool                       favorite = false;  // bookmark flag (Phase 13)

    // Gallery payload (meaningful when type == Gallery).
    std::vector<IndexNode> children;

    // Image payload (meaningful when type == Image).
    ImageMeta meta{};

    [[nodiscard]] bool is_gallery() const noexcept { return type == Type::Gallery; }
    [[nodiscard]] bool is_image()   const noexcept { return type == Type::Image; }

    static IndexNode gallery(std::string name)
    {
        IndexNode n;
        n.type = Type::Gallery;
        n.name = std::move(name);
        return n;
    }

    static IndexNode image(std::string name)
    {
        IndexNode n;
        n.type = Type::Image;
        n.name = std::move(name);
        return n;
    }
};

// Current serialised-blob version (first byte of the blob).
// v1: no tags. v2: per-node tags (Phase 12). v3: per-node favorite flag (Phase 13).
inline constexpr uint8_t INDEX_VERSION = 3;

// Maximum tree depth accepted on deserialisation — guards against stack overflow
// from a deeply-nested hostile blob.
inline constexpr uint32_t INDEX_MAX_DEPTH = 128;

// Maximum tags per node — prevents OOM/DoS from a malicious tag_count (Phase 12).
inline constexpr uint16_t INDEX_MAX_TAGS = 4096;

// Serialise the whole tree rooted at `root` into `out` (cleared first).
void serialize_index(const IndexNode& root, std::vector<uint8_t>& out);

// Parse a tree from `in` into `out`. Returns false on any malformed input (bad
// version, unknown node type, truncation, excessive depth). On failure `out` is
// left in an unspecified-but-valid state; callers must not use it.
[[nodiscard]] bool deserialize_index(std::span<const uint8_t> in, IndexNode& out);

} // namespace vault
