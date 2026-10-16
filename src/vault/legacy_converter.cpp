#include "vault/legacy_converter.h"

#include "crypto/random.h"
#include "platform/path_utf8.h"
#include "platform/safe_print.h"
#include "vault/chunk_store.h"
#include "vault/v3_read_session.h"
#include "vault/vault.h"

#include "monocypher.h"

#include <algorithm>
#include <limits>
#include <system_error>

namespace vault {
#ifdef OSV_VAULT_FIXTURE_DIR
// Deterministic cancellation seam, compiled only into the test binary.
thread_local std::function<void(bool, uint64_t)> conversion_checkpoint;
void test_only_conversion_checkpoint(std::function<void(bool, uint64_t)> callback)
{
    conversion_checkpoint = std::move(callback);
}
#endif
namespace {

constexpr uint32_t CONVERSION_VERSION = 1;

const char* status_reason(VaultResult status) noexcept
{
    switch (status) {
    case VaultResult::Ok:
        return "";
    case VaultResult::IoError:
        return "Filesystem read, write or sync failed";
    case VaultResult::BadFormat:
        return "Vault header or metadata format is invalid";
    case VaultResult::AuthFailed:
        return "Password/keyfile or encrypted data authentication failed";
    case VaultResult::Locked:
        return "Vault session is locked";
    case VaultResult::NotFound:
        return "Requested vault entry is missing";
    case VaultResult::AlreadyExists:
        return "Destination already exists";
    case VaultResult::InvalidArg:
        return "Vault operation rejected invalid metadata or arguments";
    case VaultResult::CryptoError:
        return "Random generator or key derivation failed";
    case VaultResult::Busy:
        return "Another process holds the vault lease";
    case VaultResult::IncompleteConversion:
        return "Destination is an incomplete conversion";
    case VaultResult::Damaged:
        return "Destination media references are damaged";
    case VaultResult::UnsupportedVersion:
        return "Vault format version is unsupported";
    }
    return "Unrecognized backend status";
}

const char* status_reason(v3::ReadStatus status) noexcept
{
    switch (status) {
    case v3::ReadStatus::Ok:
        return "";
    case v3::ReadStatus::IoError:
        return "Object or database read, write or sync failed";
    case v3::ReadStatus::BadFormat:
        return "Encrypted object or database format is invalid";
    case v3::ReadStatus::AuthenticationFailed:
        return "Encrypted data authentication failed";
    case v3::ReadStatus::Busy:
        return "Another process holds the destination lease";
    case v3::ReadStatus::UnsupportedVersion:
        return "Destination format version is unsupported";
    case v3::ReadStatus::AlreadyExists:
        return "Destination object already exists";
    }
    return "Unrecognized backend status";
}

const char* status_reason(v3::DbStatus status) noexcept
{
    switch (status) {
    case v3::DbStatus::Ok:
        return "";
    case v3::DbStatus::InvalidArgument:
        return "Database rejected invalid metadata";
    case v3::DbStatus::IoError:
        return "Encrypted database read, write or sync failed";
    case v3::DbStatus::WrongKeyOrCorrupt:
        return "Encrypted database key or integrity check failed";
    case v3::DbStatus::Busy:
        return "Encrypted database is busy";
    case v3::DbStatus::DiskFull:
        return "Destination disk is full";
    case v3::DbStatus::Constraint:
        return "Converted metadata violates a database constraint";
    case v3::DbStatus::UnsupportedVersion:
        return "Database schema version is unsupported";
    }
    return "Unrecognized backend status";
}

const char* status_reason(v3::RecoveryStatus status) noexcept
{
    switch (status) {
    case v3::RecoveryStatus::Ok:
        return "";
    case v3::RecoveryStatus::DatabaseError:
        return "Encrypted database integrity or cleanup operation failed";
    case v3::RecoveryStatus::FilesystemError:
        return "Destination filesystem read or cleanup operation failed";
    case v3::RecoveryStatus::Corrupt:
        return "Destination integrity check found corruption";
    case v3::RecoveryStatus::Cancelled:
        return "Destination integrity check cancelled";
    }
    return "Unrecognized recovery status";
}

const char* conversion_failure_reason(LegacyConversionStatus status) noexcept
{
    using enum LegacyConversionStatus;
    switch (status) {
    case Ok:
        return "";
    case SourceLocked:
        return "Original vault is no longer unlocked";
    case SourceNotLegacy:
        return "Original is already a directory vault";
    case InvalidDestination:
        return "Destination is empty, inaccessible or overlaps the source";
    case DestinationExists:
        return "Existing destination cannot be used for conversion";
    case ResumeMismatch:
        return "Conversion marker is absent or belongs to a different source/version";
    case AuthenticationFailed:
        return "Destination credentials or wrapped key authentication failed";
    case SourceCorrupt:
        return "Source data could not be read or authenticated";
    case IoError:
        return "Destination allocation, write or synchronization failed";
    case Cancelled:
        return "Conversion cancellation requested";
    case VerificationFailed:
        return "Destination failed the named verification operation";
    case InsufficientSpace:
        return "Destination free-space requirement could not be satisfied";
    }
    return "Unrecognized conversion status";
}

template <typename Status>
Status record_result(LegacyConversionReport& report, Status status) noexcept
{
    report.backend_code = static_cast<int>(status);
    report.detail = status_reason(status);
    return status;
}

v3::ConversionMarker source_marker(const Header& header) noexcept
{
    std::array<uint8_t, HEADER_SIZE> encoded{};
    header.serialize(encoded);
    v3::ConversionMarker marker;
    crypto_blake2b(marker.source_fingerprint.data(), marker.source_fingerprint.size(),
                   encoded.data(), encoded.size());
    marker.conversion_version = CONVERSION_VERSION;
    return marker;
}

CancellationToken cancellation_token(const LegacyConversionRequest& request) noexcept
{
    return {request.cancel, request.progress ? &request.progress->cancel : nullptr};
}

bool cancelled(const LegacyConversionRequest& request) noexcept
{
    return (request.cancel && request.cancel->load()) ||
           (request.progress && request.progress->cancel.load());
}

struct PreflightTotals {
    uint64_t nodes = 0;
    uint64_t plaintext_bytes = 0;
    bool valid = true;
};

void add_checked(uint64_t value, uint64_t& total, bool& valid) noexcept
{
    if (value > std::numeric_limits<uint64_t>::max() - total) {
        valid = false;
        return;
    }
    total += value;
}

void scan_preflight(const IndexNode& node, PreflightTotals& totals) noexcept
{
    add_checked(1, totals.nodes, totals.valid);
    if (node.is_image()) {
        add_checked(node.meta.orig_size, totals.plaintext_bytes, totals.valid);
        add_checked(node.meta.thumb_length, totals.plaintext_bytes, totals.valid);
    } else if (node.is_video()) {
        add_checked(node.vmeta.orig_size, totals.plaintext_bytes, totals.valid);
        add_checked(node.vmeta.poster_length, totals.plaintext_bytes, totals.valid);
    }
    for (const auto& child : node.children)
        scan_preflight(child, totals);
}

bool preflight_space(const std::filesystem::path& source, const std::filesystem::path& destination,
                     PreflightTotals totals, LegacyConversionReport& report) noexcept
{
    constexpr uint64_t FIXED_SLACK = 32U * 1024U * 1024U;
    std::error_code ec;
    const uint64_t source_bytes = std::filesystem::file_size(source, ec);
    const uint64_t content_bytes = std::max(totals.plaintext_bytes, source_bytes);
    constexpr uint64_t PER_NODE_SLACK = 16U * 1024U;
    if (ec) {
        report.detail = "Cannot read source file size";
        report.backend_code = ec.value();
        return false;
    }
    if (!totals.valid || totals.nodes > std::numeric_limits<uint64_t>::max() / PER_NODE_SLACK) {
        report.detail = "Source size totals overflow the conversion space estimate";
        return false;
    }
    // Conversion publishes objects in place and verifies by reading them back.
    // Budget one destination, not a second full copy. Keep source size as a
    // conservative floor, expanded originals plus stored derived records as the
    // other baseline, and allow for framing, filesystem allocation and DB/WAL.
    uint64_t required = content_bytes;
    add_checked(content_bytes / 20 + (content_bytes % 20 != 0 ? 1U : 0U), required, totals.valid);
    add_checked(totals.nodes * PER_NODE_SLACK, required, totals.valid);
    add_checked(FIXED_SLACK, required, totals.valid);
    if (!totals.valid) {
        report.detail = "Required destination space exceeds supported size";
        return false;
    }
    report.required_free_bytes = required;
    const auto parent =
        destination.parent_path().empty() ? std::filesystem::path{"."} : destination.parent_path();
    const auto info = std::filesystem::space(parent, ec);
    if (ec) {
        report.detail = "Cannot query destination filesystem free space";
        report.backend_code = ec.value();
        return false;
    }
    report.detail =
        info.available < report.required_free_bytes ? "Insufficient destination free space" : "";
    return info.available >= report.required_free_bytes;
}

bool path_prefix(const std::filesystem::path& parent, const std::filesystem::path& child)
{
    auto p = parent.begin();
    auto c = child.begin();
    for (; p != parent.end() && c != child.end(); ++p, ++c)
        if (*p != *c) return false;
    return p == parent.end();
}

std::filesystem::path normalized_for_compare(const std::filesystem::path& path, std::error_code& ec)
{
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec) return {};
    const auto parent = std::filesystem::weakly_canonical(absolute.parent_path(), ec);
    if (ec) return {};
    return (parent / absolute.filename()).lexically_normal();
}

LegacyConversionStatus validate_destination(const std::filesystem::path& source,
                                            const std::filesystem::path& destination) noexcept
{
    using enum LegacyConversionStatus;
    if (destination.empty()) return InvalidDestination;
    std::error_code ec;
    const auto source_path = normalized_for_compare(source, ec);
    if (ec) return InvalidDestination;
    const auto destination_path = normalized_for_compare(destination, ec);
    if (ec || source_path == destination_path || path_prefix(source_path, destination_path) ||
        path_prefix(destination_path, source_path))
        return InvalidDestination;
    if (std::filesystem::exists(destination_path, ec)) return ec ? IoError : DestinationExists;
    return ec ? IoError : Ok;
}

IndexNode clone_metadata(const IndexNode& source, const v3::Id* root_id = nullptr)
{
    IndexNode result;
    result.type = source.type;
    result.name = source.name;
    result.tags = source.tags;
    result.favorite = source.favorite;
    result.sort_key = source.sort_key;
    result.meta = source.meta;
    result.vmeta = source.vmeta;
    result.meta.data_offset = 0;
    result.meta.data_length = 0;
    result.meta.thumb_offset = 0;
    result.meta.thumb_length = 0;
    result.meta.data_id = {};
    result.meta.thumb_id = {};
    result.meta.context_bound = true;
    result.vmeta.chunks.clear();
    result.vmeta.poster_offset = 0;
    result.vmeta.poster_length = 0;
    result.vmeta.poster_id = {};
    result.vmeta.context_bound = true;
    if (root_id) {
        result.node_id = *root_id;
    }
    return result;
}

struct CopyContext {
    Vault& source;
    v3::ReadSession& destination;
    const LegacyConversionRequest& request;
    LegacyConversionReport& report;
    const VaultSettings& settings;
    const std::vector<SavedSearch>& saved_searches;
    IndexNode* root = nullptr;
    size_t since_commit = 0;
    uint64_t source_ordinal = 0;
};

v3::Id conversion_source_key(uint64_t ordinal) noexcept
{
    // Pre-v13 legacy indexes have no persistent node IDs. A parent-first DFS
    // ordinal is stable for the immutable read-only source and therefore also
    // covers every older index generation without treating an all-zero ID as
    // a shared identity.
    v3::Id result{'O', 'S', 'V', 'C', 'O', 'N', 'V', 1};
    for (size_t i = 0; i < sizeof(ordinal); ++i)
        result[8 + i] = static_cast<uint8_t>(ordinal >> (i * 8));
    return result;
}

LegacyConversionStatus mapped_node_id(CopyContext& context, uint64_t source_ordinal,
                                      IndexNode& destination, const v3::Id* root_id) noexcept
{
    const auto source_key = conversion_source_key(source_ordinal);
    context.report.operation = "Read resume node mapping";
    const auto mapped = context.destination.conversion_node_id(source_key);
    if (record_result(context.report, mapped.status) != v3::DbStatus::Ok)
        return LegacyConversionStatus::IoError;
    if (mapped.value) {
        destination.node_id = *mapped.value;
        return LegacyConversionStatus::Ok;
    }
    destination.node_id = root_id ? *root_id : v3::Id{};
    if (!root_id && !crypto::fill_random(destination.node_id)) {
        context.report.operation = "Generate destination node identity";
        context.report.detail = "System random generator failed";
        return LegacyConversionStatus::IoError;
    }
    context.report.operation = "Save resume node mapping";
    if (record_result(context.report, context.destination.record_conversion_node(
                                          {source_key, destination.node_id})) != v3::DbStatus::Ok)
        return LegacyConversionStatus::IoError;
    return LegacyConversionStatus::Ok;
}

LegacyConversionStatus commit_batch(CopyContext& context) noexcept
{
    if (!context.root || context.since_commit == 0) return LegacyConversionStatus::Ok;
    context.report.operation = "Commit copied metadata batch";
    if (record_result(context.report, context.destination.commit_metadata(
                                          *context.root, context.settings,
                                          context.saved_searches)) != v3::ReadStatus::Ok)
        return LegacyConversionStatus::IoError;
    context.since_commit = 0;
    return LegacyConversionStatus::Ok;
}

LegacyConversionStatus count_and_maybe_commit(CopyContext& context) noexcept
{
    if (context.request.progress) ++context.request.progress->done;
    ++context.since_commit;
    constexpr size_t BATCH_NODES = 32;
    return context.since_commit >= BATCH_NODES ? commit_batch(context) : LegacyConversionStatus::Ok;
}

LegacyConversionStatus stage_image_original(CopyContext& context, const IndexNode& source,
                                            const IndexNode& destination, uint8_t format) noexcept
{
    crypto::SecureBytes original;
    context.report.operation = "Read source image";
    if (record_result(context.report, context.source.read_image(source, original)) !=
        VaultResult::Ok)
        return LegacyConversionStatus::SourceCorrupt;
    context.report.operation = "Write destination image";
    if (const auto read =
            [&context, &original](uint64_t offset, std::span<uint8_t> output) {
                if (cancelled(context.request)) return false;
                std::ranges::copy(
                    original.as_span().subspan(static_cast<size_t>(offset), output.size()),
                    output.begin());
                return true;
            };
        record_result(context.report, context.destination.stage_object_stream(
                                          destination.node_id, v3::ObjectRole::OriginalImage,
                                          format, original.size(), read)) != v3::ReadStatus::Ok)
        return cancelled(context.request) ? LegacyConversionStatus::Cancelled
                                          : LegacyConversionStatus::IoError;
    context.report.original_bytes += original.size();
    return LegacyConversionStatus::Ok;
}

LegacyConversionStatus stage_media(CopyContext& context, const IndexNode& source,
                                   const IndexNode& destination) noexcept
{
    if (cancelled(context.request)) return LegacyConversionStatus::Cancelled;
    const uint8_t format = source.is_image() ? std::to_underlying(source.meta.format)
                                             : std::to_underlying(source.vmeta.container);
    if (const auto original_role =
            source.is_video() ? v3::ObjectRole::OriginalVideo : v3::ObjectRole::OriginalImage;
        context.destination.object_plaintext_length(destination.node_id, original_role)
            .has_value()) {
        context.report.original_bytes +=
            source.is_video() ? source.vmeta.orig_size : source.meta.orig_size;
    } else if (source.is_video()) {
        context.report.operation = "Copy video stream";
        context.report.detail = "";
        context.report.backend_code = -1;
        if (const auto status =
                copy_legacy_video_object(context.source, source, context.destination, destination,
                                         cancellation_token(context.request));
            status != LegacyConversionStatus::Ok) {
            context.report.detail =
                status == LegacyConversionStatus::SourceCorrupt
                    ? "Source video chunk could not be read, authenticated or has an invalid length"
                    : "Destination video object could not be written or synced";
            return status;
        }
        context.report.original_bytes += source.vmeta.orig_size;
    } else {
        if (const auto status = stage_image_original(context, source, destination, format);
            status != LegacyConversionStatus::Ok)
            return status;
    }

    if (const bool has_derived =
            source.is_image() ? source.meta.thumb_length != 0 : source.vmeta.poster_length != 0;
        !has_derived) {
        ++context.report.missing_derived;
        return LegacyConversionStatus::Ok;
    }
    crypto::SecureBytes derived;
    context.report.operation = "Read source thumbnail/poster";
    if (record_result(context.report, context.source.read_thumbnail(source, derived)) !=
        VaultResult::Ok)
        return LegacyConversionStatus::SourceCorrupt;
    const auto derived_role =
        source.is_image() ? v3::ObjectRole::Thumbnail : v3::ObjectRole::Poster;
    if (const auto existing =
            context.destination.object_plaintext_length(destination.node_id, derived_role);
        existing.has_value()) {
        context.report.derived_bytes += *existing;
        return LegacyConversionStatus::Ok;
    }
    context.report.operation = "Write destination thumbnail/poster";
    if (record_result(context.report,
                      context.destination.stage_object(destination.node_id, derived_role, format,
                                                       derived.as_span())) != v3::ReadStatus::Ok)
        return LegacyConversionStatus::IoError;
    context.report.derived_bytes += derived.size();
    return LegacyConversionStatus::Ok;
}

LegacyConversionStatus copy_node(CopyContext& context, const IndexNode& source,
                                 IndexNode& destination, const v3::Id* root_id = nullptr) noexcept
{
    using enum LegacyConversionStatus;
    if (cancelled(context.request)) return Cancelled;
    const uint64_t source_ordinal = ++context.source_ordinal;
    context.report.failed_ordinal = source_ordinal;
    context.report.operation = "Allocate copied metadata";
    context.report.detail = "Could not allocate node metadata";
    context.report.backend_code = -1;
    try {
        destination = clone_metadata(source, root_id);
    } catch (...) {
        return IoError;
    }
    if (const auto status = mapped_node_id(context, source_ordinal, destination, root_id);
        status != Ok)
        return status;

    if (source.is_gallery()) {
        ++context.report.galleries;
        try {
            destination.children.reserve(source.children.size());
            for (const auto& child : source.children) {
                destination.children.emplace_back();
                const auto status = copy_node(context, child, destination.children.back());
                if (status != Ok) return status;
            }
        } catch (...) {
            return IoError;
        }
        return count_and_maybe_commit(context);
    }

    if (source.is_image())
        ++context.report.images;
    else
        ++context.report.videos;
    const auto status = stage_media(context, source, destination);
    if (status != Ok) context.report.failed_ordinal = source_ordinal;
    return status == Ok ? count_and_maybe_commit(context) : status;
}

const char* image_metadata_difference(const IndexNode& source,
                                      const IndexNode& destination) noexcept
{
    if (source.meta.format != destination.meta.format) return "Image format differs";
    if (source.meta.width != destination.meta.width) return "Image width differs";
    if (source.meta.height != destination.meta.height) return "Image height differs";
    if (source.meta.orig_size != destination.meta.orig_size) return "Image original size differs";
    if (source.meta.created_ts != destination.meta.created_ts)
        return "Image creation timestamp differs";
    if (source.meta.animated != destination.meta.animated) return "Image animated differs";
    return nullptr;
}

const char* video_metadata_difference(const IndexNode& source,
                                      const IndexNode& destination) noexcept
{
    if (source.vmeta.container != destination.vmeta.container) return "Video container differs";
    if (source.vmeta.codec != destination.vmeta.codec) return "Video codec differs";
    if (source.vmeta.width != destination.vmeta.width) return "Video width differs";
    if (source.vmeta.height != destination.vmeta.height) return "Video height differs";
    if (source.vmeta.duration_us != destination.vmeta.duration_us) return "Video duration differs";
    if (source.vmeta.orig_size != destination.vmeta.orig_size) return "Video original size differs";
    if (source.vmeta.created_ts != destination.vmeta.created_ts)
        return "Video creation timestamp differs";
    return nullptr;
}

const char* metadata_difference(const IndexNode& source, const IndexNode& destination) noexcept
{
    if (source.type != destination.type) return "Node type differs";
    if (source.name != destination.name) return "Node name differs";
    if (source.tags != destination.tags) return "Node tags differ";
    if (source.favorite != destination.favorite) return "Node favorite differs";
    if (source.sort_key != destination.sort_key) return "Node sort order differs";
    if (source.children.size() != destination.children.size()) return "Node children count differs";
    if (source.is_image()) return image_metadata_difference(source, destination);
    if (source.is_video()) return video_metadata_difference(source, destination);
    return nullptr;
}

const char* settings_difference(const VaultSettings& source,
                                const VaultSettings& destination) noexcept
{
    if (source.default_sort != destination.default_sort) return "Vault default sort differs";
    if (source.tiles_show_tags != destination.tiles_show_tags)
        return "Vault tiles show tags differs";
    if (source.categories != destination.categories) return "Vault categories differs";
    if (source.tag_descriptions != destination.tag_descriptions)
        return "Vault tag descriptions differs";
    if (source.tag_field_values != destination.tag_field_values)
        return "Vault tag field values differs";
    if (source.migrated_index_version != destination.migrated_index_version)
        return "Vault migrated index version differs";
    if (source.migrated_probe_caps != destination.migrated_probe_caps)
        return "Vault migrated probe caps differs";
    if (source.migrated_thumb_side != destination.migrated_thumb_side)
        return "Vault migrated thumb side differs";
    return nullptr;
}

bool logical_searches_equal(std::span<const SavedSearch> source,
                            std::span<const SavedSearch> destination) noexcept
{
    if (source.size() != destination.size()) return false;
    for (size_t i = 0; i < source.size(); ++i)
        if (source[i].name != destination[i].name ||
            !std::ranges::equal(source[i].query.as_span(), destination[i].query.as_span()))
            return false;
    return true;
}

struct LegacyVideoReader {
    const IndexNode& node;
    ChunkStore& store;
    crypto::SecureBytes& cached;
    size_t& cached_index;
    CancellationToken cancel;

