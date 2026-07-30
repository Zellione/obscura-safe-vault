#include "vault_list_model.h"

#include <algorithm>

#include "platform/paths.h"
#include "platform/vault_registry.h"

VaultListModel::VaultListModel(QObject* parent)
    : QAbstractListModel(parent)
{
    refresh();
}

int VaultListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(vaults_.size());
}

QVariant VaultListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto& p = vaults_[static_cast<size_t>(index.row())];
    switch (role) {
    case NameRole:
        return QString::fromStdString(p.filename().string());
    case DirRole:
        return QString::fromStdString(p.parent_path().string());
    default:
        return {};
    }
}

QHash<int, QByteArray> VaultListModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {DirRole, "dir"},
    };
}

void VaultListModel::refresh()
{
    beginResetModel();
    vaults_.clear();

    // 1) Registry entries, most-recent-first. VaultRegistry::list() has already
    //    run every line through platform::normalize_user_path (vaults.list is
    //    untrusted input). Keep only entries that exist as regular files.
    const auto registry = platform::VaultRegistry::default_location();
    for (const auto& p : registry.list()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) {
            vaults_.push_back(p);
        }
    }

    // 2) *.osv files in the per-user data folder (the folder the file dialog
    //    can't reach because it is under a hidden directory). OS-enumerated
    //    paths, alphabetical, deduped against the registry entries.
    const auto dataDir = platform::config_dir();
    if (!dataDir.empty()) {
        std::vector<std::filesystem::path> scanned;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dataDir, ec), end; !ec && it != end;
             it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".osv") {
                scanned.push_back(it->path());
            }
        }
        std::sort(scanned.begin(), scanned.end());
        for (auto& p : scanned) {
            if (std::find(vaults_.begin(), vaults_.end(), p) == vaults_.end()) {
                vaults_.push_back(std::move(p));
            }
        }
    }

    endResetModel();
    emit countChanged();
}

QUrl VaultListModel::fileUrlAt(int row) const
{
    if (row < 0 || row >= rowCount()) return {};
    return QUrl::fromLocalFile(
        QString::fromStdString(vaults_[static_cast<size_t>(row)].string()));
}
