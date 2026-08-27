#pragma once

// In-memory gallery tree + its binary serialisation.
//
// The vault's directory structure is a tree of IndexNodes. A node is either a
// Gallery (named, holding child nodes) or an Image (named, holding the metadata
// and the data/thumbnail chunk locations). Per AGENTS.md a gallery holds either
// sub-galleries OR images, never a mix; that invariant is enforced by the Vault
// layer, not here — the tree itself can represent any shape.
//
// The serialised form is a hand-rolled, versioned little-endian blob (see the
// node layout in AGENTS.md / ROADMAP.md). Deserialisation is fully bounds-checked
// and depth-limited so a corrupt or hostile blob can never crash the parser.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crypto/secure_string.h"

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

// Formats whose files can carry an animation: GIF (Phase 47) and WebP
// (Phase 57). The single source of truth for the import flag, the lazy repair,
// the sniff gate, the "A" badge and the hover gate — so they cannot drift apart.
// Anything else stores animated = false and never badges, whatever its flag
// happens to say.
[[nodiscard]] constexpr bool format_can_animate(ImageFormat f) noexcept
{
    return f == ImageFormat::GIF || f == ImageFormat::WebP;
}

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
    bool        animated     = false; // multi-frame GIF (Phase 47)
};

// Metadata for one encrypted video chunk (Phase 15 PR2).
struct VideoChunk {
    uint64_t offset = 0;  // location in data region
    uint64_t length = 0;  // on-disk chunk length (nonce|cipher|tag)
};

// Video container formats (Phase 15 PR2; legacy containers Phase 52).
enum class VideoContainer : uint8_t {
    MP4     = 0,
    MKV     = 1,
    AVI     = 2,   // Phase 52
    MPEGPS  = 3,   // Phase 52 (.mpg/.mpeg)
    MPEGTS  = 4,   // Phase 52 (.ts/.m2ts)
    ASF     = 5,   // Phase 52 (.wmv/.asf)
    FLV     = 6,   // Phase 52
    OGG     = 7,   // Phase 52 (.ogv)
    RM      = 8,   // Phase 52 (.rm/.rmvb)
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
    AV1     = 7,   // WebM/.mov (Phase 40)
    QTRLE   = 8,   // .mov, QuickTime Animation/RLE (Phase 40)
    Cinepak = 9,   // .mov, legacy (Phase 40)
    // Phase 52 — legacy long tail
    MPEG1     = 10,
    MPEG2     = 11,
    MPEG4     = 12,   // ASP: DivX/Xvid
    MSMPEG4V1 = 13,
    MSMPEG4V2 = 14,
    MSMPEG4V3 = 15,   // DivX 3
    WMV1      = 16,
    WMV2      = 17,
    WMV3      = 18,
    VC1       = 19,
    H263      = 20,
    FLV1      = 21,   // Sorenson Spark
    VP6       = 22,
    VP6A      = 23,
    VP6F      = 24,
    SVQ1      = 25,
    SVQ3      = 26,
    DV        = 27,
    MSVideo1  = 28,
    RPZA      = 29,
    HuffYUV   = 30,
    FFV1      = 31,
    Theora    = 32,
    RV10      = 33,
    RV20      = 34,
    RV30      = 35,
    RV40      = 36,
    Unknown   = 0xFF,
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
// Per-gallery child ordering (Phase 37; Phase 49 reworked value 0).
//
// `Default` means "follow the vault-wide default_sort" (vault::VaultSettings) —
// it is NOT an order in itself, and never reaches sort_children(); callers
// resolve it first via ui::effective_sort_key(). `Insertion` is raw import
// order, which is what value 0 used to mean before Phase 49. Existing vaults
// are all byte 0, so they adopt the vault default with no migration.
enum class SortKey : uint8_t {
    Default   = 0,   // follow the vault's default_sort (Phase 49)
    NameAsc   = 1,   // natural (number-aware) ascending, via ui::natural_less
    NameDesc  = 2,
    DateAsc   = 3,   // by created_ts ascending
    DateDesc  = 4,
    SizeAsc   = 5,   // by orig_size ascending
    SizeDesc  = 6,
    Insertion = 7,   // raw insertion order (Phase 49; pre-49 meaning of 0)
};

struct IndexNode {
    enum class Type : uint8_t { Gallery = 0, Image = 1, Video = 2 };