    bool load(size_t index, uint64_t raw_index)
    {
        const auto& chunk = node.vmeta.chunks[index];
        if (const auto tag = chunk_tag(crypto::ChunkDomain::Video, node, chunk.id, chunk.sequence);
            !store.read_chunk({chunk.offset, chunk.length}, tag, cached))
            return false;
        const uint64_t start = raw_index * node.vmeta.chunk_size;
        if (const auto expected = static_cast<size_t>(
                std::min<uint64_t>(node.vmeta.chunk_size, node.vmeta.orig_size - start));
            cached.size() != expected)
            return false;
        cached_index = index;
        return true;
    }

    bool operator()(uint64_t offset, std::span<uint8_t> output)
    {
#ifdef OSV_VAULT_FIXTURE_DIR
        if (conversion_checkpoint) conversion_checkpoint(false, offset);
#endif
        if (offset > node.vmeta.orig_size || output.size() > node.vmeta.orig_size - offset)
            return false;
        size_t written = 0;
        while (written < output.size()) {
            if (cancel.requested()) return false;
            const uint64_t absolute = offset + written;
            const uint64_t raw_index = absolute / node.vmeta.chunk_size;
            if (raw_index >= node.vmeta.chunks.size()) return false;
            if (const auto index = static_cast<size_t>(raw_index);
                cached_index != index && !load(index, raw_index))
                return false;
            const auto within = static_cast<size_t>(absolute % node.vmeta.chunk_size);
            if (within >= cached.size()) return false;
            const size_t take = std::min(output.size() - written, cached.size() - within);
            std::copy_n(cached.data() + within, take, output.data() + written);
            written += take;
        }
        return true;
    }
};

LegacyConversionStatus verify_logical_node(Vault& source_vault, v3::ReadSession& destination_vault,
                                           const IndexNode& source, const IndexNode& destination,
                                           std::span<const uint8_t> digest_key,
                                           CancellationToken cancel, LegacyConversionReport& report,
                                           uint64_t& ordinal) noexcept
{
    report.failed_ordinal = ++ordinal;
    report.operation = "Compare node metadata";
    report.detail = "Folder structure or node metadata differs";
    if (cancel.requested()) return LegacyConversionStatus::Cancelled;
    if (const auto* difference = metadata_difference(source, destination)) {
        report.detail = difference;
        return LegacyConversionStatus::VerificationFailed;
    }
    if (!source.is_gallery()) {
        std::array<uint8_t, 32> source_digest{};
        std::array<uint8_t, 32> destination_digest{};
        report.operation = "Read source original for comparison";
        report.detail = "Source original could not be read or authenticated";
        if (const auto status =
                digest_legacy_original(source_vault, source, digest_key, source_digest, cancel);
            status != LegacyConversionStatus::Ok)
            return status;
        const auto role =
            source.is_image() ? v3::ObjectRole::OriginalImage : v3::ObjectRole::OriginalVideo;
        report.operation = "Compare original media";
        const auto destination_status = destination_vault.digest_object(
            destination.node_id, role, digest_key, destination_digest, cancel);
        report.backend_code = static_cast<int>(destination_status);
        const bool matches = source_digest == destination_digest;
        crypto_wipe(source_digest.data(), source_digest.size());
        crypto_wipe(destination_digest.data(), destination_digest.size());
        if (cancel.requested()) return LegacyConversionStatus::Cancelled;
        if (destination_status != v3::ReadStatus::Ok || !matches) {
            report.detail = destination_status != v3::ReadStatus::Ok
                                ? "Destination original could not be read or authenticated"
                                : "Original content digest mismatch";
            return LegacyConversionStatus::VerificationFailed;
        }
    }
    for (size_t i = 0; i < source.children.size(); ++i) {
        const auto status =
            verify_logical_node(source_vault, destination_vault, source.children[i],
                                destination.children[i], digest_key, cancel, report, ordinal);
        if (status != LegacyConversionStatus::Ok) return status;
    }
    return LegacyConversionStatus::Ok;
}

}  // namespace

