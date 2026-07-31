#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <ui/debounce.h>

namespace vault { class Vault; }
namespace ui { struct AdvancedQuery; }

// QML-friendly result item
struct AdvancedSearchResultItem {
    Q_GADGET
public:
    QString path;
    QString name;
    bool is_gallery = false;
    quintptr nodeKey = 0;
};
Q_DECLARE_METATYPE(AdvancedSearchResultItem)

// Saved search item
struct SavedSearchItem {
    Q_GADGET
public:
    QString name;
};
Q_DECLARE_METATYPE(SavedSearchItem)

// Controller for advanced search screen (Shift+/)
class AdvancedSearchController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<AdvancedSearchResultItem> results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QList<SavedSearchItem> savedSearches READ savedSearches NOTIFY savedSearchesChanged)
    Q_PROPERTY(QStringList tagVocabulary READ tagVocabulary NOTIFY tagVocabularyChanged)

public:
    explicit AdvancedSearchController(QObject* parent = nullptr);

    void setVault(vault::Vault* v) noexcept { vault_ = v; }

    // Called when vault changes (lock/unlock/switch) to clear stale results
    Q_INVOKABLE void onVaultChanged();

    // Called when results are explicitly cleared
    Q_INVOKABLE void clearResults() { results_.clear(); emit resultsChanged(); }

    QList<AdvancedSearchResultItem> results() const { return results_; }
    QList<SavedSearchItem> savedSearches() const { return savedSearches_; }
    QStringList tagVocabulary() const { return tagVocab_; }

    Q_INVOKABLE void search(const QStringList& includeTags, const QStringList& excludeTags,
                           const QString& nameQuery, int scope);

    Q_INVOKABLE void refreshSavedSearches();

    Q_INVOKABLE QString saveSearch(const QString& name, const QStringList& includeTags,
                                   const QStringList& excludeTags, const QString& nameQuery, int scope);

    Q_INVOKABLE QString deleteSavedSearch(const QString& name);

    Q_INVOKABLE void refreshTagVocabulary();

    // Debounce control (for testing with fake clock)
    void arm() noexcept { debounce_.arm(); pendingInclude_ = currentInclude_; pendingExclude_ = currentExclude_; pendingName_ = currentName_; pendingScope_ = currentScope_; }
    bool isArmed() const noexcept { return debounce_.armed(); }
    void updateDebounce(double dt) noexcept {
        if (debounce_.fire(dt)) {
            performDebouncedSearch();
        }
    }

signals:
    void resultsChanged();
    void savedSearchesChanged();
    void tagVocabularyChanged();

private:
    void performDebouncedSearch();

    vault::Vault* vault_ = nullptr;
    QList<AdvancedSearchResultItem> results_;
    QList<SavedSearchItem> savedSearches_;
    QStringList tagVocab_;
    ui::Debounce debounce_{0.15};  // 150ms debounce

    // Pending search parameters (for debouncing)
    QStringList currentInclude_, pendingInclude_;
    QStringList currentExclude_, pendingExclude_;
    QString currentName_, pendingName_;
    int currentScope_ = 2, pendingScope_ = 2;
};
