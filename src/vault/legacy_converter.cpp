#include "vault/legacy_converter.h"

#include "crypto/random.h"
#include "platform/path_utf8.h"
#include "vault/chunk_store.h"
#include "vault/v3_read_session.h"
#include "vault/vault.h"

#include "monocypher.h"

#include <algorithm>
#include <limits>
#include <system_error>

namespace vault {
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

bool cancelled(const LegacyConversionRequest& request) noexcept
{
    return (request.cancel && request.cancel->load(std::memory_order_relaxed)) ||
           (request.progress && request.progress->cancel.load(std::memory_order_relaxed));
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
    if (destination.empty()) return LegacyConversionStatus::InvalidDestination;
    std::error_code ec;
    const auto source_path = normalized_for_compare(source, ec);
    if (ec) return LegacyConversionStatus::InvalidDestination;
    const auto destination_path = normalized_for_compare(destination, ec);
    if (ec || source_path == destination_path || path_prefix(source_path, destination_path) ||
        path_prefix(destination_path, source_path))
        return LegacyConversionStatus::InvalidDestination;
    if (std::filesystem::exists(destination_path, ec))
        return ec ? LegacyConversionStatus::IoError : LegacyConversionStatus::DestinationExists;
    return ec ? LegacyConversionStatus::IoError : LegacyConversionStatus::Ok;
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
};

LegacyConversionStatus stage_media(CopyContext& context, const IndexNode& source,
                                   IndexNode& destination) noexcept
{
    if (cancelled(context.request)) return LegacyConversionStatus::Cancelled;
    const uint8_t format = source.is_image() ? std::to_underlying(source.meta.format)
                                             : std::to_underlying(source.vmeta.container);
    if (source.is_video()) {
        const auto status =
            copy_legacy_video_object(context.source, source, context.destination, destination);
        if (status != LegacyConversionStatus::Ok) return status;
        context.report.original_bytes += source.vmeta.orig_size;
    } else {
        crypto::SecureBytes original;
        if (context.source.read_image(source, original) != VaultResult::Ok)
            return LegacyConversionStatus::SourceCorrupt;
        if (context.destination.stage_object(destination.node_id, v3::ObjectRole::OriginalImage,
                                             format, original.as_span()) != v3::ReadStatus::Ok)
            return LegacyConversionStatus::IoError;
        context.report.original_bytes += original.size();
    }

    const bool has_derived =
        source.is_image() ? source.meta.thumb_length != 0 : source.vmeta.poster_length != 0;
    if (!has_derived) return LegacyConversionStatus::Ok;
    crypto::SecureBytes derived;
    if (context.source.read_thumbnail(source, derived) != VaultResult::Ok)
        return LegacyConversionStatus::SourceCorrupt;
    const auto derived_role =
        source.is_image() ? v3::ObjectRole::Thumbnail : v3::ObjectRole::Poster;
    if (context.destination.stage_object(destination.node_id, derived_role, format,
                                         derived.as_span()) != v3::ReadStatus::Ok)
        return LegacyConversionStatus::IoError;
    context.report.derived_bytes += derived.size();
    return LegacyConversionStatus::Ok;
}

LegacyConversionStatus copy_node(CopyContext& context, const IndexNode& source,
                                 IndexNode& destination, const v3::Id* root_id = nullptr) noexcept
{
    if (cancelled(context.request)) return LegacyConversionStatus::Cancelled;
    try {
        destination = clone_metadata(source, root_id);
    } catch (...) {
        return LegacyConversionStatus::IoError;
    }
    if (std::ranges::all_of(destination.node_id, [](uint8_t byte) { return byte == 0; }))
        return LegacyConversionStatus::IoError;

    if (source.is_gallery()) {
        ++context.report.galleries;
        try {
            destination.children.reserve(source.children.size());
            for (const auto& child : source.children) {
                destination.children.emplace_back();
                const auto status = copy_node(context, child, destination.children.back());
                if (status != LegacyConversionStatus::Ok) return status;
            }
        } catch (...) {
            return LegacyConversionStatus::IoError;
        }
        if (context.request.progress) ++context.request.progress->done;
        return LegacyConversionStatus::Ok;
    }

    if (source.is_image())
        ++context.report.images;
    else
        ++context.report.videos;
    const auto status = stage_media(context, source, destination);
    if (status == LegacyConversionStatus::Ok && context.request.progress)
        ++context.request.progress->done;
    return status;
}

}  // namespace

LegacyConversionStatus copy_legacy_video_object(Vault& source, const IndexNode& source_node,
                                                v3::ReadSession& destination,
                                                const IndexNode& destination_node) noexcept
{
    if (!source.unlocked_ || source.v3_ || !source_node.is_video() ||
        !destination_node.is_video() || source_node.vmeta.orig_size == 0 ||
        source_node.vmeta.chunk_size == 0 || source_node.vmeta.chunks.empty())
        return LegacyConversionStatus::SourceCorrupt;

    crypto::SecureBytes cached;
    size_t cached_index = source_node.vmeta.chunks.size();
    ChunkStore store(source.read_fp_, source.master_key_.as_span(), framed_chunks(source.header_));
    const auto read = [&](uint64_t offset, std::span<uint8_t> output) mutable {
        if (offset > source_node.vmeta.orig_size ||
            output.size() > source_node.vmeta.orig_size - offset)
            return false;
        size_t written = 0;
        while (written < output.size()) {
            const uint64_t absolute = offset + written;
            const uint64_t raw_index = absolute / source_node.vmeta.chunk_size;
            if (raw_index >= source_node.vmeta.chunks.size()) return false;
            const size_t index = static_cast<size_t>(raw_index);
            if (cached_index != index) {
                const auto& chunk = source_node.vmeta.chunks[index];
                const auto tag =
                    chunk_tag(crypto::ChunkDomain::Video, source_node, chunk.id, chunk.sequence);
                if (!store.read_chunk({chunk.offset, chunk.length}, tag, cached)) return false;
                const uint64_t start = raw_index * source_node.vmeta.chunk_size;
                const auto expected = static_cast<size_t>(std::min<uint64_t>(
                    source_node.vmeta.chunk_size, source_node.vmeta.orig_size - start));
                if (cached.size() != expected) return false;
                cached_index = index;
            }
            const size_t within = static_cast<size_t>(absolute % source_node.vmeta.chunk_size);
            if (within >= cached.size()) return false;
            const size_t take = std::min(output.size() - written, cached.size() - within);
            std::copy_n(cached.data() + within, take, output.data() + written);
            written += take;
        }
        return true;
    };
    const auto status = destination.stage_object_stream(
        destination_node.node_id, v3::ObjectRole::OriginalVideo,
        std::to_underlying(source_node.vmeta.container), source_node.vmeta.orig_size, read);
    return status == v3::ReadStatus::Ok ? LegacyConversionStatus::Ok
                                        : LegacyConversionStatus::SourceCorrupt;
}