LegacyConversionStatus copy_legacy_video_object(Vault& source, const IndexNode& source_node,
                                                v3::ReadSession& destination,
                                                const IndexNode& destination_node,
                                                CancellationToken cancel) noexcept
{
    if (!source.unlocked_ || source.v3_ || !source_node.is_video() ||
        !destination_node.is_video() || source_node.vmeta.orig_size == 0 ||
        source_node.vmeta.chunk_size == 0 || source_node.vmeta.chunks.empty())
        return LegacyConversionStatus::SourceCorrupt;

    crypto::SecureBytes cached;
    size_t cached_index = source_node.vmeta.chunks.size();
    ChunkStore store(source.read_fp_, source.master_key_.as_span(), framed_chunks(source.header_));
    LegacyVideoReader read{source_node, store, cached, cached_index, cancel};
    bool source_failed = false;
    const auto status = destination.stage_object_stream(
        destination_node.node_id, v3::ObjectRole::OriginalVideo,
        std::to_underlying(source_node.vmeta.container), source_node.vmeta.orig_size,
        [&](uint64_t offset, std::span<uint8_t> output) {
            const bool ok = read(offset, output);
            source_failed = source_failed || !ok;
            return ok;
        });
    if (cancel.requested()) return LegacyConversionStatus::Cancelled;
    if (source_failed) return LegacyConversionStatus::SourceCorrupt;
    return status == v3::ReadStatus::Ok ? LegacyConversionStatus::Ok
                                        : LegacyConversionStatus::IoError;
}