    Type type = Type::Gallery;
    crypto::SecureString name;               // Phase 91: mlock'd + wiped
    std::vector<crypto::SecureString> tags;  // per-node tags (Phase 12; secure since 91)
    bool favorite = false;                   // bookmark flag (Phase 13)
    SortKey sort_key =
        SortKey::Default;  // children order (Phase 37); meaningful only when is_gallery()

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
    crypto::SecureString name;   // Phase 91: mlock'd + wiped
    std::vector<uint8_t> query;  // opaque serialised ui::AdvancedQuery blob
};

// One tag-category → colour-swatch mapping (Phase 49). `name` is the tag prefix
// before the first ':' ("artist" for "artist:Kaguya"), matched case-insensitively;
// `swatch` indexes gfx's fixed 16-colour table. Vault-global metadata, stored
// inside the .osv because a vault's categories describe its contents.
struct TagCategory {
    crypto::SecureString name;  // Phase 91: mlock'd + wiped
    uint8_t swatch = 0;
    // Phase 73: the category's optional template — ordered field names, empty =
    // no template. When a brand-new tag of this category is added, the UI
    // prompts for one value per field (skippable).
    std::vector<crypto::SecureString> fields;

    friend bool operator==(const TagCategory&, const TagCategory&) = default;
};

// One tag → free-text description (Phase 51). Vault-global metadata: a
// description belongs to the *tag*, not to any node carrying it, and tags are
// plain per-node strings with no registry — so storing it here is the only
// place it cannot go inconsistent between carriers.
struct TagDescription {
    crypto::SecureString tag;  // Phase 91: mlock'd + wiped
    crypto::SecureString text;

    friend bool operator==(const TagDescription&, const TagDescription&) = default;
};

// One (tag, field) → value entry (Phase 73). Vault-global like TagDescription:
// a field value belongs to the *tag* (via its category's template), never to a
// node carrying it. `field` names a field of the tag's category template; the
// pair (tag, field) is matched case-insensitively, first-seen casing kept.
struct TagFieldValue {
    crypto::SecureString tag;  // Phase 91: mlock'd + wiped
    crypto::SecureString field;
    crypto::SecureString value;

    friend bool operator==(const TagFieldValue&, const TagFieldValue&) = default;
};

// Vault-global user settings (Phase 49), serialised after the saved-searches
// block. Defaults here are also the values a pre-v8 blob reads back with, except
// `categories`, which pre-v8 blobs get from seeded() — see deserialize_index.
struct VaultSettings {
    SortKey                    default_sort    = SortKey::Insertion;
    bool                       tiles_show_tags = true;
    std::vector<TagCategory>   categories;
    std::vector<TagDescription> tag_descriptions;
    std::vector<TagFieldValue> tag_field_values;   // Phase 73

    // Phase 65: what content backfills have been run against this vault.
    // 0/0 is the correct reading for every pre-v10 blob — such a vault has
    // never been migrated. See docs/roadmap/phase-65-blocking-migration.md.
    uint8_t  migrated_index_version = 0;
    uint16_t migrated_probe_caps    = 0;

    // Phase 75: stored-thumbnail budget (max long side, px) this vault's thumbs
    // and posters were last generated at. 0 = legacy (pre-v12 blob or never
    // regenerated) — compared against image::THUMB_MAX_SIDE by the caller.
    uint16_t migrated_thumb_side = 0;

