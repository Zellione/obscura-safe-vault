#include <chrono>
#include <cstdio>

#include "chunk_store.h"
#include "safe_name.h"
#include "vault.h"
#include "staging.h"
#include "vault_ops.h"

#include "image/decode.h"
#include "image/anim_info.h"
#include "image/thumbnail.h"
#include "media/video_probe.h"
#include "platform/error_log.h"

namespace vault {

namespace {
    // Decode image and generate thumbnail from decoded pixels (pure CPU, no lock)
    struct DecodedThumb {
        ImageFormat format;
        uint32_t width;
        uint32_t height;
        bool animated;
        std::vector<uint8_t> thumb_bytes;
    };

    DecodedThumb decode_and_thumbnail(std::span<const uint8_t> file_data)
    {
        DecodedThumb result{ImageFormat::Unknown, 0, 0, false, {}};
        if (auto decoded = image::decode_from_memory(file_data)) {
            result.format = static_cast<ImageFormat>(decoded->format);
            result.width = static_cast<uint32_t>(decoded->width);
            result.height = static_cast<uint32_t>(decoded->height);
            result.animated = image::is_animated(decoded->format, file_data);
            if (auto thumb_jpeg = image::make_thumbnail(*decoded, 256, 85)) {
                result.thumb_bytes = *thumb_jpeg;
            }
        }
        return result;
    }
}  // namespace

StagedNode stage_image(Vault& v, std::span<const uint8_t> file_data,
                       std::string_view filename, const StagedThumb* precomputed)
{
    using enum VaultResult;

    if (!v.unlocked_) {
        return {Locked, {}};
    }
    if (!is_safe_node_name(filename)) {
        return {InvalidArg, {}};
    }

    ChunkStore store(v.fp_, v.master_key_.as_span(), framed_chunks(v.header_));

    // Append the main image data chunk, holding the write mutex for the entire chunk.
    ChunkSpan data_span;
    {
        std::lock_guard lk(*v.write_mutex_);
        if (!store.append_chunk(file_data, data_span)) {
            return {IoError, {}};
        }
        // Phase 50: flush buffered writes to fp_ so they become readable via read_fp_.
        // The synchronous add_image path additionally calls ChunkStore::sync() before
        // commit, but staged chunks must be immediately visible to concurrent reads
        // once the main thread attaches the node (next phase task).
        std::fflush(v.fp_);
    }

    // Decode and generate thumbnail if not precomputed.
    ImageFormat format = ImageFormat::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    bool animated = false;
    ChunkSpan thumb_span;

    if (precomputed) {
        format = precomputed->format;
        width = precomputed->width;
        height = precomputed->height;
        animated = precomputed->animated;

        // Append precomputed thumbnail if non-empty.
        if (!precomputed->thumb_jpeg.empty()) {
            std::lock_guard lk(*v.write_mutex_);
            if (!store.append_chunk(precomputed->thumb_jpeg, thumb_span)) {
                return {IoError, {}};
            }
            std::fflush(v.fp_);
        }
    } else {
        // Decode inline, exactly like add_image does (pure CPU part extracted to helper)
        auto decoded_thumb = decode_and_thumbnail(file_data);
        format = decoded_thumb.format;
        width = decoded_thumb.width;
        height = decoded_thumb.height;
        animated = decoded_thumb.animated;

        // Append thumbnail if generated (holding lock for chunk write)
        if (!decoded_thumb.thumb_bytes.empty()) {
            std::lock_guard lk(*v.write_mutex_);
            if (!store.append_chunk(decoded_thumb.thumb_bytes, thumb_span)) {
                return {IoError, {}};
            }
            std::fflush(v.fp_);
        }
    }

    // Build the fully-populated but UNATTACHED IndexNode.
    IndexNode img = IndexNode::image(std::string(filename));
    img.meta.format = format;
    img.meta.width = width;
    img.meta.height = height;
    img.meta.orig_size = file_data.size();
    img.meta.created_ts = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    img.meta.data_offset = data_span.offset;
    img.meta.data_length = data_span.length;
    img.meta.thumb_offset = thumb_span.offset;
    img.meta.thumb_length = thumb_span.length;
    img.meta.animated = animated;

    StagedNode result;
    result.status = Ok;
    result.node = std::move(img);
    return result;
}

StagedNode stage_video(Vault& v, std::span<const uint8_t> file_data,
                       std::string_view filename, uint32_t chunk_size,
                       const StagedVideoInfo* precomputed)
{
    using enum VaultResult;

    if (!v.unlocked_) {
        return {Locked, {}};
    }
    if (!is_safe_node_name(filename)) {
        return {InvalidArg, {}};
    }
    if (chunk_size == 0) {
        return {InvalidArg, {}};
    }

    // Probe only when the caller supplied no metadata: a cross-vault transfer
    // (Phase 67) carries the source's stored metadata, and re-probing would
    // reject legacy videos whose codec is still Unknown.
    media::VideoProbeResult probe;
    if (!precomputed && !media::probe_video(file_data, probe)) {
        return {InvalidArg, {}};
    }

    ChunkStore store(v.fp_, v.master_key_.as_span(), framed_chunks(v.header_));

    // Append video data chunks, one per 1-MiB chunk, with a lock_guard per chunk.
    std::vector<VideoChunk> chunks;
    for (size_t off = 0; off < file_data.size(); off += chunk_size) {
        const size_t len = std::min<size_t>(chunk_size, file_data.size() - off);
        ChunkSpan span;
        {
            std::lock_guard lk(*v.write_mutex_);
            if (!store.append_chunk(file_data.subspan(off, len), span)) {
                return {IoError, {}};
            }
            std::fflush(v.fp_);
        }
        chunks.push_back({span.offset, span.length});
    }

    // An empty file would store zero chunks; treat as invalid (no video stream).
    if (chunks.empty()) {
        return {InvalidArg, {}};
    }

    // Store the poster if it was generated.
    uint64_t poster_offset = 0;
    uint64_t poster_length = 0;
    const std::span<const uint8_t> poster =
        precomputed ? std::span<const uint8_t>(precomputed->poster_jpeg)
                    : std::span<const uint8_t>(probe.poster_jpeg);
    if (!poster.empty()) {
        ChunkSpan poster_span;
        {
            std::lock_guard lk(*v.write_mutex_);
            if (!store.append_chunk(poster, poster_span)) {
                return {IoError, {}};
            }
            std::fflush(v.fp_);
        }
        poster_offset = poster_span.offset;
        poster_length = poster_span.length;
    }

    // Build the fully-populated but UNATTACHED IndexNode.
    IndexNode vid = IndexNode::video(std::string(filename));
    vid.vmeta.container   = precomputed ? precomputed->container   : probe.container;
    vid.vmeta.codec       = precomputed ? precomputed->codec       : probe.codec;
    vid.vmeta.width       = precomputed ? precomputed->width       : probe.width;
    vid.vmeta.height      = precomputed ? precomputed->height      : probe.height;
    vid.vmeta.duration_us = precomputed ? precomputed->duration_us : probe.duration_us;
    vid.vmeta.orig_size = file_data.size();
    vid.vmeta.created_ts = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    vid.vmeta.chunk_size = chunk_size;
    vid.vmeta.chunks = std::move(chunks);
    vid.vmeta.poster_offset = poster_offset;
    vid.vmeta.poster_length = poster_length;

    StagedNode result;
    result.status = Ok;
    result.node = std::move(vid);
    return result;
}

VaultResult attach_staged(Vault& v, std::string_view gallery_path, IndexNode&& node)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;

