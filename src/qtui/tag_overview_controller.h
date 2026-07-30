#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <vector>
#include <string>

namespace vault { class Vault; }

// QML-friendly gadget for a single tag row in the overview
struct TagOverviewItem {
    Q_GADGET
public:
    QString tag;
    int galleryCount = 0;
    int imageCount = 0;
    QString description;

    TagOverviewItem() = default;
    TagOverviewItem(const QString& t, int g, int i, const QString& d)
        : tag(t), galleryCount(g), imageCount(i), description(d) {}

    int totalCount() const { return galleryCount + imageCount; }
};
Q_DECLARE_METATYPE(TagOverviewItem)

// Controller for the tag overview screen (Shift+T)
// Presents all tags in the vault with their direct-tag counts + descriptions
class TagOverviewController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<TagOverviewItem> tags READ tags NOTIFY tagsChanged)

public:
    explicit TagOverviewController(QObject* parent = nullptr);

    void setVault(vault::Vault* v) noexcept { vault_ = v; }

    // Get the current list of tags
    QList<TagOverviewItem> tags() const { return tags_; }

    // Refresh the tag list from the vault
    Q_INVOKABLE void refresh();

    // Sort the tags by key (0 = Name, 1 = Count)
    Q_INVOKABLE void sortBy(int sortKey);

    // Filter tags by prefix (case-insensitive)
    Q_INVOKABLE void filterByPrefix(const QString& prefix);

    // Set a tag's description (empty string removes it)
    Q_INVOKABLE bool setTagDescription(const QString& tag, const QString& description);

    // Import a JSON tag dictionary from bytes
    // Returns empty string on success, error message on failure
    Q_INVOKABLE QString importTagDictJson(const QByteArray& jsonBytes);

    // Get summary lines for the last import (empty if no import happened)
    Q_INVOKABLE QStringList importSummaryLines() const;

    // Clear the import summary
    Q_INVOKABLE void clearImportSummary();

signals:
    void tagsChanged();
    void importFinished();

private:
    vault::Vault* vault_ = nullptr;
    QList<TagOverviewItem> tags_;
    QList<TagOverviewItem> allTags_;  // Unfiltered list
    std::vector<std::string> lastImportSummary_;

    bool isVaultReady() const;
};