    // The starting category set for a vault that has never stored one: a freshly
    // created vault and every pre-v8 vault. An empty SAVED list is a legitimate
    // state and is never re-seeded.
    [[nodiscard]] static VaultSettings seeded();
};

// Current serialised-blob version (first byte of the blob).
// v1: no tags. v2: per-node tags (Phase 12). v3: per-node favorite flag (Phase 13).
// v4: video nodes + VideoMeta (Phase 15 PR2). v5: vault-global saved-searches
// block after the tree root (Phase 18); pre-v5 blobs read with an empty list.
// v6: per-gallery sort_key (Phase 37); pre-v6 blobs read every node as Manual.
// v7: per-image `animated` flag (Phase 47); pre-v7 blobs read every image as
// not animated, and are fixed during vault migration (Phase 65).
// v8: vault-global settings block after the saved-searches block, and sort_key
// byte 0 re-read as `Default` with a new `Insertion = 7` (Phase 49); pre-v8
// blobs read with the seeded default settings and every gallery at `Default`.
// v9: per-tag descriptions appended to the vault-global settings block
// (Phase 51); pre-v9 blobs read with an empty list.
// v10: vault-global migration watermark appended to the settings block
// (Phase 65: migrated_index_version u8, migrated_probe_caps u16); pre-v10
// blobs read 0/0 and are therefore treated as never migrated.
// v11: category template fields appended to each category entry, and a
// vault-global tag-field-values block appended after the migration watermark
// (Phase 73); pre-v11 blobs read with no templates and no values.
// v12: migrated_thumb_side u16 appended after the tag-field-values block
// (Phase 75); pre-v12 blobs read 0 (thumbnails never regenerated).
inline constexpr uint8_t INDEX_VERSION = 12;

// Phase 65: the index version whose content backfills the migration performs.
// Bump ONLY when a NEW index version adds a field needing a content backfill —
// NOT on every INDEX_VERSION bump, or unrelated format changes would re-offer a
// migration that has nothing to do with them. Currently 7: the version that
// introduced ImageMeta::animated.
inline constexpr uint8_t MIGRATION_INDEX_VERSION = 7;

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

// Maximum tag categories per vault, and max bytes in one category name —
// bound a hostile settings block (Phase 49).
inline constexpr uint16_t INDEX_MAX_TAG_CATEGORIES = 256;
inline constexpr uint16_t INDEX_MAX_CATEGORY_BYTES = 64;

// Maximum tag descriptions per vault, and max bytes in one description —
// bound a hostile settings block (Phase 51). The tag KEY is deliberately
// uncapped beyond its u16 length prefix, matching how node tags are bounded.
inline constexpr uint16_t INDEX_MAX_TAG_DESCRIPTIONS = 4096;
inline constexpr uint16_t INDEX_MAX_TAG_DESC_BYTES   = 512;

// Maximum template fields per category, bytes per field name, bytes per stored
// value, and total stored (tag, field, value) entries — bound a hostile
// settings block (Phase 73).
inline constexpr uint8_t  INDEX_MAX_TEMPLATE_FIELDS   = 16;
inline constexpr uint16_t INDEX_MAX_FIELD_BYTES       = 64;
inline constexpr uint16_t INDEX_MAX_FIELD_VALUE_BYTES = 256;
inline constexpr uint16_t INDEX_MAX_TAG_FIELD_VALUES  = 16384;

// Size of gfx's fixed tag-colour palette. Lives here (not in gfx) because it
// bounds a persisted field: a swatch byte >= this is rejected on deserialise.
inline constexpr uint8_t TAG_SWATCH_COUNT = 16;

// Serialise the whole tree rooted at `root` into `out` (cleared first). The
// two-argument form writes an empty saved-searches block; the three-argument
// form persists `searches` after the tree (Phase 18). The four-argument form
// (Phase 49) also persists the vault-global settings block.
void serialize_index(const IndexNode& root, std::vector<uint8_t>& out);
void serialize_index(const IndexNode& root, const std::vector<SavedSearch>& searches,
                     std::vector<uint8_t>& out);
void serialize_index(const IndexNode& root, const std::vector<SavedSearch>& searches,
                     const VaultSettings& settings, std::vector<uint8_t>& out);

// Parse a tree from `in` into `out`. Returns false on any malformed input (bad
// version, unknown node type, truncation, excessive depth). On failure `out` is
// left in an unspecified-but-valid state; callers must not use it. The
// three-argument form also extracts the saved-searches block into `searches`
// (empty for pre-v5 blobs). The four-argument form (Phase 49) also extracts
// the vault-global settings block.
[[nodiscard]] bool deserialize_index(std::span<const uint8_t> in, IndexNode& out);
[[nodiscard]] bool deserialize_index(std::span<const uint8_t> in, IndexNode& out,
                                     std::vector<SavedSearch>& searches);
[[nodiscard]] bool deserialize_index(std::span<const uint8_t> in, IndexNode& out,
                                     std::vector<SavedSearch>& searches,
                                     VaultSettings& settings);

// Look up a tag's description, matched case-insensitively (ASCII fold — the same
// identity rule tag de-duplication uses). Returns an empty view when absent.
// The returned view BORROWS from `s` — never call this on a temporary.
[[nodiscard]] std::string_view find_tag_description(const VaultSettings& s,
                                                    std::string_view     tag);

// Upsert a tag's description, matched case-insensitively; the first-seen casing
// of the key is kept, the text is replaced. An empty `text` REMOVES the entry
// rather than storing a blank, so the map never accumulates dead keys. A new key
// is dropped silently once INDEX_MAX_TAG_DESCRIPTIONS is reached (an existing
// key is always still updatable). Pure struct mutation — the caller persists via
// set_vault_settings().
void set_tag_description(VaultSettings& s, std::string_view tag, std::string_view text);

// --- Phase 73: category templates + per-tag field values --------------------
// All pure struct operations; the caller persists via set_vault_settings().

// The prefix before the first ':' when both prefix and suffix are non-empty
// (the same split rule ui::resolve_tag uses), else empty.
[[nodiscard]] std::string_view tag_category_prefix(std::string_view tag);

// `category`'s template fields. Empty when the category is absent or has no
// template. The returned span BORROWS from `s` — never call on a temporary.
[[nodiscard]] std::span<const crypto::SecureString> category_template(const VaultSettings& s,
                                                                      std::string_view category);

// Replace `category`'s template. False when the category does not exist.
// Fields are ci de-duped keeping first casing, clamped to
// INDEX_MAX_TEMPLATE_FIELDS entries; over-long names are truncated to
// INDEX_MAX_FIELD_BYTES.
bool set_category_template(VaultSettings& s, std::string_view category,
                           std::vector<std::string> fields);

// Look up / upsert one (tag, field) value, both keys matched ci. Empty when
// absent; the returned view BORROWS from `s`. An empty `value` REMOVES the
// entry; a new entry is dropped silently at INDEX_MAX_TAG_FIELD_VALUES.
[[nodiscard]] std::string_view find_tag_field_value(const VaultSettings& s,
                                                    std::string_view tag,
                                                    std::string_view field);
void set_tag_field_value(VaultSettings& s, std::string_view tag,
                         std::string_view field, std::string_view value);

// Rename `old_field` in `category`'s template and re-key every stored value
// whose tag belongs to `category` (by prefix). False when old_field is absent,
// new_field already exists (ci), or new_field is empty/over-cap.
bool rename_template_field(VaultSettings& s, std::string_view category,
                           std::string_view old_field, std::string_view new_field);

// Remove `field` from `category`'s template and erase its stored values for
// that category's tags. False when the field is absent.
bool remove_template_field(VaultSettings& s, std::string_view category,
                           std::string_view field);

// ASCII case-insensitive tag identity — the single definition of "same tag"
// inside vault/ (ui::tag_ci_equal mirrors it for the UI layer, which may not
// depend on it going the other way).
[[nodiscard]] bool tag_ci_equal(std::string_view a, std::string_view b) noexcept;

} // namespace vault
