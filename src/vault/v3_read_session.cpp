#include "vault/v3_read_session.h"

#include "crypto/random.h"
#include "image/thumbnail.h"
#include "vault/safe_name.h"

#include <algorithm>
#include <limits>

namespace vault::v3 {
namespace {

uint64_t cache_identity(const Id& id, uint8_t role) noexcept
{
    uint64_t value = 1469598103934665603ULL ^ role;
    for (const uint8_t byte : id)
        value = (value ^ byte) * 1099511628211ULL;
    return value == 0 ? role : value;
}

bool valid_media_values(const NodeRecord& record) noexcept
{
    const auto value_or_unknown = [](const std::optional<int>& value, int maximum) {
        return !value.has_value() || (*value >= 0 && (*value <= maximum || *value == 0xff));
    };
    if (record.type == NodeType::Image)
        return value_or_unknown(record.media_format, std::to_underlying(ImageFormat::AVIF));
    if (record.type == NodeType::Video)
        return value_or_unknown(record.media_format, std::to_underlying(VideoContainer::RM)) &&
               value_or_unknown(record.codec, std::to_underlying(VideoCodec::RV40)) &&
               record.duration_ms.value_or(0) <= std::numeric_limits<uint64_t>::max() / 1000;
    return !record.media_format.has_value() && !record.codec.has_value();
}

ReadStatus map_db(DbStatus status) noexcept
{
    switch (status) {
    case DbStatus::Ok:
        return ReadStatus::Ok;
    case DbStatus::Busy:
        return ReadStatus::Busy;
    case DbStatus::UnsupportedVersion:
        return ReadStatus::UnsupportedVersion;
    case DbStatus::WrongKeyOrCorrupt:
        return ReadStatus::BadFormat;
    default:
        return ReadStatus::IoError;
    }
}

IndexNode node_from_record(const NodeRecord& record)
{
    IndexNode node = IndexNode::gallery(record.display_name.view());
    if (record.type == NodeType::Image)
        node = IndexNode::image(record.display_name.view());
    else if (record.type == NodeType::Video)
        node = IndexNode::video(record.display_name.view());
    node.node_id = record.node_id;
    node.favorite = record.favorite;
    if (node.is_gallery()) node.sort_key = static_cast<SortKey>(record.sort_key.value_or(0));
    if (node.is_image()) {
        node.meta.format = static_cast<ImageFormat>(record.media_format.value_or(0xff));
        node.meta.width = record.width.value_or(0);
        node.meta.height = record.height.value_or(0);
        node.meta.orig_size = record.original_size.value_or(0);
        node.meta.created_ts = record.created_ts;
        node.meta.animated = record.animated.value_or(false);
    } else if (node.is_video()) {
        node.vmeta.container = static_cast<VideoContainer>(record.media_format.value_or(0xff));
        node.vmeta.codec = static_cast<VideoCodec>(record.codec.value_or(0xff));
        node.vmeta.width = record.width.value_or(0);
        node.vmeta.height = record.height.value_or(0);
        node.vmeta.duration_us = record.duration_ms.value_or(0) * 1000;
        node.vmeta.orig_size = record.original_size.value_or(0);
        node.vmeta.created_ts = record.created_ts;
        node.vmeta.chunk_size = VIDEO_OBJECT_FRAME_PLAIN;
    }
    return node;
}

void apply_object_markers(IndexNode& node, const std::vector<ObjectRecord>& objects)
{
    using enum ObjectRole;
    for (const auto& object : objects) {
        if (object.node_id != node.node_id) continue;
        if (node.is_image() && object.role == Thumbnail) {
            node.meta.thumb_offset = cache_identity(node.node_id, 2);
            node.meta.thumb_length = object.encrypted_length;
        } else if (node.is_image() && object.role == OriginalImage) {
            node.meta.data_offset = cache_identity(node.node_id, 1);
            node.meta.data_length = object.encrypted_length;
        } else if (node.is_video() && object.role == Poster) {
            node.vmeta.poster_offset = cache_identity(node.node_id, 3);
            node.vmeta.poster_length = object.encrypted_length;
        } else if (node.is_video() && object.role == OriginalVideo) {
            node.vmeta.orig_size = object.plaintext_length;
            node.vmeta.chunk_size = object.frame_plain_limit;
            node.vmeta.chunks.resize(object.frame_count);
        }
    }
    for (auto& child : node.children)
        apply_object_markers(child, objects);
}

bool load_settings(const Database& database, VaultSettings& settings) noexcept
{
    auto db_settings = database.settings();
    if (db_settings.status != DbStatus::Ok || db_settings.value.default_sort > 7) return false;
    settings.default_sort = static_cast<SortKey>(db_settings.value.default_sort);
    settings.tiles_show_tags = db_settings.value.tiles_show_tags;
    settings.migrated_index_version = db_settings.value.migrated_index_version;
    settings.migrated_probe_caps = db_settings.value.migrated_probe_caps;
    settings.migrated_thumb_side = db_settings.value.migrated_thumb_side;

    auto categories = database.tag_categories();
    if (categories.status != DbStatus::Ok) return false;
    for (auto& source : categories.value)
        settings.categories.emplace_back(std::move(source.display_name), source.swatch,
                                         std::move(source.fields));

    auto descriptions = database.tag_descriptions();
    if (descriptions.status != DbStatus::Ok) return false;
    for (auto& source : descriptions.value)
        settings.tag_descriptions.emplace_back(std::move(source.tag),
                                               std::move(source.description));

    auto field_values = database.resolved_tag_field_values();
    if (field_values.status != DbStatus::Ok) return false;
    for (auto& source : field_values.value)
        settings.tag_field_values.emplace_back(std::move(source.tag), std::move(source.field),
                                               std::move(source.value));
    return true;
}

}  // namespace

ReadSession::OpenResult ReadSession::open(const std::filesystem::path& path)
{
    auto root = VaultRoot::open(path);
    if (!root || !root->validate_layout()) return {ReadStatus::IoError, {}};
    std::array<uint8_t, V3_HEADER_SIZE> raw{};
    if (!root->read_header(raw)) return {ReadStatus::BadFormat, {}};
    V3Header header;
    const auto parsed = parse_v3_header(raw, header);
    if (parsed == HeaderStatus::UnsupportedVersion) return {ReadStatus::UnsupportedVersion, {}};
    if (parsed != HeaderStatus::Ok) return {ReadStatus::BadFormat, {}};
    auto session = std::make_unique<ReadSession>();
    session->root_handle_ = std::move(*root);
    session->header_ = header;
    return {ReadStatus::Ok, std::move(session)};
}

ReadSession::OpenResult ReadSession::create(const std::filesystem::path& path,
                                            std::span<const uint8_t> password,
                                            std::span<const uint8_t> keyfile,
                                            const crypto::KdfParams& kdf)
{
    if (std::error_code exists_error; std::filesystem::exists(path, exists_error))
        return {ReadStatus::AlreadyExists, {}};
    auto root = VaultRoot::create(path);
    if (!root) return {ReadStatus::IoError, {}};
    auto session = std::make_unique<ReadSession>();
    session->root_handle_ = std::move(*root);
    const auto fail_create = [&](ReadStatus status) -> OpenResult {
        session->database_.reset();
        session->session_lock_.reset();
        session->master_key_.wipe();
        (void)session->root_handle_.rollback_creation();
        return {status, {}};
    };
    session->session_lock_ = session->root_handle_.try_writer_lock();
    if (!session->session_lock_) return fail_create(ReadStatus::Busy);
    if (create_v3_header(password, keyfile, kdf, session->header_, session->master_key_) !=
        HeaderStatus::Ok)
        return fail_create(ReadStatus::IoError);
    if (const auto raw = serialize_v3_header(session->header_);
        !session->root_handle_.write_header(raw))
        return fail_create(ReadStatus::IoError);
    Id root_id{};
    if (!crypto::fill_random(root_id)) return fail_create(ReadStatus::IoError);
    auto db_key = derive_database_key(session->master_key_.as_span(), session->header_.vault_id);
    auto opened = Database::create_in_root(session->root_handle_, db_key.as_span(), root_id);
    if (opened.status != DbStatus::Ok || !opened.database) return fail_create(ReadStatus::IoError);
    session->database_ = std::move(*opened.database);
    if (!session->materialize()) return fail_create(ReadStatus::BadFormat);
    session->settings_ = VaultSettings::seeded();
    session->settings_.migrated_index_version = MIGRATION_INDEX_VERSION;
    session->settings_.migrated_thumb_side = static_cast<uint16_t>(image::THUMB_MAX_SIDE);
    if (session->database_->sync_metadata(session->root_, session->settings_, {}) != DbStatus::Ok)
        return fail_create(ReadStatus::IoError);
    session->unlocked_ = true;
    return {ReadStatus::Ok, std::move(session)};
}

ReadStatus ReadSession::unlock(std::span<const uint8_t> password, std::span<const uint8_t> keyfile)
{
    if (unlocked_) return ReadStatus::Ok;
    session_lock_ = root_handle_.try_writer_lock();
    if (!session_lock_) return ReadStatus::Busy;
    const auto unwrapped = unwrap_v3_master_key(header_, password, keyfile, master_key_);
    if (unwrapped == HeaderStatus::AuthenticationFailed) {
        session_lock_.reset();
        return ReadStatus::AuthenticationFailed;
    }
    if (unwrapped != HeaderStatus::Ok) {
        session_lock_.reset();
        return ReadStatus::IoError;
    }
    auto db_key = derive_database_key(master_key_.as_span(), header_.vault_id);
    auto opened = Database::open_writable(root_handle_, db_key.as_span());
    if (opened.status != DbStatus::Ok || !opened.database) {
        master_key_.wipe();
        session_lock_.reset();
        return map_db(opened.status);
    }
    database_ = std::move(*opened.database);
    if (!materialize()) {
        lock();
        return ReadStatus::BadFormat;
    }
    unlocked_ = true;
    return ReadStatus::Ok;
}

bool ReadSession::build_children(IndexNode& parent, const Id& parent_id, unsigned depth,
                                 size_t& count) noexcept
{
    if (depth >= INDEX_MAX_DEPTH) return false;
    auto children = database_->list_children(parent_id);
    if (children.status != DbStatus::Ok) return false;
    for (const auto& record : children.value) {
        if (++count > 1'000'000 || !is_safe_node_name(record.display_name.view())) return false;
        if ((record.sort_key.has_value() &&
             *record.sort_key > std::to_underlying(SortKey::Insertion)) ||
            !valid_media_values(record))
            return false;
        IndexNode child = node_from_record(record);
        auto tags = database_->node_tags(record.node_id);
        if (tags.status != DbStatus::Ok || tags.value.size() > INDEX_MAX_TAGS) return false;
        child.tags = std::move(tags.value);
        if (child.is_gallery() && !build_children(child, record.node_id, depth + 1, count))
            return false;
        parent.children.emplace_back(std::move(child));
    }
    return true;
}

bool ReadSession::materialize() noexcept
{
    const Id root_id = database_root_node_id(*database_);
    auto root_record = database_->find_node(root_id);
    if (root_record.status != DbStatus::Ok || !root_record.value.has_value() ||
        root_record.value->type != NodeType::Gallery || root_record.value->parent_id.has_value())
        return false;
    root_ = node_from_record(*root_record.value);
    root_.name = "";
    auto root_tags = database_->node_tags(root_id);
    if (root_tags.status != DbStatus::Ok) return false;
    root_.tags = std::move(root_tags.value);
    if (size_t count = 1; !build_children(root_, root_id, 0, count)) return false;
    auto refs = database_->object_references();
    if (refs.status != DbStatus::Ok) return false;
    if (refs.value.size() > 3'000'000) return false;
    for (const auto& object : refs.value) {
        if (object.frame_count == 0 || object.frame_count > OBJECT_MAX_FRAMES ||
            object.frame_plain_limit == 0 || object.frame_plain_limit > OBJECT_MAX_FRAME_PLAIN)
            return false;
    }
    objects_ = std::move(refs.value);
    apply_object_markers(root_, objects_);
    if (!load_settings(*database_, settings_)) return false;
    auto searches = database_->saved_searches();
    if (searches.status != DbStatus::Ok) return false;
    for (auto& source : searches.value)
        saved_searches_.emplace_back(std::move(source.display_name), std::move(source.query));
    return true;
}

std::optional<ObjectInfo> ReadSession::object_for(const Id& node_id, ObjectRole role) const noexcept
{
    const std::lock_guard lock(state_mutex_);
    const auto it = std::ranges::find_if(objects_, [&](const ObjectRecord& object) {
        return object.node_id == node_id && object.role == role;
    });
    if (it == objects_.end()) return std::nullopt;
    const IndexNode* owner = nullptr;
    std::function<void(const IndexNode&)> find_owner = [&](const IndexNode& node) {
        if (owner) return;
        if (node.node_id == node_id) {
            owner = &node;
            return;
        }
        for (const auto& child : node.children)
            find_owner(child);
    };
    find_owner(root_);
    if (!owner) return std::nullopt;
    const uint8_t media_format = owner->is_image() ? std::to_underlying(owner->meta.format)
                                                   : std::to_underlying(owner->vmeta.container);
    return ObjectInfo{
        it->object_id,  header_.vault_id,     it->node_id,          it->role,
        media_format,   it->plaintext_length, it->encrypted_length, it->frame_plain_limit,
        it->frame_count};
}

ReadStatus ReadSession::read(const IndexNode& node, ObjectRole role,
                             crypto::SecureBytes& out) const noexcept
{
    return read_id(node.node_id, role, out);
}

ReadStatus ReadSession::read_id(const Id& node_id, ObjectRole role,
                                crypto::SecureBytes& out) const noexcept
{
    if (!unlocked_) return ReadStatus::IoError;
    const auto info = object_for(node_id, role);
    if (!info.has_value()) return ReadStatus::BadFormat;
    auto reader = ObjectReader::open(root_handle_, master_key_.as_span(), *info);
    if (reader.status == ObjectStatus::AuthenticationFailed)
        return ReadStatus::AuthenticationFailed;
    if (reader.status != ObjectStatus::Ok || !reader.reader) return ReadStatus::BadFormat;
    const auto status = reader.reader->read_all(out);
    if (status == ObjectStatus::Ok) return ReadStatus::Ok;
    if (status == ObjectStatus::AuthenticationFailed) return ReadStatus::AuthenticationFailed;
    return ReadStatus::BadFormat;
}

ReadStatus ReadSession::commit_metadata(const IndexNode& root, const VaultSettings& settings,
                                        std::span<const SavedSearch> searches) noexcept
{
    const std::lock_guard lock(state_mutex_);
    if (!unlocked_ || !database_ || !session_lock_) return ReadStatus::IoError;
    if (const auto status = database_->sync_metadata(root, settings, searches, staged_objects_);
        status != DbStatus::Ok)
        return map_db(status);
    staged_objects_.clear();
    root_ = root;
    settings_ = settings;
    saved_searches_.assign(searches.begin(), searches.end());
    auto refs = database_->object_references();
    if (refs.status != DbStatus::Ok) return map_db(refs.status);
    objects_ = std::move(refs.value);
    apply_object_markers(root_, objects_);
    return ReadStatus::Ok;
}

ReadStatus ReadSession::stage_object(const Id& node_id, ObjectRole role, uint8_t media_format,
                                     std::span<const uint8_t> plaintext, ObjectInfo* info) noexcept
{
    const std::lock_guard lock(state_mutex_);
    if (!unlocked_ || !database_ || !session_lock_ || plaintext.empty()) return ReadStatus::IoError;
    const uint32_t frame_limit =
        role == ObjectRole::OriginalVideo ? VIDEO_OBJECT_FRAME_PLAIN : OBJECT_MAX_FRAME_PLAIN;
    const ObjectWriteRequest request{header_.vault_id, node_id, role, media_format, frame_limit};
    const auto written = write_object(root_handle_, master_key_.as_span(), request, plaintext);
    if (written.status != ObjectStatus::Ok) return ReadStatus::IoError;
    try {
        staged_objects_.emplace_back(
            written.info.object_id, node_id, role, written.info.encrypted_length,
            written.info.plaintext_length, written.info.frame_plain_limit,
            written.info.frame_count, 0);
    } catch (...) {
        // The published immutable object stays unreferenced and is safe for later GC.
        return ReadStatus::IoError;
    }
    if (info) *info = written.info;
    return ReadStatus::Ok;
}

ReadStatus ReadSession::change_password(std::span<const uint8_t> old_password,
                                        std::span<const uint8_t> old_keyfile,
                                        std::span<const uint8_t> new_password,
                                        std::span<const uint8_t> new_keyfile) noexcept
{
    std::optional<WriterLock> temporary_lock;
    if (!session_lock_) {
        temporary_lock = root_handle_.try_writer_lock();
        if (!temporary_lock) return ReadStatus::Busy;
    }
    crypto::SecureBuffer<crypto::KEY_SIZE> verified;
    const auto checked = unwrap_v3_master_key(header_, old_password, old_keyfile, verified);
    if (checked == HeaderStatus::AuthenticationFailed) return ReadStatus::AuthenticationFailed;
    if (checked != HeaderStatus::Ok) return ReadStatus::IoError;
    if (unlocked_ && !std::ranges::equal(verified.as_span(), master_key_.as_span()))
        return ReadStatus::IoError;
    V3Header replacement;
    if (rewrap_v3_header(header_, verified.as_span(), new_password, new_keyfile, replacement) !=
        HeaderStatus::Ok)
        return ReadStatus::IoError;
    if (const auto raw = serialize_v3_header(replacement); !root_handle_.replace_header(raw))
        return ReadStatus::IoError;
    header_ = replacement;
    return ReadStatus::Ok;
}

void ReadSession::lock() noexcept
{
    const std::lock_guard state_lock(state_mutex_);
    database_.reset();
    objects_.clear();
    staged_objects_.clear();
    root_ = IndexNode::gallery("");
    saved_searches_.clear();
    settings_ = VaultSettings{};
    unlocked_ = false;
    session_lock_.reset();
    master_key_.wipe();
}

}  // namespace vault::v3
