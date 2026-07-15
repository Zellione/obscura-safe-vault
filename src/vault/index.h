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

// Video codec types (Phase 15 PR2; ProRes/DNxHD/MJPEG Phase 28; VP8/VP9
// Phase 38). Stored as a raw u8 in the index, so new values are read back by
// older builds as an unnamed enum value and displayed as plain "Video" —
// never a format break.
enum class VideoCodec : uint8_t {
    H264    = 0,
    HEVC    = 1,
    ProRes  = 2,   // Apple ProRes (all profiles)
    DNxHD   = 3,   // Avid DNxHD/DNxHR (one FFmpeg codec id)
    MJPEG   = 4,   // Motion JPEG
    VP8     = 5,   // WebM (Phase 38)
    VP9     = 6,   // WebM (Phase 38)
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

// Per-gallery children display order (Phase 37). Persisted on Gallery nodes
// only — Image/Video nodes carry the field too (serialized uniformly, see
// index.cpp) but never read it.
enum class SortKey : uint8_t {
    Manual   = 0,   // raw insertion order — today's behavior, the default
    NameAsc  = 1,   // natural (number-aware) ascending, via ui::natural_less
    NameDesc = 2,
    DateAsc  = 3,   // by created_ts ascending
    DateDesc = 4,
    SizeAsc  = 5,   // by orig_size ascending
    SizeDesc = 6,
};

struct IndexNode {
    enum class Type : uint8_t { Gallery = 0, Image = 1, Video = 2 };

    Type                       type = Type::Gallery;
    std::string                name;
    std::vector<std::string>   tags;  // per-node tags (Phase 12)
    bool                       favorite = false;  // bookmark flag (Phase 13)
    SortKey                    sort_key = SortKey::Manual;  // children order (Phase 37); meaningful only when is_gallery()

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

// One named, reusable saved search (Phase 18). Stored as vault-global metadata
// alongside the tree root, not as a node. `query` is the opaque serialised
// ui::AdvancedQuery blob (see src/ui/advanced_search_model.h); the index treats
// it as bytes so the vault layer stays decoupled from the UI query model.
struct SavedSearch {
    std::string          name;
    std::vector<uint8_t> query;
};

// Current serialised-blob version (first byte of the blob).
// v1: no tags. v2: per-node tags (Phase 12). v3: per-node favorite flag (Phase 13).
// v4: video nodes + VideoMeta (Phase 15 PR2). v5: vault-global saved-searches
// block after the tree root (Phase 18); pre-v5 blobs read with an empty list.
// v6: per-gallery sort_key (Phase 37); pre-v6 blobs read every node as Manual.
inline constexpr uint8_t INDEX_VERSION = 6;

// Maximum tree depth accepted on deserialisation — guards against stack overflow
// from a deeply-nested hostile blob.
inline constexpr uint32_t INDEX_MAX_DEPTH = 128;

// Maximum tags per node — prevents OOM/DoS from a malicious tag_count (Phase 12).
inline constexpr uint16_t INDEX_MAX_TAGS = 4096;

// Maximum chunks in a video — prevents OOM/DoS from a malicious chunk_count
// (Phase 15 PR2). 1 MiB chunks * 1M = 1 TiB max video.
inline constexpr uint32_t INDEX_MAX_VIDEO_CHUNKS = 1u << 20;

// Maximum saved searches per vault, and max bytes per saved query blob —
// bound a hostile saved-searches block (Phase 18).
inline constexpr uint16_t INDEX_MAX_SAVED_SEARCHES   = 4096;
inline constexpr uint32_t INDEX_MAX_SAVED_QUERY_BYTES = 1u << 20;

// Serialise the whole tree rooted at `root` into `out` (cleared first). The
// two-argument form writes an empty saved-searches block; the three-argument
// form persists `searches` after the tree (Phase 18).
void serialize_index(const IndexNode& root, std::vector<uint8_t>& out);
void serialize_index(const IndexNode& root, const std::vector<SavedSearch>& searches,
                     std::vector<uint8_t>& out);

// Parse a tree from `in` into `out`. Returns false on any malformed input (bad
// version, unknown node type, truncation, excessive depth). On failure `out` is
// left in an unspecified-but-valid state; callers must not use it. The
// three-argument form also extracts the saved-searches block into `searches`
// (empty for pre-v5 blobs).
[[nodiscard]] bool deserialize_index(std::span<const uint8_t> in, IndexNode& out);
[[nodiscard]] bool deserialize_index(std::span<const uint8_t> in, IndexNode& out,
                                     std::vector<SavedSearch>& searches);

} // namespace vault
