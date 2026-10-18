#pragma once

#include "platform/path_utf8.h"
#include "vault/vault.h"

#include <array>
#include <filesystem>

namespace vaulttest {

// Deliberately cheap test credentials; production KDF parameters remain unchanged.
inline constexpr crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};
inline constexpr std::array<uint8_t, 2> kPassword{'p', 'w'};

// Callers REQUIRE success so failed setup cannot silently invalidate later assertions.
[[nodiscard]] inline bool make_legacy_writable_vault(vault::Vault& v,
                                                     const std::filesystem::path& path)
{
    return vault::Vault::create(platform::path_to_utf8(path), kPassword, {}, kTestKdf, v) ==
           vault::VaultResult::Ok;
}

[[nodiscard]] inline bool make_directory_vault(vault::Vault& v, const std::filesystem::path& path)
{
    return vault::Vault::create_directory(platform::path_to_utf8(path), kPassword, {}, kTestKdf,
                                          v) == vault::VaultResult::Ok;
}

// Generic cold reopen retains production behavior: directory writes, legacy reads.
[[nodiscard]] inline bool open_vault(const std::filesystem::path& path, vault::Vault& v)
{
    return vault::Vault::open(platform::path_to_utf8(path), v) == vault::VaultResult::Ok &&
           v.unlock(kPassword, {}) == vault::VaultResult::Ok;
}

[[nodiscard]] inline bool open_legacy_read_only_vault(const std::filesystem::path& path,
                                                      vault::Vault& v)
{
    return vault::Vault::open(platform::path_to_utf8(path), v) == vault::VaultResult::Ok &&
           vault::vault_is_read_only(v) && v.unlock(kPassword, {}) == vault::VaultResult::Ok;
}

}  // namespace vaulttest
