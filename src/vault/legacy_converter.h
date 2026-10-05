#pragma once

#include "crypto/kdf.h"
#include "vault/op_progress.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>

namespace vault {

class Vault;
struct IndexNode;
namespace v3 {
class ReadSession;
}

enum class LegacyConversionStatus : uint8_t {
    Ok,
    SourceLocked,
    SourceNotLegacy,
    InvalidDestination,
    DestinationExists,
    ResumeMismatch,
    AuthenticationFailed,
    SourceCorrupt,
    IoError,
    Cancelled,
    VerificationFailed,
    InsufficientSpace,
};

struct LegacyConversionRequest {
    std::filesystem::path destination;
    std::span<const uint8_t> password;
    std::span<const uint8_t> keyfile;
    crypto::KdfParams kdf;
    const std::atomic_bool* cancel = nullptr;
    OpProgress* progress = nullptr;
};

struct LegacyConversionReport {
    LegacyConversionStatus status = LegacyConversionStatus::IoError;
    uint64_t galleries = 0;
    uint64_t images = 0;
    uint64_t videos = 0;
    uint64_t original_bytes = 0;
    uint64_t derived_bytes = 0;
    uint64_t required_free_bytes = 0;
    bool deep_verified = false;
    bool cold_reopened = false;
    bool resumed = false;
};

// Copy an already-unlocked legacy vault into a fresh v3 directory. The source
// is read only and is never modified. Every destination identity, key and nonce
// is minted afresh; success requires deep verification and an independent reopen.
[[nodiscard]] LegacyConversionReport
convert_legacy_vault(Vault& source, const LegacyConversionRequest& request) noexcept;

// Internal bounded bridge used by the converter. It decrypts at most one
// legacy video chunk at a time into locked memory while the v3 writer pulls
// fixed-size object frames.
[[nodiscard]] LegacyConversionStatus
copy_legacy_video_object(Vault& source, const IndexNode& source_node, v3::ReadSession& destination,
                         const IndexNode& destination_node) noexcept;

}  // namespace vault
