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
    } else if (node.is_video()) {
        add_checked(node.vmeta.orig_size, totals.plaintext_bytes, totals.valid);
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
    if (ec || !totals.valid ||
        content_bytes > (std::numeric_limits<uint64_t>::max() - FIXED_SLACK) / 2)
        return false;
    report.required_free_bytes = content_bytes * 2 + FIXED_SLACK;
    const auto parent =
        destination.parent_path().empty() ? std::filesystem::path{"."} : destination.parent_path();
    const auto info = std::filesystem::space(parent, ec);
    return !ec && info.available >= report.required_free_bytes;
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
    } else {
        (void)crypto::fill_random(result.node_id);
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
    const auto mapped = context.destination.conversion_node_id(source_key);
    if (mapped.status != v3::DbStatus::Ok) return LegacyConversionStatus::IoError;
    if (mapped.value) {
        destination.node_id = *mapped.value;
        return LegacyConversionStatus::Ok;
    }
    destination.node_id = root_id ? *root_id : v3::Id{};
    if (!root_id && !crypto::fill_random(destination.node_id))
        return LegacyConversionStatus::IoError;
    if (context.destination.record_conversion_node({source_key, destination.node_id}) !=
        v3::DbStatus::Ok)
        return LegacyConversionStatus::IoError;
    return LegacyConversionStatus::Ok;
}

LegacyConversionStatus commit_batch(CopyContext& context) noexcept
{
    if (!context.root || context.since_commit == 0) return LegacyConversionStatus::Ok;
    if (context.destination.commit_metadata(*context.root, context.settings,
                                            context.saved_searches) != v3::ReadStatus::Ok)
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
        if (const auto status =
                copy_legacy_video_object(context.source, source, context.destination, destination,
                                         cancellation_token(context.request));
            status != LegacyConversionStatus::Ok)
            return status;
        context.report.original_bytes += source.vmeta.orig_size;
    } else {
        crypto::SecureBytes original;
        if (context.source.read_image(source, original) != VaultResult::Ok)
            return LegacyConversionStatus::SourceCorrupt;
        const auto read = [&](uint64_t offset, std::span<uint8_t> output) {
            if (cancelled(context.request)) return false;
            std::ranges::copy(
                original.as_span().subspan(static_cast<size_t>(offset), output.size()),
                output.begin());
            return true;
        };
        if (context.destination.stage_object_stream(destination.node_id,
                                                    v3::ObjectRole::OriginalImage, format,
                                                    original.size(), read) != v3::ReadStatus::Ok)
            return cancelled(context.request) ? LegacyConversionStatus::Cancelled
                                              : LegacyConversionStatus::IoError;
        context.report.original_bytes += original.size();
    }

    if (const bool has_derived =
        source.is_image() ? source.meta.thumb_length != 0 : source.vmeta.poster_length != 0;
        !has_derived) {
        ++context.report.missing_derived;
        return LegacyConversionStatus::Ok;
    }
    crypto::SecureBytes derived;
    if (context.source.read_thumbnail(source, derived) != VaultResult::Ok)
        return LegacyConversionStatus::SourceCorrupt;
    const auto derived_role =
        source.is_image() ? v3::ObjectRole::Thumbnail : v3::ObjectRole::Poster;
    if (const auto existing =
            context.destination.object_plaintext_length(destination.node_id, derived_role);
        existing.has_value()) {
        context.report.derived_bytes += *existing;
        return LegacyConversionStatus::Ok;
    }
    if (context.destination.stage_object(destination.node_id, derived_role, format,
                                         derived.as_span()) != v3::ReadStatus::Ok)
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

bool logical_metadata_equal(const IndexNode& source, const IndexNode& destination) noexcept
{
    if (source.type != destination.type || source.name != destination.name ||
        source.tags != destination.tags || source.favorite != destination.favorite ||
        source.sort_key != destination.sort_key ||
        source.children.size() != destination.children.size())
        return false;
    if (source.is_image())
        return source.meta.format == destination.meta.format &&
               source.meta.width == destination.meta.width &&
               source.meta.height == destination.meta.height &&
               source.meta.orig_size == destination.meta.orig_size &&
               source.meta.created_ts == destination.meta.created_ts &&
               source.meta.animated == destination.meta.animated;
    if (source.is_video())
        return source.vmeta.container == destination.vmeta.container &&
               source.vmeta.codec == destination.vmeta.codec &&
               source.vmeta.width == destination.vmeta.width &&
               source.vmeta.height == destination.vmeta.height &&
               source.vmeta.duration_us == destination.vmeta.duration_us &&
               source.vmeta.orig_size == destination.vmeta.orig_size &&
               source.vmeta.created_ts == destination.vmeta.created_ts;
    return true;
}

