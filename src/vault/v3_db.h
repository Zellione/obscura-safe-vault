#pragma once

#include "crypto/crypto_sizes.h"
#include "crypto/secure_string.h"
#include "vault/index.h"
#include "vault/v3_crypto_spec.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

struct sqlite3;

namespace vault::v3 {

class Database;
class VaultRoot;

enum class DbStatus {
    Ok,
    InvalidArgument,
    IoError,
    WrongKeyOrCorrupt,
    Busy,
    DiskFull,
    Constraint,
    UnsupportedVersion,
};

enum class NodeType : uint8_t { Gallery = 0, Image = 1, Video = 2 };

struct NodeRecord {
    Id node_id{};
    std::optional<Id> parent_id;
    NodeType type = NodeType::Gallery;
    crypto::SecureString display_name;
    uint64_t sibling_order = 0;
    bool favorite = false;
    uint64_t created_ts = 0;
    std::optional<int> media_format;
    std::optional<uint32_t> width;
    std::optional<uint32_t> height;
    std::optional<uint64_t> duration_ms;
    std::optional<int> codec;
    std::optional<uint64_t> original_size;
    std::optional<uint8_t> sort_key;
    std::optional<bool> animated;
};

struct ObjectRecord {
    ObjectRecord() = default;
    ObjectRecord(Id object, Id node, ObjectRole object_role,  // NOSONAR cpp:S107 -- fixed DB row
                 uint64_t encrypted,  // NOSONAR cpp:S107 -- mirrors the fixed objects row
                 uint64_t plaintext, uint32_t frame_limit, uint32_t frames,
                 uint64_t generation) noexcept
        : object_id(object), node_id(node), role(object_role), encrypted_length(encrypted),
          plaintext_length(plaintext), frame_plain_limit(frame_limit), frame_count(frames),
          creation_generation(generation)
    {}
    Id object_id{};
    Id node_id{};
    ObjectRole role = ObjectRole::OriginalImage;
    uint64_t encrypted_length = 0;
    uint64_t plaintext_length = 0;
    uint32_t frame_plain_limit = 0;
    uint32_t frame_count = 0;
    uint64_t creation_generation = 0;
};

struct SettingsRecord {
    uint64_t app_generation = 0;
    uint8_t default_sort = 7;
    bool tiles_show_tags = true;
    uint8_t migrated_index_version = 0;
    uint16_t migrated_probe_caps = 0;
    uint16_t migrated_thumb_side = 0;
};

struct SavedSearchRecord {
    int64_t search_id = 0;
    crypto::SecureString display_name;
    crypto::SecureBlob query;
};

struct ConversionMarker {
    std::array<uint8_t, 32> source_fingerprint{};
    uint32_t conversion_version = 0;
};

struct TagCategoryRecord {
    int64_t category_id = 0;
    crypto::SecureString display_name;
    uint8_t swatch = 0;
    std::vector<crypto::SecureString> fields;
};

struct TagFieldValueRecord {
    int64_t tag_id = 0;
    int64_t category_id = 0;
    uint32_t field_order = 0;
    crypto::SecureString value;
};

struct TagDescriptionRecord {
    crypto::SecureString tag;
    crypto::SecureString description;
};

struct ResolvedTagFieldValueRecord {
    crypto::SecureString tag;
    crypto::SecureString field;
    crypto::SecureString value;
};

template <typename T> struct DbResult {
    DbStatus status = DbStatus::IoError;
    T value{};
};

class Database {  // NOSONAR cpp:S1448 -- typed repositories intentionally share one SQLCipher
                  // handle
public:
    struct OpenResult;

    Database() = default;
    ~Database();
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    [[nodiscard]] static OpenResult create(const std::filesystem::path& path,
                                           std::span<const uint8_t, crypto::KEY_SIZE> database_key,
                                           const Id& root_node_id) noexcept;
    [[nodiscard]] static OpenResult
    create_in_root(const VaultRoot& root, std::span<const uint8_t, crypto::KEY_SIZE> database_key,
                   const Id& root_node_id) noexcept;
    [[nodiscard]] static OpenResult open(const std::filesystem::path& path,
                                         std::span<const uint8_t, crypto::KEY_SIZE> database_key,
                                         bool writable) noexcept;
    [[nodiscard]] static OpenResult
    open_read_only(const VaultRoot& root,
                   std::span<const uint8_t, crypto::KEY_SIZE> database_key) noexcept;
    [[nodiscard]] static OpenResult
    open_writable(const VaultRoot& root,
                  std::span<const uint8_t, crypto::KEY_SIZE> database_key) noexcept;

