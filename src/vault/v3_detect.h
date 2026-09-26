#pragma once

#include <filesystem>

namespace vault::v3 {

enum class CandidateKind {
    LegacyFile,
    DirectoryV3,
    NotVault,
    UnsupportedVersion,
    UnsafePath,
};

// Advisory format classification only. Phase 106 performs the authoritative
// descriptor-relative, race-safe open; callers must never use this result as a
// path authorization decision.
[[nodiscard]] CandidateKind classify_candidate(const std::filesystem::path& path) noexcept;

} // namespace vault::v3