bool logical_settings_equal(const VaultSettings& source, const VaultSettings& destination) noexcept
{
    return source.default_sort == destination.default_sort &&
           source.tiles_show_tags == destination.tiles_show_tags &&
           source.categories == destination.categories &&
           source.tag_descriptions == destination.tag_descriptions &&
           source.tag_field_values == destination.tag_field_values &&
           source.migrated_index_version == destination.migrated_index_version &&
           source.migrated_probe_caps == destination.migrated_probe_caps &&
           source.migrated_thumb_side == destination.migrated_thumb_side;
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
                                           CancellationToken cancel) noexcept
{
    if (cancel.requested()) return LegacyConversionStatus::Cancelled;
    if (!logical_metadata_equal(source, destination))
        return LegacyConversionStatus::VerificationFailed;
    if (!source.is_gallery()) {
        std::array<uint8_t, 32> source_digest{};
        std::array<uint8_t, 32> destination_digest{};
        if (const auto status =
                digest_legacy_original(source_vault, source, digest_key, source_digest, cancel);
            status != LegacyConversionStatus::Ok)
            return status;
        const auto role =
            source.is_image() ? v3::ObjectRole::OriginalImage : v3::ObjectRole::OriginalVideo;
        const auto destination_status = destination_vault.digest_object(
            destination.node_id, role, digest_key, destination_digest, cancel);
        const bool matches = source_digest == destination_digest;
        crypto_wipe(source_digest.data(), source_digest.size());
        crypto_wipe(destination_digest.data(), destination_digest.size());
        if (cancel.requested()) return LegacyConversionStatus::Cancelled;
        if (destination_status != v3::ReadStatus::Ok || !matches)
            return LegacyConversionStatus::VerificationFailed;
    }
    for (size_t i = 0; i < source.children.size(); ++i) {
        const auto status = verify_logical_node(source_vault, destination_vault, source.children[i],
                                                destination.children[i], digest_key, cancel);
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
    const auto status = destination.stage_object_stream(
        destination_node.node_id, v3::ObjectRole::OriginalVideo,
        std::to_underlying(source_node.vmeta.container), source_node.vmeta.orig_size, read);
    if (cancel.requested()) return LegacyConversionStatus::Cancelled;
    return status == v3::ReadStatus::Ok ? LegacyConversionStatus::Ok
                                        : LegacyConversionStatus::SourceCorrupt;
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
        return preflight_space(platform::utf8_to_path(source_.path_), request_.destination, totals,
                               report) ||
               fail(report, InsufficientSpace);
    }

    bool open_destination(Vault& destination, LegacyConversionReport& report) const noexcept
    {
    const auto validation =
            validate_destination(platform::utf8_to_path(source_.path_), request_.destination);
    if (validation != LegacyConversionStatus::Ok &&
            validation != LegacyConversionStatus::DestinationExists)
            return fail(report, validation);
        const auto path = platform::path_to_utf8(request_.destination);
        const auto marker = source_marker(source_.header_);
        if (validation == LegacyConversionStatus::DestinationExists)
            return resume_destination(destination, path, marker, report);
        const auto created = Vault::create_directory(path, request_.password, request_.keyfile,
                                                     request_.kdf, destination);
        if (created == VaultResult::Ok && destination.v3_ &&
            destination.v3_->begin_conversion(marker) == v3::DbStatus::Ok)
            return true;
        return fail(report, created == VaultResult::AlreadyExists
                                ? LegacyConversionStatus::DestinationExists
                                : LegacyConversionStatus::IoError);
    }

    bool resume_destination(Vault& destination, const std::string& path,
                            const v3::ConversionMarker& marker,
                            LegacyConversionReport& report) const noexcept
    {
        if (Vault::open(path, destination) != VaultResult::Ok)
            return fail(report, LegacyConversionStatus::DestinationExists);
        const auto unlocked =
            destination.unlock_directory(request_.password, request_.keyfile, true);
        if (unlocked == VaultResult::AuthFailed)
            return fail(report, LegacyConversionStatus::AuthenticationFailed);
        if (unlocked != VaultResult::Ok || !destination.v3_)
            return fail(report, LegacyConversionStatus::DestinationExists);
        if (const auto existing = destination.v3_->conversion_marker();
            existing.status != v3::DbStatus::Ok || !existing.value ||
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
        if (status == LegacyConversionStatus::SourceCorrupt)
                platform::safe_println(stderr,
                                       "[Converter] source item ordinal {} failed authentication",
                                   report.failed_ordinal);
            return fail(report, status);
    }
        if (const auto status = commit_batch(context); status != LegacyConversionStatus::Ok)
            return fail(report, status);
        destination.root_ = std::move(root);
        destination.settings_ = source_.settings_;
        destination.saved_searches_ = source_.saved_searches_;
        return true;
    }

    bool verify(Vault& destination, LegacyConversionReport& report) const noexcept
    {
#ifdef OSV_VAULT_FIXTURE_DIR
        if (conversion_checkpoint) conversion_checkpoint(true, 0);
#endif
        if (const auto deep =
                destination.v3_->verify(v3::VerifyDepth::Deep, cancellation_token(request_));
            deep.status != v3::RecoveryStatus::Ok || deep.has_corruption())
            return fail(report, cancelled(request_) ? LegacyConversionStatus::Cancelled
                                                    : LegacyConversionStatus::VerificationFailed);
        report.deep_verified = true;
        if (report.resumed && destination.v3_->garbage_collect(0).status != v3::RecoveryStatus::Ok)
            return fail(report, cancelled(request_) ? LegacyConversionStatus::Cancelled
                                                    : LegacyConversionStatus::VerificationFailed);
    destination.lock();
        return cold_verify(report);
    }

    bool cold_verify(LegacyConversionReport& report) const noexcept
    {
    Vault reopened;
        if (const auto path = platform::path_to_utf8(request_.destination);
            Vault::open(path, reopened) != VaultResult::Ok)
            return fail(report, cancelled(request_) ? LegacyConversionStatus::Cancelled
                                                    : LegacyConversionStatus::VerificationFailed);
        const auto unlocked = reopened.unlock_directory(request_.password, request_.keyfile, true);
        if (unlocked == VaultResult::AuthFailed)
            return fail(report, LegacyConversionStatus::AuthenticationFailed);
        if (unlocked != VaultResult::Ok ||
            reopened.v3_->verify(v3::VerifyDepth::Deep, cancellation_token(request_)).status !=
                v3::RecoveryStatus::Ok)
            return fail(report, cancelled(request_) ? LegacyConversionStatus::Cancelled
                                                    : LegacyConversionStatus::VerificationFailed);
        report.cold_reopened = true;
        if (!logical_verify(reopened, report)) return false;
        if (reopened.v3_->finish_conversion() != v3::DbStatus::Ok)
            return fail(report, cancelled(request_) ? LegacyConversionStatus::Cancelled
                                                    : LegacyConversionStatus::VerificationFailed);
        return true;
    }

    bool logical_verify(Vault& reopened, LegacyConversionReport& report) const noexcept
    {
        using enum LegacyConversionStatus;
        std::array<uint8_t, 32> key{};
        if (!crypto::fill_random(key)) return fail(report, IoError);
        const auto logical = verify_logical_node(source_, *reopened.v3_, source_.root_,
                                                 reopened.root_, key, cancellation_token(request_));
        crypto_wipe(key.data(), key.size());
        if (logical != Ok ||
            !logical_settings_equal(source_.settings_, reopened.settings_) ||
            !logical_searches_equal(source_.saved_searches_, reopened.saved_searches_))
            return fail(report, logical == Ok ? VerificationFailed : logical);
    report.logical_verified = true;
        return true;
    }

    static bool fail(LegacyConversionReport& report, LegacyConversionStatus status) noexcept
    {
        report.status = status;
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
