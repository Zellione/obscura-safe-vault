#pragma once

#include "crypto/secure_mem.h"
#include "vault/v3_crypto_spec.h"
#include "vault/v3_fs.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace vault::v3 {

inline constexpr uint32_t OBJECT_MAX_FRAME_PLAIN = 1024U * 1024U;
inline constexpr uint32_t OBJECT_MAX_FRAMES = 1024U * 1024U;
inline constexpr size_t OBJECT_PREAMBLE_SIZE = 64;

enum class ObjectStatus : uint8_t {
    Ok,
    InvalidArgument,
    IoError,
    AlreadyExists,
    Corrupt,
    AuthenticationFailed,
    OutOfMemory,
};

struct ObjectWriteRequest {
    Id vault_id{};
    Id owner_node_id{};
    ObjectRole role = ObjectRole::OriginalImage;
    uint8_t media_format = 0;
    uint32_t frame_plain_limit = OBJECT_MAX_FRAME_PLAIN;
};

struct ObjectInfo {
    ObjectId object_id{};
    Id vault_id{};
    Id owner_node_id{};
    ObjectRole role = ObjectRole::OriginalImage;
    uint8_t media_format = 0;
    uint64_t plaintext_length = 0;
    uint64_t encrypted_length = 0;
    uint32_t frame_plain_limit = 0;
    uint32_t frame_count = 0;
};

struct ObjectWriteResult {
    ObjectStatus status = ObjectStatus::IoError;
    ObjectInfo info{};
};

struct ObjectInspection {
    ObjectStatus status = ObjectStatus::IoError;
    ObjectInfo info{};
};

[[nodiscard]] bool valid_object_id(const ObjectId& id) noexcept;
[[nodiscard]] std::optional<ObjectId> generate_object_id() noexcept;
[[nodiscard]] std::array<uint8_t, OBJECT_PREAMBLE_SIZE>
build_object_preamble(const Id& vault_id, const ObjectId& object_id,
                      uint32_t frame_plain_limit) noexcept;

[[nodiscard]] ObjectWriteResult write_object(const VaultRoot& root,
                                             std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                             const ObjectWriteRequest& request,
                                             std::span<const uint8_t> plaintext) noexcept;

using ObjectReadFn = std::function<bool(uint64_t offset, std::span<uint8_t> destination)>;

// Pulls exactly `plaintext_length` bytes from `read`. Each callback destination is bounded by
// frame_plain_limit and is locked/wiped storage; false cancels without publishing a partial file.
[[nodiscard]] ObjectWriteResult
write_object_stream(const VaultRoot& root, std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                    const ObjectWriteRequest& request, uint64_t plaintext_length,
                    const ObjectReadFn& read) noexcept;

class ObjectReader {
public:
    struct Entry {
        uint64_t offset;
        uint64_t length;
        uint32_t plain_length;
        uint8_t compression;
    };
    struct OpenResult {
        ObjectStatus status = ObjectStatus::IoError;
        std::unique_ptr<ObjectReader> reader;
    };

    [[nodiscard]] static OpenResult open(const VaultRoot& root,
                                         std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                         const ObjectInfo& expected) noexcept;
    [[nodiscard]] ObjectStatus read_all(crypto::SecureBytes& out) const noexcept;
    [[nodiscard]] ObjectStatus read_range(uint64_t offset, size_t length,
                                          crypto::SecureBytes& out) const noexcept;
    [[nodiscard]] const ObjectInfo& info() const noexcept
    {
        return info_;
    }

    ObjectReader(ObjectFile file, crypto::SecureBuffer<crypto::KEY_SIZE> key,
                 const ObjectInfo& info, std::vector<Entry> entries) noexcept;

private:
    [[nodiscard]] ObjectStatus read_frame(uint32_t index, crypto::SecureBytes& out) const noexcept;
    ObjectFile file_;
    crypto::SecureBuffer<crypto::KEY_SIZE> key_;
    ObjectInfo info_;
    std::vector<Entry> entries_;
};

[[nodiscard]] ObjectInspection inspect_object(const VaultRoot& root,
                                              std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                              const ObjectInfo& expected) noexcept;

}  // namespace vault::v3