LegacyConversionStatus digest_legacy_original(Vault& source, const IndexNode& source_node,
                                              std::span<const uint8_t> digest_key,
                                              std::span<uint8_t> digest,
                                              CancellationToken cancel) noexcept
{
    if (!source.unlocked_ || source.v3_ || source_node.is_gallery() || digest.empty() ||
        digest.size() > 64 || digest_key.empty() || digest_key.size() > 64)
        return LegacyConversionStatus::SourceCorrupt;
    crypto_blake2b_ctx hash{};
    crypto_blake2b_keyed_init(&hash, digest.size(), digest_key.data(), digest_key.size());
    if (source_node.is_image()) {
        if (cancel.requested()) {
            crypto_wipe(&hash, sizeof(hash));
            return LegacyConversionStatus::Cancelled;
        }
        crypto::SecureBytes original;
        if (source.read_image(source_node, original) != VaultResult::Ok) {
            crypto_wipe(&hash, sizeof(hash));
            return LegacyConversionStatus::SourceCorrupt;
        }
        crypto_blake2b_update(&hash, original.data(), original.size());
    } else {
        ChunkStore store(source.read_fp_, source.master_key_.as_span(),
                         framed_chunks(source.header_));
        for (const auto& chunk : source_node.vmeta.chunks) {
            if (cancel.requested()) {
                crypto_wipe(&hash, sizeof(hash));
                return LegacyConversionStatus::Cancelled;
            }
            crypto::SecureBytes plaintext;
            if (const auto tag =
                    chunk_tag(crypto::ChunkDomain::Video, source_node, chunk.id, chunk.sequence);
                !store.read_chunk({chunk.offset, chunk.length}, tag, plaintext)) {
                crypto_wipe(&hash, sizeof(hash));
                return LegacyConversionStatus::SourceCorrupt;
            }
            crypto_blake2b_update(&hash, plaintext.data(), plaintext.size());
        }
    }
    crypto_blake2b_final(&hash, digest.data());
    return LegacyConversionStatus::Ok;
}

