#include "tag_list_import_adapter.h"

#include <QDebug>
#include "vault/vault.h"
#include "ui/tag_list_parse.h"

TagListImportAdapter::TagListImportAdapter(vault::Vault* vault)
    : vault_(vault)
{
}

bool TagListImportAdapter::isVaultReady() const
{
    return vault_ && vault_->is_unlocked();
}

QStringList TagListImportAdapter::parseTagList(const QByteArray& fileBytes) const
{
    try {
        auto parsed = ui::parse_tag_list(
            std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(fileBytes.data()),
                fileBytes.size()
            )
        );

        QStringList result;
        for (const auto& tag : parsed) {
            result.append(QString::fromStdString(tag));
        }
        return result;
    } catch (...) {
        qWarning() << "Exception parsing tag list";
    }
    return {};
}

int TagListImportAdapter::importTagsToNode(const QString& nodePath, const QStringList& tags)
{
    if (!isVaultReady()) {
        qWarning() << "Vault not ready";
        return -1;
    }

    int successCount = 0;
    std::string pathStr = nodePath.toStdString();

    try {
        for (const auto& tag : tags) {
            auto result = vault_->add_tag(pathStr, tag.toStdString());
            if (result == vault::VaultResult::Ok) {
                successCount++;
            }
        }
        return successCount;
    } catch (...) {
        qWarning() << "Exception importing tags to" << nodePath;
    }
    return -1;
}

int TagListImportAdapter::importTagsFromBytes(const QString& nodePath, const QByteArray& fileBytes)
{
    auto tags = parseTagList(fileBytes);
    return importTagsToNode(nodePath, tags);
}
