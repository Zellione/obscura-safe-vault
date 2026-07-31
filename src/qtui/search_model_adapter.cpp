#include "search_model_adapter.h"

#include "ui/search_model.h"
#include "vault/vault.h"
#include <algorithm>

SearchModelAdapter::SearchModelAdapter(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<SearchResultItem>();
}

bool SearchModelAdapter::isVaultReady() const
{
    return vault_ != nullptr && vault_->is_unlocked();
}

QList<SearchResultItem> SearchModelAdapter::search(const QString& query, int scope)
{
    QList<SearchResultItem> result;
    if (!isVaultReady()) {
        return result;
    }

    // scope parameter is already vault::SearchScope as an int
    vault::SearchScope vaultScope = static_cast<vault::SearchScope>(scope);

    // Get all results in the current scope from vault
    auto hits = vault_->search("", vaultScope);  // Empty string gets all items in scope

    // Tokenize the query
    auto tokens = ui::tokenize(query.toStdString());

    // Filter and score the results
    std::vector<std::pair<const vault::SearchHit*, int>> scored_results;
    for (const auto& hit : hits) {
        int score = ui::score(tokens, hit.name, hit.effective_tags);
        if (score > 0 || tokens.empty()) {  // Include unscored results if query is empty
            scored_results.push_back({&hit, score});
        }
    }

    // Sort by score (highest first)
    std::sort(scored_results.begin(), scored_results.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

    // Convert to QML-friendly results
    for (const auto& [hit, _] : scored_results) {
        SearchResultItem item;
        item.path = QString::fromStdString(hit->path);
        item.is_gallery = hit->is_gallery;
        item.name = QString::fromStdString(hit->name);
        if (hit->node) {
            item.nodeKey = static_cast<quintptr>(reinterpret_cast<uintptr_t>(hit->node));
        }
        result.append(item);
    }

    return result;
}
