#pragma once
#include <functional>
#include "vault/index.h"
#include "vault/op_progress.h"

namespace vault {
class Vault;
enum class VaultResult;
void initialize_tag_provenance(IndexNode& node);
[[nodiscard]] bool rebuild_bundle_tags(IndexNode& node, const VaultSettings& settings);
// Visit stored references in addition to effective node.tags. Category rename
// must include these references in its existing atomic replacement plan.
void visit_bundle_tag_references(IndexNode& root, VaultSettings& settings,
                                 const std::function<void(crypto::SecureString&)>& visit);
[[nodiscard]] VaultResult save_tag_bundle(Vault& v, std::string_view name,
                                          const std::vector<std::string>& tags,
                                          std::string_view original_name = {});
[[nodiscard]] VaultResult delete_tag_bundle(Vault& v, std::string_view name);
[[nodiscard]] VaultResult apply_tag_bundle(Vault& v, std::span<const std::string> paths,
                                           std::string_view name, bool remove,
                                           OpProgress* progress = nullptr);
// Prepare transfer metadata in the destination's existing batch transaction.
// Conflicting definitions get a unique imported name; effective ancestor tags
// materialized by the transfer are retained as explicit assignments.
[[nodiscard]] VaultResult copy_tag_provenance(const IndexNode& source,
                                              const VaultSettings& source_settings,
                                              IndexNode& destination, Vault& destination_vault);
}  // namespace vault
