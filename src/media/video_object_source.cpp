#include "media/video_object_source.h"

#include "platform/error_log.h"
#include "platform/safe_print.h"
#include "vault/v3_fs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <format>
#include <limits>
#include <new>
#include <ranges>
#include <string>
#include <utility>

namespace media {

VideoObjectSource::VideoObjectSource(
    std::unique_ptr<vault::v3::ObjectReader> reader) noexcept
    : reader_(std::move(reader))
{}

VideoObjectSource::OpenResult
VideoObjectSource::open(const vault::v3::VaultRoot& root,
                        std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                        const vault::v3::ObjectInfo& expected) noexcept
{
    using enum vault::v3::ObjectStatus;
    OpenResult result;
    if (expected.role != vault::v3::ObjectRole::OriginalVideo) {
        result.status = InvalidArgument;
        return result;
    }
    auto opened = vault::v3::ObjectReader::open(root, master_key, expected);
    result.status = opened.status;
    if (opened.status == Ok) {
        result.source = std::unique_ptr<VideoObjectSource>(
            new (std::nothrow) VideoObjectSource(std::move(opened.reader)));
        if (!result.source) result.status = OutOfMemory;
    }
    return result;
}

uint64_t VideoObjectSource::size() const noexcept
{
    return reader_->info().plaintext_length;
}

void VideoObjectSource::clear_cache() noexcept
{
    for (auto& entry : cache_) {
        (void)entry.bytes.resize(0);
        entry.valid = false;
    }
}

VideoObjectSource::CacheEntry* VideoObjectSource::load_frame(uint32_t index) noexcept
{
    for (auto& entry : cache_) {
        if (entry.valid && entry.index == index) {
            entry.stamp = ++stamp_;
            return &entry;
        }
    }
    auto* target = &cache_[0];
    if (cache_[0].valid && (!cache_[1].valid || cache_[1].stamp < cache_[0].stamp))
        target = &cache_[1];
    (void)target->bytes.resize(0);
    target->valid = false;
    last_status_ = reader_->read_frame(index, target->bytes);
    if (last_status_ != vault::v3::ObjectStatus::Ok) {
        last_error_frame_ = index;
        if (!diagnostic_reported_) {
            try {
                platform::log_error(
                    "VideoObjectSource",
                    std::format("object {}, frame {}: authentication/read failure (status {})",
                                vault::v3::object_relative_path(reader_->info().object_id), index,
                                std::to_underlying(last_status_)));
            } catch (const std::exception& error) {
                platform::safe_println(stderr,
                                       "[VideoObjectSource] could not format diagnostic: {}",
                                       error.what());
            }
            diagnostic_reported_ = true;
        }
        return nullptr;
    }
    target->index = index;
    target->stamp = ++stamp_;
    target->valid = true;
    return target;
}

int64_t VideoObjectSource::read(uint64_t offset, std::span<uint8_t> out) noexcept
{
    std::scoped_lock lock(mutex_);
    if (cancelled_.load()) {
        clear_cache();
        last_status_ = vault::v3::ObjectStatus::IoError;
        return -1;
    }
    const uint64_t total = size();
    if (offset >= total || out.empty()) return 0;
    const uint64_t wanted = std::min<uint64_t>(out.size(), total - offset);
    uint64_t copied = 0;
    const uint32_t frame_limit = reader_->info().frame_plain_limit;
    while (copied < wanted) {
        if (cancelled_.load()) {
            clear_cache();
            last_status_ = vault::v3::ObjectStatus::IoError;
            return -1;
        }
        const uint64_t position = offset + copied;
        const uint64_t frame64 = position / frame_limit;
        if (frame64 > std::numeric_limits<uint32_t>::max()) return -1;
        auto* frame = load_frame(static_cast<uint32_t>(frame64));
        if (!frame) return -1;
        const auto within = static_cast<size_t>(position % frame_limit);
        if (within >= frame->bytes.size()) return -1;
        const auto take = static_cast<size_t>(std::min<uint64_t>(
            wanted - copied, frame->bytes.size() - within));
        std::memcpy(out.data() + static_cast<size_t>(copied), frame->bytes.data() + within, take);
        copied += take;
    }
    last_status_ = vault::v3::ObjectStatus::Ok;
    return static_cast<int64_t>(copied);
}

void VideoObjectSource::cancel() noexcept
{
    cancelled_.store(true);
    std::scoped_lock lock(mutex_);
    clear_cache();
}

bool VideoObjectSource::cancelled() const noexcept
{
    return cancelled_.load();
}

size_t VideoObjectSource::cached_frame_count() const noexcept
{
    std::scoped_lock lock(mutex_);
    return static_cast<size_t>(std::ranges::count_if(cache_, [](const CacheEntry& entry) {
        return entry.valid;
    }));
}

vault::v3::ObjectStatus VideoObjectSource::last_status() const noexcept
{
    std::scoped_lock lock(mutex_);
    return last_status_;
}

uint32_t VideoObjectSource::last_error_frame() const noexcept
{
    std::scoped_lock lock(mutex_);
    return last_error_frame_;
}

}  // namespace media
