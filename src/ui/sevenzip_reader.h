#pragma once

#include "crypto/secure_mem.h"
#include "ui/archive_read_error.h"
#include "ui/zip_plan.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace ui {
struct SevenZipState;

// 7z/RAR decoder. Extraction is one forward pass with one outstanding entry,
// never an archive-sized plaintext cache. Source spans must outlive the reader.
class SevenZipReader {
public:
    SevenZipReader();
    ~SevenZipReader();
    SevenZipReader(const SevenZipReader&) = delete;
    SevenZipReader& operator=(const SevenZipReader&) = delete;

    [[nodiscard]] static bool handles(std::span<const uint8_t> bytes);
    [[nodiscard]] bool open(std::span<const uint8_t> bytes, std::string_view password);
    [[nodiscard]] bool open_files(std::span<const std::filesystem::path> paths,
                                  std::string_view password);
    [[nodiscard]] bool extract(size_t index, crypto::SecureBytes& out);
    [[nodiscard]] const std::vector<ZipEntry>& entries() const;
    [[nodiscard]] ArchiveReadError error() const;
    [[nodiscard]] bool encrypted() const;
    [[nodiscard]] size_t stream_opens() const;

private:
    std::unique_ptr<SevenZipState> state_;
};
}  // namespace ui
