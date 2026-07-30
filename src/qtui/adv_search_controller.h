#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

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

signals:
    void resultsChanged();
    void savedSearchesChanged();
    void tagVocabularyChanged();

private:
    vault::Vault* vault_ = nullptr;
    QList<AdvancedSearchResultItem> results_;
    QList<SavedSearchItem> savedSearches_;
    QStringList tagVocab_;
};
