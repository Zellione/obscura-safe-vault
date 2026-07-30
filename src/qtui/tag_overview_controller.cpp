#include "tag_overview_controller.h"

#include <QDebug>
#include "vault/vault.h"
#include "vault/vault_search.h"
#include "vault/index.h"
#include "ui/tag_overview_model.h"
#include "ui/tag_json_parse.h"
#include "ui/tag_dict_import.h"

TagOverviewController::TagOverviewController(QObject* parent)
    : QObject(parent)
{
}

bool TagOverviewController::isVaultReady() const
{
    return vault_ && vault_->is_unlocked();
}

void TagOverviewController::refresh()
{
    allTags_.clear();
    tags_.clear();

    if (!isVaultReady()) {
        emit tagsChanged();
        return;
    }

    try {
        vault::VaultSearch search(*vault_);
        auto tagTallies = search.tag_overview();
        auto settings = vault::vault_settings(*vault_);

        for (const auto& tally : tagTallies) {
            auto desc = vault::find_tag_description(settings, tally.tag);
            auto item = TagOverviewItem(
                QString::fromStdString(tally.tag),
                tally.gallery_count,
                tally.image_count,
                QString::fromStdString(std::string(desc))
            );
            allTags_.append(item);
        }

        // Copy to filtered list
        tags_ = allTags_;
        emit tagsChanged();
    } catch (...) {
        qWarning() << "Exception refreshing tag overview";
    }
}

void TagOverviewController::sortBy(int sortKey)
{
    if (allTags_.isEmpty()) return;

    try {
        // Convert to ui::TagTally for sorting
        std::vector<ui::TagTally> tallies;
        for (const auto& item : allTags_) {
            tallies.push_back(ui::TagTally{
                item.tag.toStdString(),
                item.galleryCount,
                item.imageCount,
                item.description.toStdString()
            });
        }

        // Sort using the pure model function
        ui::sort_tags(tallies, sortKey == 0 ? ui::TagSort::Name : ui::TagSort::Count);

        // Convert back to QList
        tags_.clear();
        for (const auto& tally : tallies) {
            tags_.append(TagOverviewItem(
                QString::fromStdString(tally.tag),
                tally.gallery_count,
                tally.image_count,
                QString::fromStdString(tally.description)
            ));
        }

        emit tagsChanged();
    } catch (...) {
        qWarning() << "Exception sorting tags";
    }
}

void TagOverviewController::filterByPrefix(const QString& prefix)
{
    try {
        std::vector<ui::TagTally> tallies;
        for (const auto& item : allTags_) {
            tallies.push_back(ui::TagTally{
                item.tag.toStdString(),
                item.galleryCount,
                item.imageCount,
                item.description.toStdString()
            });
        }

        auto filtered = ui::filter_tags(tallies, prefix.toStdString());
        tags_.clear();
        for (const auto& tally : filtered) {
            tags_.append(TagOverviewItem(
                QString::fromStdString(tally.tag),
                tally.gallery_count,
                tally.image_count,
                QString::fromStdString(tally.description)
            ));
        }

        emit tagsChanged();
    } catch (...) {
        qWarning() << "Exception filtering tags";
    }
}

bool TagOverviewController::setTagDescription(const QString& tag, const QString& description)
{
    if (!isVaultReady()) return false;

    try {
        auto settings = vault::vault_settings(*vault_);
        vault::set_tag_description(settings, tag.toStdString(), description.toStdString());
        auto result = vault::set_vault_settings(*vault_, settings);

        if (result == vault::VaultResult::Ok) {
            // Update the local cache
            for (auto& item : allTags_) {
                if (item.tag == tag) {
                    item.description = description;
                }
            }
            for (auto& item : tags_) {
                if (item.tag == tag) {
                    item.description = description;
                }
            }
            emit tagsChanged();
            return true;
        }
    } catch (...) {
        qWarning() << "Exception setting tag description for" << tag;
    }
    return false;
}

QString TagOverviewController::importTagDictJson(const QByteArray& jsonBytes)
{
    if (!isVaultReady()) {
        return "Vault not unlocked";
    }

    lastImportSummary_.clear();

    try {
        // Parse the JSON dictionary
        auto parsed = ui::parse_tag_dict_json(
            std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(jsonBytes.data()),
                jsonBytes.size()
            )
        );

        // Apply to vault settings
        auto settings = vault::vault_settings(*vault_);
        auto summary = ui::apply_tag_dict(settings, parsed);

        // Try to commit
        auto result = vault::set_vault_settings(*vault_, settings);
        if (result != vault::VaultResult::Ok) {
            return "Failed to save vault settings";
        }

        // Build summary lines
        lastImportSummary_ = ui::tag_dict_summary_lines(summary);

        // Refresh the local tag list
        refresh();

        emit importFinished();
        return "";  // Empty string means success
    } catch (const std::exception& e) {
        qWarning() << "Exception importing tag dictionary:" << e.what();
        return QString::fromStdString(std::string(e.what()));
    } catch (...) {
        qWarning() << "Unknown exception importing tag dictionary";
        return "Unknown error";
    }
}

QStringList TagOverviewController::importSummaryLines() const
{
    QStringList result;
    for (const auto& line : lastImportSummary_) {
        result.append(QString::fromStdString(line));
    }
    return result;
}

void TagOverviewController::clearImportSummary()
{
    lastImportSummary_.clear();
}