class LegacyConverter {
public:
    LegacyConverter(Vault& source, const LegacyConversionRequest& request)
        : source_(source), request_(request)
    {}

    LegacyConversionReport run() const noexcept
    {
        LegacyConversionReport report;
        if (!preflight(report)) return report;
        Vault destination;
        if (!open_destination(destination, report)) return report;
        if (!copy(destination, report)) return report;
        if (!verify(destination, report)) return report;
        report.failed_ordinal = 0;
        report.status = LegacyConversionStatus::Ok;
        return report;
    }

private:
    bool preflight(LegacyConversionReport& report) const noexcept
    {
        using enum LegacyConversionStatus;
        if (!source_.unlocked_) return fail(report, SourceLocked);
        if (source_.v3_) return fail(report, SourceNotLegacy);
        PreflightTotals totals;
        scan_preflight(source_.root_, totals);
        if (request_.progress) {
            request_.progress->done = 0;
            request_.progress->total = static_cast<int>(std::min<uint64_t>(
                totals.nodes, static_cast<uint64_t>(std::numeric_limits<int>::max())));
            request_.progress->expanding = false;
        }
        report.operation = "Estimate destination free space";
        return preflight_space(platform::utf8_to_path(source_.path_), request_.destination, totals,
                               report) ||
               fail(report, InsufficientSpace);
    }

