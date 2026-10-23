#pragma once
#include <array>
#include <span>
#include <vector>
#include "crypto/secure_string.h"
#include "vault/op_progress.h"
#include "vault/vault.h"

namespace vault {
using RenameId = std::array<uint8_t, crypto::NODE_ID_SIZE>;
struct RenameOptions {
    crypto::SecureString pattern;
    crypto::SecureString replacement;
    bool preserve_extensions = true;
    bool recursive = false;
    bool case_sensitive = true;
    bool all_matches = false;
};
enum class RenameState { Changed, Unchanged, Unmatched, Invalid, Collision };
struct RenameRow {
    RenameId id{};
    RenameId parent{};
    crypto::SecureString before;
    crypto::SecureString after;
    RenameState state = RenameState::Unmatched;
};
struct RenamePreview {
    std::vector<RenameRow> rows;
    crypto::SecureString error;
    [[nodiscard]] bool valid() const;
};
// Caller owns the vault tree exclusively, including during preview. IDs are
// resolved once in a linear walk; no path survives a parent rename.
[[nodiscard]] RenamePreview preview_rename(Vault& v, std::span<const RenameId> selected,
                                           const RenameOptions& options,
                                           OpProgress* progress = nullptr);
[[nodiscard]] VaultResult apply_rename(Vault& v, const RenamePreview& preview,
                                       OpProgress* progress = nullptr);
}  // namespace vault