    IndexNode* g = v.find_gallery(gallery_path);
    if (!g) return NotFound;

    if (vault_ops::child_named(g, node.name)) return AlreadyExists;

    if (!vault_ops::push_child(g->children, std::move(node))) {
        platform::log_error("Vault", "attach_staged: allocation failure");
        return IoError;
    }

    return Ok;
}

VaultResult ensure_gallery_path(Vault& v, std::string_view gallery_path)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;

    const auto segments = vault_ops::split_path(gallery_path);
    if (segments.empty()) return Ok;  // root already exists, idempotent

    // Validate each segment is a safe node name.
    for (std::string_view seg : segments) {
        if (!is_safe_node_name(seg)) return InvalidArg;
    }

    IndexNode* cur = &v.root_;
    for (std::string_view seg : segments) {
        IndexNode* child = vault_ops::child_named(cur, seg);
        if (child) {
            if (!child->is_gallery()) return InvalidArg;  // name is an image
            cur = child;
        } else {
            cur->children.push_back(IndexNode::gallery(std::string(seg)));
            cur = &cur->children.back();
        }
    }

    return Ok;  // idempotent: return Ok even if nothing was created
}

VaultResult add_image_prestaged(Vault& v, std::string_view gallery_path,
                                std::span<const uint8_t> file_data,
                                std::string_view filename,
                                const StagedThumb& thumb, uint64_t created_ts)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;
    if (!is_safe_node_name(filename)) return InvalidArg;

    IndexNode* g = v.find_gallery(gallery_path);
    if (!g) return NotFound;
    if (vault_ops::child_named(g, filename)) return AlreadyExists;

    StagedNode staged = stage_image(v, file_data, filename, &thumb);
    if (staged.status != Ok) return staged.status;
    if (created_ts != 0) staged.node.meta.created_ts = created_ts;

    if (const VaultResult r = attach_staged(v, gallery_path, std::move(staged.node)); r != Ok)
        return r;
    if (ChunkStore store(v.fp_, v.master_key_.as_span(), framed_chunks(v.header_));
        !store.sync())
        return IoError;
    return v.commit_index();
}

VaultResult add_video_prestaged(Vault& v, std::string_view gallery_path,
                                std::span<const uint8_t> file_data,
                                std::string_view filename,
                                const StagedVideoInfo& info, uint64_t created_ts)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;
    if (!is_safe_node_name(filename)) return InvalidArg;

    IndexNode* g = v.find_gallery(gallery_path);
    if (!g) return NotFound;
    if (vault_ops::child_named(g, filename)) return AlreadyExists;

    StagedNode staged = stage_video(v, file_data, filename, VIDEO_CHUNK_SIZE, &info);
    if (staged.status != Ok) return staged.status;
    if (created_ts != 0) staged.node.vmeta.created_ts = created_ts;

    if (const VaultResult r = attach_staged(v, gallery_path, std::move(staged.node)); r != Ok)
        return r;
    if (ChunkStore store(v.fp_, v.master_key_.as_span(), framed_chunks(v.header_));
        !store.sync())
        return IoError;
    return v.commit_index();
}

}  // namespace vault
