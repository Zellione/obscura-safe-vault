#include "tag_controller.h"

#include <QDebug>
#include "vault/vault.h"
#include "vault/vault_search.h"
#include "ui/tag_suggest.h"
#include "ui/tag_inherit.h"
#include "ui/tag_category.h"
#include "gfx/theme.h"

TagController::TagController(QObject* parent)
    : QObject(parent)
{
}

bool TagController::isVaultReady() const
{
    return vault_ && vault_->is_unlocked();
}

bool TagController::addTag(const QString& nodePath, const QString& tag)
{
    if (!isVaultReady()) return false;

    std::string path = nodePath.toStdString();
    std::string tagStr = tag.toStdString();

    try {
        auto result = vault_->add_tag(path, tagStr);
        if (result == vault::VaultResult::Ok) {
            emit tagsChanged(nodePath);
            return true;
        }
    } catch (...) {
        qWarning() << "Exception adding tag:" << tag;
    }
    return false;
}

bool TagController::removeTag(const QString& nodePath, const QString& tag)
{
    if (!isVaultReady()) return false;

    std::string path = nodePath.toStdString();
    std::string tagStr = tag.toStdString();

    try {
        auto result = vault_->remove_tag(path, tagStr);
        if (result == vault::VaultResult::Ok) {
            emit tagsChanged(nodePath);
            return true;
        }
    } catch (...) {
        qWarning() << "Exception removing tag:" << tag;
    }
    return false;
}

QStringList TagController::getOwnTags(const QString& nodePath) const
{
    if (!isVaultReady()) return {};

    std::string path = nodePath.toStdString();
    try {
        auto node = vault_->resolve_node(path);
        if (!node) return {};

        QStringList result;
        for (const auto& tag : node->tags) {
            result.append(QString::fromStdString(tag));
        }
        return result;
    } catch (...) {
        qWarning() << "Exception getting tags";
    }
    return {};
}

QStringList TagController::getInheritedTags(const QString& nodePath) const
{
    if (!isVaultReady()) return {};

    std::string path = nodePath.toStdString();
    try {
        auto tags = ui::inherited_tags(*vault_, path);
        QStringList result;
        for (const auto& tag : tags) {
            result.append(QString::fromStdString(tag));
        }
        return result;
    } catch (...) {
        qWarning() << "Exception getting inherited tags";
    }
    return {};
}

QStringList TagController::getContentsTags(const QString& nodePath) const
{
    if (!isVaultReady()) return {};

    std::string path = nodePath.toStdString();
    try {
        auto tags = ui::contents_tags(*vault_, path);
        QStringList result;
        for (const auto& tag : tags) {
            result.append(QString::fromStdString(tag));
        }
        return result;
    } catch (...) {
        qWarning() << "Exception getting contents tags";
    }
    return {};
}

QStringList TagController::getSuggestions(const QString& prefix, const QString& nodePath) const
{
    if (!isVaultReady()) return {};

    std::string prefixStr = prefix.toStdString();
    std::string path = nodePath.toStdString();

    try {
        auto node = vault_->resolve_node(path);
        if (!node) return {};

        // Get the full vocabulary from the vault using VaultSearch
        vault::VaultSearch search(*vault_);
        auto allTags = search.all_tags();

        // Get suggestions using the SDL UI function
        auto suggestions = ui::editor_tag_suggestions(prefixStr, allTags, node->tags);

        QStringList result;
        for (const auto& tag : suggestions) {
            result.append(QString::fromStdString(tag));
        }
        return result;
    } catch (...) {
        qWarning() << "Exception getting suggestions for" << prefix;
    }
    return {};
}

QStringList TagController::getVocabulary() const
{
    if (!isVaultReady()) return {};

    try {
        vault::VaultSearch search(*vault_);
        auto tags = search.all_tags();
        QStringList result;
        for (const auto& tag : tags) {
            result.append(QString::fromStdString(tag));
        }
        return result;
    } catch (...) {
        qWarning() << "Exception getting vocabulary";
    }
    return {};
}

QString TagController::getTagDisplayText(const QString& tag) const
{
    if (!isVaultReady()) return tag;

    std::string tagStr = tag.toStdString();
    try {
        auto settings = vault_settings(*vault_);
        auto display = ui::resolve_tag(tagStr, settings.categories);
        return QString::fromStdString(std::string(display.text));
    } catch (...) {
        return tag;
    }
}

int TagController::getTagSwatchIndex(const QString& tag) const
{
    if (!isVaultReady()) return -1;

    std::string tagStr = tag.toStdString();
    try {
        auto settings = vault_settings(*vault_);
        auto display = ui::resolve_tag(tagStr, settings.categories);
        return display.swatch;
    } catch (...) {
        return -1;
    }
}
