#include "test_framework.h"

#include "vault/v3_detect.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

fs::path make_temp_dir()
{
    std::array<char, 64> pattern{};
    std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-v3-detect-XXXXXX");
    const char* made = ::mkdtemp(pattern.data());
    return made == nullptr ? fs::path{} : fs::path{made};
}

void write_prefix(const fs::path& path, std::array<uint8_t, 8> magic, uint16_t version)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
    const std::array<uint8_t, 4> tail{
        static_cast<uint8_t>(version & 0xffU), static_cast<uint8_t>(version >> 8U), 0, 1,
    };
    out.write(reinterpret_cast<const char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
}

} // namespace

TEST(v3_detector_classifies_legacy_file_and_directory)
{
    const auto tmp = make_temp_dir();
    REQUIRE(!tmp.empty());
    const auto legacy = tmp / "old.osv";
    write_prefix(legacy, {'O', 'S', 'V', 'A', 'U', 'L', 'T', 0}, 1);
    CHECK_EQ(vault::v3::classify_candidate(legacy), vault::v3::CandidateKind::LegacyFile);

    const auto modern = tmp / "new.osv";
    fs::create_directory(modern);
    write_prefix(modern / "vault.header", {'O', 'S', 'V', '3', 'D', 'I', 'R', 0}, 3);
    CHECK_EQ(vault::v3::classify_candidate(modern), vault::v3::CandidateKind::DirectoryV3);
    fs::remove_all(tmp);
}

TEST(v3_detector_fails_closed_on_truncation_future_versions_and_wrong_shapes)
{
    const auto tmp = make_temp_dir();
    REQUIRE(!tmp.empty());
    const auto truncated = tmp / "truncated.osv";
    std::ofstream(truncated, std::ios::binary).write("OSV3", 4);
    CHECK_EQ(vault::v3::classify_candidate(truncated), vault::v3::CandidateKind::NotVault);

    const auto future = tmp / "future.osv";
    fs::create_directory(future);
    write_prefix(future / "vault.header", {'O', 'S', 'V', '3', 'D', 'I', 'R', 0}, 4);
    CHECK_EQ(vault::v3::classify_candidate(future), vault::v3::CandidateKind::UnsupportedVersion);

    const auto wrong = tmp / "wrong.osv";
    fs::create_directory(wrong);
    fs::create_directory(wrong / "vault.header");
    CHECK_EQ(vault::v3::classify_candidate(wrong), vault::v3::CandidateKind::UnsafePath);
    CHECK_EQ(vault::v3::classify_candidate(tmp / "missing"), vault::v3::CandidateKind::NotVault);
    fs::remove_all(tmp);
}

TEST(v3_detector_rejects_root_and_header_symlinks)
{
    const auto tmp = make_temp_dir();
    REQUIRE(!tmp.empty());
    const auto real = tmp / "real.osv";
    fs::create_directory(real);
    write_prefix(real / "vault.header", {'O', 'S', 'V', '3', 'D', 'I', 'R', 0}, 3);
    fs::create_directory_symlink(real, tmp / "root-link.osv");
    CHECK_EQ(vault::v3::classify_candidate(tmp / "root-link.osv"),
             vault::v3::CandidateKind::UnsafePath);

    const auto linked_header = tmp / "linked-header.osv";
    fs::create_directory(linked_header);
    fs::create_symlink(real / "vault.header", linked_header / "vault.header");
    CHECK_EQ(vault::v3::classify_candidate(linked_header),
             vault::v3::CandidateKind::UnsafePath);
    fs::remove_all(tmp);
}