    bool open_destination(Vault& destination, LegacyConversionReport& report) const noexcept
    {
        report.operation = "Validate destination path";
        const auto validation =
            validate_destination(platform::utf8_to_path(source_.path_), request_.destination);
        if (validation != LegacyConversionStatus::Ok &&
            validation != LegacyConversionStatus::DestinationExists)
            return fail(report, validation);
        const auto path = platform::path_to_utf8(request_.destination);
        const auto marker = source_marker(source_.header_);
        if (validation == LegacyConversionStatus::DestinationExists)
            return resume_destination(destination, path, marker, report);
        report.operation = "Create destination vault and conversion marker";
        const auto created =
            record_result(report, Vault::create_directory(path, request_.password, request_.keyfile,
                                                          request_.kdf, destination));
        if (created == VaultResult::Ok && destination.v3_ &&
            record_result(report, destination.v3_->begin_conversion(marker)) == v3::DbStatus::Ok)
            return true;
        return fail(report, created == VaultResult::AlreadyExists
                                ? LegacyConversionStatus::DestinationExists
                                : LegacyConversionStatus::IoError);
    }

    bool resume_destination(Vault& destination, const std::string& path,
                            const v3::ConversionMarker& marker,
                            LegacyConversionReport& report) const noexcept
    {
        report.operation = "Open incomplete destination for resume";
        if (record_result(report, Vault::open(path, destination)) != VaultResult::Ok)
            return fail(report, LegacyConversionStatus::DestinationExists);
        report.operation = "Unlock incomplete destination for resume";
        const auto unlocked = record_result(
            report, destination.unlock_directory(request_.password, request_.keyfile, true));
        if (unlocked == VaultResult::AuthFailed)
            return fail(report, LegacyConversionStatus::AuthenticationFailed);
        if (unlocked != VaultResult::Ok || !destination.v3_)
            return fail(report, LegacyConversionStatus::DestinationExists);
        report.operation = "Validate resume source fingerprint";
        if (const auto existing = destination.v3_->conversion_marker();
            record_result(report, existing.status) != v3::DbStatus::Ok || !existing.value ||
            existing.value->conversion_version != marker.conversion_version ||
            existing.value->source_fingerprint != marker.source_fingerprint)
            return fail(report, LegacyConversionStatus::ResumeMismatch);
        report.resumed = true;
        return true;
    }

