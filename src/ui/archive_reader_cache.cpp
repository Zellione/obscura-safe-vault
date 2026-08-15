#include "ui/archive_reader_cache.h"

#ifdef OSV_VENDORED_ARCHIVE

namespace ui {

ArchiveReader* ArchiveReaderCache::get(std::span<const uint8_t> bytes,
                                       std::string_view         passphrase)
{
    const Key key{bytes.data(), bytes.size()};
    if (const auto it = readers_.find(key); it != readers_.end()) {
        return it->second.get();
    }
    auto reader = std::make_unique<ArchiveReader>();
    if (!reader->open(bytes, passphrase)) {
        return nullptr;
    }
    ++opens_;
    return readers_.try_emplace(key, std::move(reader)).first->second.get();
}

void ArchiveReaderCache::drop(std::span<const uint8_t> bytes) noexcept
{
    readers_.erase(Key{bytes.data(), bytes.size()});
}

} // namespace ui

#endif // OSV_VENDORED_ARCHIVE
