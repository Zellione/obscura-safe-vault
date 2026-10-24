#pragma once
#include "vault/index.h"
#include "vault/op_progress.h"

namespace vault {
class Vault;
enum class VaultResult;
void initialize_tag_provenance(IndexNode& node);
[[nodiscard]] bool rebuild_bundle_tags(IndexNode& node, const VaultSettings& settings);
// Visit stored references in addition to effective node.tags. Category rename
// must include these references in its existing atomic replacement plan.
template <typename Visitor>
void visit_explicit_tag_references(IndexNode& node, const Visitor& visit)
{
    for (auto& tag : node.explicit_tags)
        visit(tag);
    for (auto& child : node.children)
        visit_explicit_tag_references(child, visit);
}

template <typename Visitor>
void visit_bundle_tag_references(IndexNode& root, VaultSettings& settings, const Visitor& visit)
{
    visit_explicit_tag_references(root, visit);
    for (auto& bundle : settings.tag_bundles)
        for (auto& tag : bundle.tags)
            visit(tag);
}
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
// Atomically merge gallery tag ownership and definitions before moving children.
// commit=false stages the merge in the transfer driver's existing batch transaction.
[[nodiscard]] VaultResult
merge_tag_provenance(const IndexNode& source, const VaultSettings& source_settings,
                     Vault& destination_vault, std::string_view destination_path,
                     std::span<const std::string> inherited = {}, bool commit = true);
}  // namespace vault
