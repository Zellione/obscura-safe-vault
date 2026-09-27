#include "v3_detect.h"

#include "platform/path_utf8.h"
#include "v3_crypto_spec.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <system_error>

namespace vault::v3 {
namespace {

constexpr std::array<uint8_t, 8> LEGACY_MAGIC{'O', 'S', 'V', 'A', 'U', 'L', 'T', 0};
constexpr std::array<uint8_t, 8> V3_MAGIC{'O', 'S', 'V', '3', 'D', 'I', 'R', 0};

uint16_t read_u16_le(const uint8_t* p) noexcept
{
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8U);
}

CandidateKind classify_header(const std::filesystem::path& path,
                              const std::array<uint8_t, 8>& magic, uint16_t expected_version,
                              CandidateKind matched) noexcept
{
    using enum CandidateKind;

    std::FILE* fp = platform::fopen_path(path, "rb");
    if (fp == nullptr) return NotVault;
    std::array<uint8_t, 12> prefix{};
    const size_t got = std::fread(prefix.data(), 1, prefix.size(), fp);
    std::fclose(fp);
    if (got != prefix.size()) return NotVault;
    if (!std::equal(magic.begin(), magic.end(), prefix.begin())) return NotVault;
    return read_u16_le(prefix.data() + 8) == expected_version ? matched : UnsupportedVersion;
}

}  // namespace

CandidateKind classify_candidate(const std::filesystem::path& path) noexcept
{
    using enum CandidateKind;

    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || status.type() == std::filesystem::file_type::not_found) return NotVault;
    if (std::filesystem::is_symlink(status)) return UnsafePath;
    if (std::filesystem::is_regular_file(status))
        return classify_header(path, LEGACY_MAGIC, 1, LegacyFile);
    if (!std::filesystem::is_directory(status)) return UnsafePath;

    const auto header = path / "vault.header";
    const auto header_status = std::filesystem::symlink_status(header, ec);
    if (ec || header_status.type() == std::filesystem::file_type::not_found) return NotVault;
    if (std::filesystem::is_symlink(header_status) ||
        !std::filesystem::is_regular_file(header_status))
        return UnsafePath;
    return classify_header(header, V3_MAGIC, FORMAT_VERSION, DirectoryV3);
}

}  // namespace vault::v3