    bool copy(Vault& destination, LegacyConversionReport& report) const noexcept
    {
        IndexNode root;
        const v3::Id root_id = destination.root_.node_id;
        CopyContext context{source_,           *destination.v3_,        request_, report,
                            source_.settings_, source_.saved_searches_, &root};
        if (const auto status = copy_node(context, source_.root_, root, &root_id);
            status != LegacyConversionStatus::Ok) {
            return fail(report, status);
        }
        if (const auto status = commit_batch(context); status != LegacyConversionStatus::Ok)
            return fail(report, status);
        destination.root_ = std::move(root);
        destination.settings_ = source_.settings_;
        destination.saved_searches_ = source_.saved_searches_;
        return true;
    }

    bool check_destination(v3::ReadSession& session, LegacyConversionReport& report) const noexcept
    {
        const auto deep = session.verify(v3::VerifyDepth::Deep, cancellation_token(request_));
        report.backend_code = static_cast<int>(deep.status);
        report.objects_checked = deep.objects_checked;
        report.authenticated_bytes = deep.authenticated_bytes;
        if (deep.status == v3::RecoveryStatus::Ok && !deep.has_corruption()) return true;
        report.detail = "Destination integrity check failed";
        switch (deep.status) {
        case v3::RecoveryStatus::DatabaseError:
            report.detail = "Encrypted database integrity check failed";
            break;
        case v3::RecoveryStatus::FilesystemError:
            report.detail = "Cannot read destination filesystem";
            break;
        case v3::RecoveryStatus::Cancelled:
            report.detail = "Integrity check cancelled";
            break;
        default:
            break;
        }
        for (const auto& finding : deep.findings) {
            using enum v3::IntegrityFindingKind;
            switch (finding.kind) {
            case MissingReferencedObject:
                report.detail = "Referenced encrypted object is missing";
                break;
            case ReferencedObjectSizeMismatch:
                report.detail = "Encrypted object size differs from database";
                break;
            case UnauthenticObject:
                report.detail = "Encrypted object authentication failed";
                break;
            case MalformedObject:
                report.detail = "Encrypted object has malformed framing";
                break;
            case InvalidMediaReferences:
                report.detail = "Database contains invalid media references";
                break;
            case UnreferencedGarbage:
                continue;
            }
            report.failed_object = finding.object_id;
            break;
        }
        return fail(report, cancelled(request_) ? LegacyConversionStatus::Cancelled
                                                : LegacyConversionStatus::VerificationFailed);
    }

