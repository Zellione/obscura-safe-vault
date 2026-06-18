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

// Metadata for one encrypted video chunk (Phase 15 PR2).
struct VideoChunk {
    uint64_t offset = 0;  // location in data region
    uint64_t length = 0;  // on-disk chunk length (nonce|cipher|tag)
};

// Video container formats (Phase 15 PR2).
enum class VideoContainer : uint8_t {
    MP4     = 0,
    MKV     = 1,
    Unknown = 0xFF,
};

// Video codec types (Phase 15 PR2).
enum class VideoCodec : uint8_t {
    H264    = 0,
    HEVC    = 1,
    Unknown = 0xFF,
};

// Metadata + chunk locations for one stored video (Phase 15 PR2).
struct VideoMeta {
    VideoContainer         container      = VideoContainer::Unknown;
    VideoCodec             codec          = VideoCodec::Unknown;
    uint32_t               width          = 0;   // resolution; 0 until decoder fills it
    uint32_t               height         = 0;
    uint64_t               duration_us    = 0;   // microseconds
    uint64_t               orig_size      = 0;   // plaintext container bytes (sum of chunks)
    uint64_t               created_ts     = 0;   // Unix seconds
    uint32_t               chunk_size     = 0;   // plaintext split size; last chunk may be smaller
    std::vector<VideoChunk> chunks;              // ordered encrypted-chunk locations
    uint64_t               poster_offset  = 0;   // first-frame JPEG poster (Phase 15 PR4); 0 length = none
    uint64_t               poster_length  = 0;
};

struct IndexNode {
    enum class Type : uint8_t { Gallery = 0, Image = 1, Video = 2 };

    Type                       type = Type::Gallery;
    std::string                name;
    std::vector<std::string>   tags;  // per-node tags (Phase 12)
    bool                       favorite = false;  // bookmark flag (Phase 13)

    // Gallery payload (meaningful when type == Gallery).
    std::vector<IndexNode> children;

    // Image payload (meaningful when type == Image).
    ImageMeta meta{};

    // Video payload (meaningful when type == Video) (Phase 15 PR2).
    VideoMeta vmeta{};

    [[nodiscard]] bool is_gallery() const noexcept { return type == Type::Gallery; }
    [[nodiscard]] bool is_image()   const noexcept { return type == Type::Image; }
    [[nodiscard]] bool is_video()   const noexcept { return type == Type::Video; }
    [[nodiscard]] bool is_media()   const noexcept { return is_image() || is_video(); }

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

    static IndexNode video(std::string name)
    {
        IndexNode n;
        n.type = Type::Video;
        n.name = std::move(name);
        return n;
    }
};

// Current serialised-blob version (first byte of the blob).
// v1: no tags. v2: per-node tags (Phase 12). v3: per-node favorite flag (Phase 13).
// v4: video nodes + VideoMeta (Phase 15 PR2).
inline constexpr uint8_t INDEX_VERSION = 4;

// Maximum tree depth accepted on deserialisation — guards against stack overflow
// from a deeply-nested hostile blob.
inline constexpr uint32_t INDEX_MAX_DEPTH = 128;

// Maximum tags per node — prevents OOM/DoS from a malicious tag_count (Phase 12).
inline constexpr uint16_t INDEX_MAX_TAGS = 4096;

// Maximum chunks in a video — prevents OOM/DoS from a malicious chunk_count
// (Phase 15 PR2). 1 MiB chunks * 1M = 1 TiB max video.
inline constexpr uint32_t INDEX_MAX_VIDEO_CHUNKS = 1u << 20;

// Serialise the whole tree rooted at `root` into `out` (cleared first).
void serialize_index(const IndexNode& root, std::vector<uint8_t>& out);

// Parse a tree from `in` into `out`. Returns false on any malformed input (bad
// version, unknown node type, truncation, excessive depth). On failure `out` is
// left in an unspecified-but-valid state; callers must not use it.
[[nodiscard]] bool deserialize_index(std::span<const uint8_t> in, IndexNode& out);

} // namespace vault
