#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace platform {

// Longest path we will hand to fopen(). Generous next to PATH_MAX (4096 on
// Linux) and Windows' extended-length limit (32767), but a bound nonetheless.
inline constexpr size_t MAX_USER_PATH_BYTES = 4096;

// Normalize a path that arrived from OUTSIDE the program — a native file dialog
// (SDL hands back raw C strings) or a line read back out of vaults.list. Every
// such path reaches std::fopen, so it is normalized here, at the boundary,
// before it is used: the "transform, normalize, validate, then use" order that
// path-injection defences require (CWE-22 / Sonar cpp:S2083).
//
// Returns nullopt for a path that is empty, holds an embedded NUL (which would
// silently truncate the C string fopen() receives), or exceeds
// MAX_USER_PATH_BYTES. Otherwise returns it lexically_normal()'d, so ".." and
// "." components are collapsed before anything opens it.
//
// NOTE this deliberately does NOT confine the result to a base directory. There
// is none to confine it to: the user picks where their .osv vault lives, and
// external drives and removable media are entirely normal. The user and the
// "attacker" are the same principal here — there is no privilege boundary to
// cross. The traversal that IS reachable in this app (a hostile vault's node
// name escaping the export folder) is stopped in vault::safe_name and
// ui::export_path_within, not here.
[[nodiscard]] std::optional<std::filesystem::path> normalize_user_path(std::string_view raw);

// Per-user data directory (created if needed). Empty path on failure.
[[nodiscard]] std::filesystem::path config_dir();

// config_dir() / "vault.osv"  (just the filename if config_dir() is empty).
[[nodiscard]] std::filesystem::path default_vault_path();

// Read an entire file into a byte vector. nullopt if it cannot be opened/read.
[[nodiscard]] std::optional<std::vector<uint8_t>>
read_file(const std::filesystem::path& path);

// Bytes in a generated keyfile (512 bits — far beyond brute force).
inline constexpr size_t KEYFILE_SIZE = 64;

// Create a fresh CSPRNG keyfile at `path`. Refuses to overwrite an existing
// file: clobbering a keyfile with new random bytes would permanently lock
// every vault bound to the old one. Returns false on RNG or I/O failure.
[[nodiscard]] bool write_new_keyfile(const std::filesystem::path& path);

} // namespace platform