    bool verify(Vault& destination, LegacyConversionReport& report) const noexcept
    {
#ifdef OSV_VAULT_FIXTURE_DIR
        if (conversion_checkpoint) conversion_checkpoint(true, 0);
#endif
        report.failed_ordinal = 0;
        report.backend_code = -1;
        report.operation = "Verify written destination";
        if (!check_destination(*destination.v3_, report)) return false;
        report.deep_verified = true;
        report.operation = "Clean up resumed destination";
        report.detail = "Garbage collection could not complete";
        if (report.resumed && record_result(report, destination.v3_->garbage_collect(0).status) !=
                                  v3::RecoveryStatus::Ok)
            return fail(report, LegacyConversionStatus::VerificationFailed);
        destination.lock();
        return cold_verify(report);
    }

    bool cold_verify(LegacyConversionReport& report) const noexcept
    {
        Vault reopened;
        report.operation = "Reopen destination from disk";
        report.detail = "Destination header or directory could not be opened";
        if (record_result(report, Vault::open(platform::path_to_utf8(request_.destination),
                                              reopened)) != VaultResult::Ok)
            return fail(report, LegacyConversionStatus::VerificationFailed);
        report.operation = "Unlock reopened destination";
        report.detail = "Destination database or wrapped key could not be unlocked";
        const auto unlocked = record_result(
            report, reopened.unlock_directory(request_.password, request_.keyfile, true));
        if (unlocked != VaultResult::Ok)
            return fail(report, unlocked == VaultResult::AuthFailed
                                    ? LegacyConversionStatus::AuthenticationFailed
                                    : LegacyConversionStatus::VerificationFailed);
        report.operation = "Verify reopened destination";
        if (!check_destination(*reopened.v3_, report)) return false;
        report.cold_reopened = true;
        if (!logical_verify(reopened, report)) return false;
        report.operation = "Mark conversion complete";
        report.detail = "Could not commit conversion completion to the database";
        if (record_result(report, reopened.v3_->finish_conversion()) != v3::DbStatus::Ok)
            return fail(report, LegacyConversionStatus::VerificationFailed);
        return true;
    }

    bool logical_verify(Vault& reopened, LegacyConversionReport& report) const noexcept
    {
        using enum LegacyConversionStatus;
        report.operation = "Prepare original comparison";
        report.detail = "System random generator failed";
        std::array<uint8_t, 32> key{};
        if (!crypto::fill_random(key)) return fail(report, IoError);
        uint64_t ordinal = 0;
        report.backend_code = -1;
        const auto logical =
            verify_logical_node(source_, *reopened.v3_, source_.root_, reopened.root_, key,
                                cancellation_token(request_), report, ordinal);
        crypto_wipe(key.data(), key.size());
        if (logical != Ok) return fail(report, logical);
        report.failed_ordinal = 0;
        report.operation = "Compare vault settings";
        report.detail = "Copied vault settings differ from the source";
        if (const auto* difference = settings_difference(source_.settings_, reopened.settings_)) {
            report.detail = difference;
            return fail(report, VerificationFailed);
        }
        report.operation = "Compare saved searches";
        report.detail = "Copied saved searches differ from the source";
        if (!logical_searches_equal(source_.saved_searches_, reopened.saved_searches_))
            return fail(report, VerificationFailed);
        report.logical_verified = true;
        return true;
    }

    bool fail(LegacyConversionReport& report, LegacyConversionStatus status) const noexcept
    {
        if (cancelled(request_)) status = LegacyConversionStatus::Cancelled;
        report.status = status;
        if (report.detail[0] == '\0' || status == LegacyConversionStatus::Cancelled)
            report.detail = conversion_failure_reason(status);
        platform::safe_println(stderr,
                               "[Converter] {}: {} (status {}, backend {}, item {}, objects {}, "
                               "authenticated bytes {})",
                               report.operation, report.detail, static_cast<int>(status),
                               report.backend_code, report.failed_ordinal, report.objects_checked,
                               report.authenticated_bytes);
        if (std::ranges::any_of(report.failed_object, [](uint8_t byte) { return byte != 0; })) {
            std::array<char, 33> hex{};
            constexpr std::string_view digits = "0123456789abcdef";
            for (size_t i = 0; i < report.failed_object.size(); ++i) {
                hex[2 * i] = digits[report.failed_object[i] >> 4];
                hex[2 * i + 1] = digits[report.failed_object[i] & 15];
            }
            platform::safe_println(stderr, "[Converter] failed encrypted object {}", hex.data());
        }
        return false;
    }

    Vault& source_;
    const LegacyConversionRequest& request_;
};

LegacyConversionReport convert_legacy_vault(Vault& source,
                                            const LegacyConversionRequest& request) noexcept
{
    return LegacyConverter{source, request}.run();
}

}  // namespace vault
