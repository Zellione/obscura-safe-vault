#pragma once

// Advanced-search facade over a Vault (Phase 18).
//
// Groups the weighted/grouped query API and saved-search persistence into one
// cohesive object so the Vault class itself stays within its method budget
// (cpp:S1448). A VaultSearch is a thin, non-owning view: it borrows a `Vault&`
// and reaches into the vault's index + saved searches as a friend. Every call
// needs an unlocked vault; otherwise results are empty (or VaultResult::Locked).

#include <string>
#include <string_view>
#include <vector>

#include "vault/vault.h"   // Vault, VaultResult, SearchHit, SavedSearch

namespace ui { struct AdvancedQuery; }   // pure query model (src/ui/advanced_search_model.h)

namespace vault {

class VaultSearch {
public:
    explicit VaultSearch(Vault& vault) noexcept : v_(vault) {}

    // The vault's distinct tag vocabulary across the whole tree, de-duplicated
    // case-insensitively and sorted case-insensitively (feeds tag autocomplete).
    // Empty while locked.
    [[nodiscard]] std::vector<std::string> all_tags() const;

    // Evaluate a weighted include/exclude/grouped query against the tree (scope
    // taken from query.scope), ranked by descending relevance score then
    // ascending path. Empty while locked.
    [[nodiscard]] std::vector<SearchHit> run_search(const ui::AdvancedQuery& query) const;

    // Saved searches — vault-global, persisted in the encrypted index. save_search
    // upserts by exact name (InvalidArg for an empty name or when full);
    // delete_saved_search removes by name (NotFound if absent). Both persist via
    // the crash-safe index swap and need an unlocked vault (Locked otherwise).
    // list_saved_searches returns a copy (empty while locked).
    [[nodiscard]] VaultResult save_search(std::string_view name, const ui::AdvancedQuery& query);
    [[nodiscard]] VaultResult delete_saved_search(std::string_view name);
    [[nodiscard]] std::vector<SavedSearch> list_saved_searches() const;

private:
    Vault& v_;
};

} // namespace vault
