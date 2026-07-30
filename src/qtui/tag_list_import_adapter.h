#pragma once

#include <QString>
#include <QStringList>
#include <vector>
#include <string>

namespace vault { class Vault; }

// Adapter for parsing and importing a tag list from a .txt file
// Uses ui::tag_list_parse to normalize tags, then adds each to a node
class TagListImportAdapter {
public:
    explicit TagListImportAdapter(vault::Vault* vault = nullptr);

    void setVault(vault::Vault* v) noexcept { vault_ = v; }

    // Parse a tag list from raw file bytes and return the normalized tags
    QStringList parseTagList(const QByteArray& fileBytes) const;

    // Add each tag in the list to a node (merge, not replace)
    // Returns number of tags successfully added, or -1 on error
    int importTagsToNode(const QString& nodePath, const QStringList& tags);

    // Combined parse + import from file bytes
    int importTagsFromBytes(const QString& nodePath, const QByteArray& fileBytes);

private:
    vault::Vault* vault_ = nullptr;

    bool isVaultReady() const;
};
