#include "adv_search_controller.h"
#include <vault/vault.h>
#include <vault/vault_search.h>
#include <ui/advanced_search_model.h>

AdvancedSearchController::AdvancedSearchController(QObject* parent)
    : QObject(parent)
{
}

void AdvancedSearchController::search(const QStringList& includeTags, const QStringList& excludeTags,
                                     const QString& nameQuery, int scope)
{
    if (!vault_ || !vault_->is_unlocked()) {
        results_.clear();
        emit resultsChanged();
        return;
    }

    // Build the query
    ui::AdvancedQuery query;
    query.scope = static_cast<ui::SearchScope>(scope);
    query.name_query = nameQuery.toStdString();

    for (const auto& tag : includeTags) {
        query.include.push_back({tag.toStdString(), 1});
    }

    for (const auto& tag : excludeTags) {
        query.exclude.push_back(tag.toStdString());
    }

    // Execute search via VaultSearch facade
    vault::VaultSearch searcher(*vault_);
    auto hits = searcher.run_search(query);

    // Convert to QML items
    results_.clear();
    for (const auto& hit : hits) {
        AdvancedSearchResultItem item;
        item.path = QString::fromStdString(hit.path);
        item.name = QString::fromStdString(hit.name);
        item.is_gallery = hit.is_gallery;
        item.nodeKey = reinterpret_cast<quintptr>(hit.node);
        results_.append(item);
    }

    emit resultsChanged();
}

void AdvancedSearchController::refreshSavedSearches()
{
    if (!vault_) {
        savedSearches_.clear();
        emit savedSearchesChanged();
        return;
    }

    // Access via VaultSearch facade
    vault::VaultSearch searcher(*vault_);
    auto searches = searcher.list_saved_searches();
    savedSearches_.clear();

    for (const auto& search : searches) {
        SavedSearchItem item;
        item.name = QString::fromStdString(search.name);
        savedSearches_.append(item);
    }

    emit savedSearchesChanged();
}

QString AdvancedSearchController::saveSearch(const QString& name, const QStringList& includeTags,
                                            const QStringList& excludeTags, const QString& nameQuery,
                                            int scope)
{
    if (!vault_ || !vault_->is_unlocked()) {
        return "Vault not unlocked";
    }

    // Build query
    ui::AdvancedQuery query;
    query.scope = static_cast<ui::SearchScope>(scope);
    query.name_query = nameQuery.toStdString();

    for (const auto& tag : includeTags) {
        query.include.push_back({tag.toStdString(), 1});
    }

    for (const auto& tag : excludeTags) {
        query.exclude.push_back(tag.toStdString());
    }

    // Save via VaultSearch facade
    vault::VaultSearch searcher(*vault_);
    auto result = searcher.save_search(name.toStdString(), query);
    if (result != vault::VaultResult::Ok) {
        return "Failed to save search";
    }

    // Refresh list
    refreshSavedSearches();
    return "";
}

QString AdvancedSearchController::deleteSavedSearch(const QString& name)
{
    if (!vault_ || !vault_->is_unlocked()) {
        return "Vault not unlocked";
    }

    vault::VaultSearch searcher(*vault_);
    auto result = searcher.delete_saved_search(name.toStdString());
    if (result != vault::VaultResult::Ok) {
        return "Failed to delete search";
    }

    // Refresh list
    refreshSavedSearches();
    return "";
}

void AdvancedSearchController::refreshTagVocabulary()
{
    if (!vault_ || !vault_->is_unlocked()) {
        tagVocab_.clear();
        emit tagVocabularyChanged();
        return;
    }

    vault::VaultSearch searcher(*vault_);
    auto allTags = searcher.all_tags();
    tagVocab_.clear();

    for (const auto& tag : allTags) {
        tagVocab_.append(QString::fromStdString(tag));
    }

    tagVocab_.sort();
    emit tagVocabularyChanged();
}
