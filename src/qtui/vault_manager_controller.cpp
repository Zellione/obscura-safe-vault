#include "vault_manager_controller.h"

#include <QUrl>

#include "platform/vault_registry.h"
#include "platform/paths.h"
#include "vault_list_model.h"

VaultManagerController::VaultManagerController(QObject* parent)
    : QObject(parent),
      vaultListModel_(std::make_unique<VaultListModel>())
{
}

bool VaultManagerController::removeVaultFromRegistry(int row)
{
    if (!vaultListModel_ || row < 0 || row >= vaultListModel_->rowCount()) {
        return false;
    }

    const auto fileUrl = vaultListModel_->fileUrlAt(row);
    if (!fileUrl.isValid()) {
        return false;
    }

    const auto path = fileUrl.toLocalFile().toStdString();
    const auto pathOpt = platform::normalize_user_path(path);
    if (!pathOpt.has_value()) {
        return false;
    }

    const auto registry = platform::VaultRegistry::default_location();
    const bool removed = registry.remove(pathOpt.value());
    if (removed) {
        vaultListModel_->refresh();
    }
    return removed;
}

bool VaultManagerController::addVaultToRegistry(const QUrl& fileUrl)
{
    const auto path = fileUrl.toLocalFile().toStdString();
    const auto pathOpt = platform::normalize_user_path(path);
    if (!pathOpt.has_value()) {
        return false;
    }

    const auto registry = platform::VaultRegistry::default_location();
    const bool added = registry.add(pathOpt.value());
    if (added) {
        vaultListModel_->refresh();
    }
    return added;
}

void VaultManagerController::refreshVaultList()
{
    if (vaultListModel_) {
        vaultListModel_->refresh();
    }
}