LegacyConversionReport convert_legacy_vault(Vault& source,
                                            const LegacyConversionRequest& request) noexcept
{
    LegacyConversionReport report;
    if (!source.unlocked_) {
        report.status = LegacyConversionStatus::SourceLocked;
        return report;
    }
    if (source.v3_) {
        report.status = LegacyConversionStatus::SourceNotLegacy;
        return report;
    }
    PreflightTotals totals;
    scan_preflight(source.root_, totals);
    if (request.progress) {
        request.progress->done = 0;
        request.progress->total = static_cast<int>(std::min<uint64_t>(
            totals.nodes, static_cast<uint64_t>(std::numeric_limits<int>::max())));
        request.progress->expanding = false;
    }
    if (!preflight_space(platform::utf8_to_path(source.path_), request.destination, totals,
                         report)) {
        report.status = LegacyConversionStatus::InsufficientSpace;
        return report;
    }
    const auto validation =
        validate_destination(platform::utf8_to_path(source.path_), request.destination);
    if (validation != LegacyConversionStatus::Ok &&
        validation != LegacyConversionStatus::DestinationExists) {
        report.status = validation;
        return report;
    }

    Vault destination;
    const auto destination_utf8 = platform::path_to_utf8(request.destination);
    const auto marker = source_marker(source.header_);
    if (validation == LegacyConversionStatus::DestinationExists) {
        if (Vault::open(destination_utf8, destination) != VaultResult::Ok ||
            destination.unlock(request.password, request.keyfile) != VaultResult::Ok ||
            !destination.v3_) {
            report.status = LegacyConversionStatus::DestinationExists;
            return report;
        }
        const auto existing = destination.v3_->conversion_marker();
        if (existing.status != v3::DbStatus::Ok || !existing.value ||
            existing.value->conversion_version != marker.conversion_version ||
            existing.value->source_fingerprint != marker.source_fingerprint) {
            report.status = LegacyConversionStatus::ResumeMismatch;
            return report;
        }
        report.resumed = true;
    } else {
        const auto created = Vault::create_directory(destination_utf8, request.password,
                                                     request.keyfile, request.kdf, destination);
        if (created != VaultResult::Ok || !destination.v3_ ||
            destination.v3_->begin_conversion(marker) != v3::DbStatus::Ok) {
            report.status = created == VaultResult::AlreadyExists
                                ? LegacyConversionStatus::DestinationExists
                                : LegacyConversionStatus::IoError;
            return report;
        }
    }

    IndexNode converted_root;
    const v3::Id root_id = destination.root_.node_id;
    CopyContext context{source, *destination.v3_, request, report};
    if (const auto status = copy_node(context, source.root_, converted_root, &root_id);
        status != LegacyConversionStatus::Ok) {
        report.status = status;
        return report;
    }
    if (destination.v3_->commit_metadata(converted_root, source.settings_,
                                         source.saved_searches_) != v3::ReadStatus::Ok) {
        report.status = LegacyConversionStatus::IoError;
        return report;
    }
    destination.root_ = converted_root;
    destination.settings_ = source.settings_;
    destination.saved_searches_ = source.saved_searches_;

    const auto verified = destination.v3_->verify(v3::VerifyDepth::Deep);
    if (verified.status != v3::RecoveryStatus::Ok || verified.has_corruption()) {
        report.status = LegacyConversionStatus::VerificationFailed;
        return report;
    }
    report.deep_verified = true;
    if (report.resumed) {
        const auto garbage = destination.v3_->garbage_collect(0);
        if (garbage.status != v3::RecoveryStatus::Ok) {
            report.status = LegacyConversionStatus::VerificationFailed;
            return report;
        }
    }
    destination.lock();

    Vault reopened;
    if (Vault::open(destination_utf8, reopened) != VaultResult::Ok) {
        report.status = LegacyConversionStatus::VerificationFailed;
        return report;
    }
    const auto unlocked = reopened.unlock(request.password, request.keyfile);
    if (unlocked == VaultResult::AuthFailed) {
        report.status = LegacyConversionStatus::AuthenticationFailed;
        return report;
    }
    if (unlocked != VaultResult::Ok ||
        reopened.v3_->verify(v3::VerifyDepth::Deep).status != v3::RecoveryStatus::Ok ||
        reopened.v3_->finish_conversion() != v3::DbStatus::Ok) {
        report.status = LegacyConversionStatus::VerificationFailed;
        return report;
    }
    report.cold_reopened = true;
    report.status = LegacyConversionStatus::Ok;
    return report;
}

}  // namespace vault
