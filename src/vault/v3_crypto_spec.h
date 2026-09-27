#pragma once

#include "crypto/crypto_sizes.h"
#include "crypto/secure_mem.h"

#include <array>
#include <cstdint>
#include <span>

namespace vault::v3 {

inline constexpr uint16_t FORMAT_VERSION = 3;
inline constexpr uint16_t OBJECT_FORMAT_VERSION = 1;
inline constexpr uint8_t OBJECT_AD_VERSION = 1;

using Id = std::array<uint8_t, crypto::NODE_ID_SIZE>;

enum class ObjectRole : uint8_t {
    OriginalImage = 1,
    Thumbnail = 2,
    Poster = 3,
    OriginalVideo = 4,
};

inline constexpr size_t OBJECT_FRAME_AD_SIZE = 66;

struct ObjectFrameTag {
    ObjectRole role = ObjectRole::OriginalImage;
    Id vault_id{};
    Id object_id{};
    Id owner_node_id{};
    uint32_t frame_index = 0;
    uint32_t frame_count = 0;
    uint64_t total_plaintext_length = 0;
};

[[nodiscard]] crypto::SecureBuffer<crypto::KEY_SIZE>
derive_database_key(std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                    std::span<const uint8_t, crypto::NODE_ID_SIZE> vault_id) noexcept;

[[nodiscard]] crypto::SecureBuffer<crypto::KEY_SIZE>
derive_object_key(std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                  std::span<const uint8_t, crypto::NODE_ID_SIZE> vault_id,
                  std::span<const uint8_t, crypto::NODE_ID_SIZE> object_id) noexcept;

[[nodiscard]] std::array<uint8_t, OBJECT_FRAME_AD_SIZE>
build_object_frame_ad(const ObjectFrameTag& tag) noexcept;

} // namespace vault::v3

