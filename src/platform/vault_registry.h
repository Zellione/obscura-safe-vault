#pragma once

#include <filesystem>
#include <vector>

namespace platform {

// A config-dir list of known vault file PATHS. Stores NO secrets — never
// passwords, keys, or keyfile bytes. Most-recent-first ordering; add() moves an
// entry to the front and de-duplicates. Persisted as newline-delimited UTF-8
// paths, written atomically (temp file + rename).
class VaultRegistry {
public:
    VaultRegistry() = default;                              // empty: no backing file
    explicit VaultRegistry(std::filesystem::path file);

    [[nodiscard]] static VaultRegistry default_location();  // config_dir()/"vaults.list"

    [[nodiscard]] std::vector<std::filesystem::path> list() const;
    bool add(const std::filesystem::path& vault);           // move-to-front, dedup, persist
    bool remove(const std::filesystem::path& vault);        // persist
    void seed_if_empty(const std::filesystem::path& candidate);

    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

private:
    [[nodiscard]] bool write(const std::vector<std::filesystem::path>& entries) const;

    std::filesystem::path file_;
};

} // namespace platform
