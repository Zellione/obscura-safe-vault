#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace platform {

// Per-user data directory (created if needed). Empty path on failure.
[[nodiscard]] std::filesystem::path config_dir();

// config_dir() / "vault.osv"  (just the filename if config_dir() is empty).
[[nodiscard]] std::filesystem::path default_vault_path();

// Read an entire file into a byte vector. nullopt if it cannot be opened/read.
[[nodiscard]] std::optional<std::vector<uint8_t>>
read_file(const std::filesystem::path& path);

} // namespace platform
