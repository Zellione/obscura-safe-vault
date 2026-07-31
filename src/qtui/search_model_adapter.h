#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <cstdint>

#include "vault/vault.h"

// QML-friendly wrapper for search results
struct SearchResultItem {
    Q_GADGET
public:
    Q_PROPERTY(QString path MEMBER path)
    Q_PROPERTY(bool is_gallery MEMBER is_gallery)
    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(quintptr nodeKey MEMBER nodeKey)

    QString path;
    bool is_gallery = false;
    QString name;
    quintptr nodeKey = 0;
};

Q_DECLARE_METATYPE(SearchResultItem)

class SearchModelAdapter : public QObject {
    Q_OBJECT

public:
    explicit SearchModelAdapter(QObject* parent = nullptr);

    // Set the vault pointer (called during app context setup)
    void setVault(vault::Vault* v) noexcept { vault_ = v; }

    // Search and filter results by query and scope.
    // Returns a ranked list of search results.
    Q_INVOKABLE QList<SearchResultItem> search(const QString& query, int scope);

signals:
    // Emitted when vault contents change
    void vaultChanged();

private:
    vault::Vault* vault_ = nullptr;

    bool isVaultReady() const;
};