    [[nodiscard]] DbStatus insert_node(const NodeRecord& node) noexcept;
    [[nodiscard]] DbResult<std::vector<NodeRecord>>
    list_children(const Id& parent_id) const noexcept;
    [[nodiscard]] DbResult<std::optional<NodeRecord>> find_node(const Id& node_id) const noexcept;
    [[nodiscard]] DbResult<std::vector<NodeRecord>>
    search_nodes(std::string_view term) const noexcept;
    [[nodiscard]] DbStatus insert_object(const ObjectRecord& object) noexcept;
    [[nodiscard]] DbResult<std::vector<ObjectRecord>> object_references() const noexcept;
    // Phase 109 mutation primitives. The object reference and generation change are
    // committed in one SQL transaction; callers must publish the immutable file first.
    [[nodiscard]] DbStatus commit_object_create(ObjectRecord object) noexcept;
    [[nodiscard]] DbResult<std::optional<Id>> commit_object_replace(ObjectRecord object) noexcept;
    [[nodiscard]] DbResult<std::optional<Id>> commit_object_delete(const Id& node_id,
                                                                   ObjectRole role) noexcept;
    [[nodiscard]] DbResult<bool> object_is_referenced(const Id& object_id) const noexcept;
    [[nodiscard]] DbStatus add_tag(int64_t tag_id, std::string_view display_name,
                                   std::string_view canonical_name) noexcept;
    [[nodiscard]] DbStatus assign_tag(const Id& node_id, int64_t tag_id) noexcept;
    [[nodiscard]] DbResult<std::vector<crypto::SecureString>>
    node_tags(const Id& node_id) const noexcept;
    [[nodiscard]] DbStatus add_tag_category(const TagCategoryRecord& category) noexcept;
    [[nodiscard]] DbResult<std::vector<TagCategoryRecord>> tag_categories() const noexcept;
    [[nodiscard]] DbStatus set_tag_field_value(const TagFieldValueRecord& value) noexcept;
    [[nodiscard]] DbResult<std::vector<TagFieldValueRecord>>
    tag_field_values(int64_t tag_id) const noexcept;
    [[nodiscard]] DbStatus set_tag_description(int64_t tag_id,
                                               std::string_view description) noexcept;
    [[nodiscard]] DbResult<std::optional<crypto::SecureString>>
    tag_description(int64_t tag_id) const noexcept;
    [[nodiscard]] DbResult<std::vector<TagDescriptionRecord>> tag_descriptions() const noexcept;
    [[nodiscard]] DbResult<std::vector<ResolvedTagFieldValueRecord>>
    resolved_tag_field_values() const noexcept;
    [[nodiscard]] DbStatus set_settings(const SettingsRecord& settings) noexcept;
    [[nodiscard]] DbResult<SettingsRecord> settings() const noexcept;
    [[nodiscard]] DbStatus add_saved_search(const SavedSearchRecord& search) noexcept;
    [[nodiscard]] DbResult<std::vector<SavedSearchRecord>> saved_searches() const noexcept;
    [[nodiscard]] DbStatus
    backup_to(const std::filesystem::path& destination,
              std::span<const uint8_t, crypto::KEY_SIZE> database_key) const noexcept;
    [[nodiscard]] DbStatus
    backup_to(const VaultRoot& destination,
              std::span<const uint8_t, crypto::KEY_SIZE> database_key) const noexcept;
    // Atomically mirrors the logical metadata snapshot while preserving object
    // references for live nodes. References belonging to removed nodes are
    // dropped in the same transaction; their immutable files become GC input.
    [[nodiscard]] DbStatus
    sync_metadata(const IndexNode& root, const VaultSettings& settings,
                  std::span<const SavedSearch> searches,
                  std::span<const ObjectRecord> staged_objects = {}) noexcept;
    [[nodiscard]] DbStatus begin_conversion(const ConversionMarker& marker) noexcept;
    [[nodiscard]] DbResult<std::optional<ConversionMarker>> conversion_marker() const noexcept;
    [[nodiscard]] DbStatus finish_conversion() noexcept;

private:
    friend bool database_healthy(const Database&) noexcept;
    friend bool database_deep_healthy(const Database&) noexcept;
    friend DbStatus maintain_database(Database&) noexcept;
    friend Id database_root_node_id(const Database&) noexcept;
    friend DbStatus set_database_user_version_for_test(Database&, int) noexcept;
    [[nodiscard]] static OpenResult
    open_raw(const std::filesystem::path& path,
             std::span<const uint8_t, crypto::KEY_SIZE> database_key, int flags) noexcept;
    explicit Database(sqlite3* handle) noexcept : handle_(handle) {}
    sqlite3* handle_ = nullptr;
};

[[nodiscard]] bool database_healthy(const Database& database) noexcept;
[[nodiscard]] bool database_deep_healthy(const Database& database) noexcept;
[[nodiscard]] DbStatus maintain_database(Database& database) noexcept;
[[nodiscard]] Id database_root_node_id(const Database& database) noexcept;
// Test-only migration seam; production never sets versions directly.
[[nodiscard]] DbStatus set_database_user_version_for_test(Database& database, int version) noexcept;

struct Database::OpenResult {
    DbStatus status = DbStatus::IoError;
    std::optional<Database> database;
};

}  // namespace vault::v3
