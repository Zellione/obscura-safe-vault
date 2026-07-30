#pragma once

#include <QObject>
#include <QStringList>
#include <vector>
#include <string>

namespace vault { class Vault; }
namespace ui { struct TagTallyEntry; }

class TagController : public QObject {
    Q_OBJECT

public:
    explicit TagController(QObject* parent = nullptr);

    // Set the vault pointer (called during app context setup)
    void setVault(vault::Vault* v) noexcept { vault_ = v; }

    // Add a tag to the node at nodePath. Returns true on success.
    // Respects INDEX_MAX_TAGS; returns false if adding would exceed the limit.
    Q_INVOKABLE bool addTag(const QString& nodePath, const QString& tag);

    // Remove a tag from the node at nodePath. Returns true on success.
    Q_INVOKABLE bool removeTag(const QString& nodePath, const QString& tag);

    // Get the node's own tags (not inherited, not from contents)
    Q_INVOKABLE QStringList getOwnTags(const QString& nodePath) const;

    // Get tags inherited from ancestor galleries
    Q_INVOKABLE QStringList getInheritedTags(const QString& nodePath) const;

    // Get tags from the gallery's descendant content (galleries only)
    Q_INVOKABLE QStringList getContentsTags(const QString& nodePath) const;

    // Get autosuggest rankings for a prefix, excluding own tags
    Q_INVOKABLE QStringList getSuggestions(const QString& prefix,
                                           const QString& nodePath) const;

    // Get the vocabulary of all tags in the vault
    Q_INVOKABLE QStringList getVocabulary() const;

    // Get display text for a tag (after category prefix stripping)
    Q_INVOKABLE QString getTagDisplayText(const QString& tag) const;

    // Get the swatch index for a tag's color (-1 if uncategorized/text dim)
    Q_INVOKABLE int getTagSwatchIndex(const QString& tag) const;

signals:
    // Emitted when tags change (externally or via this controller)
    void tagsChanged(const QString& nodePath);

private:
    vault::Vault* vault_ = nullptr;

    // Helper: check if vault is unlocked and ready
    bool isVaultReady() const;
};
