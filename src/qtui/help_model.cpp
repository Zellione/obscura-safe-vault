#include "help_model.h"

HelpModel::HelpModel(QObject* parent)
    : QObject(parent)
{
    updateGroups();
}

HelpModel::~HelpModel() = default;

void HelpModel::setScreenGroups(const QVariantList& screenGroups)
{
    // No-op if groups are identical
    if (screenGroups_ == screenGroups) {
        return;
    }

    screenGroups_ = screenGroups;
    updateGroups();
    emit groupsChanged();
}

void HelpModel::updateGroups()
{
    allGroups_.clear();

    // Always prepend the synthesized Global group
    allGroups_.append(createGlobalGroup());

    // Append screen groups in order
    for (const auto& screenGroup : screenGroups_) {
        allGroups_.append(screenGroup);
    }
}

QVariantMap HelpModel::createGlobalGroup() const
{
    QVariantMap globalGroup;
    globalGroup.insert("title", "Global");

    QVariantList entries;

    // F1 — Help
    {
        QVariantMap entry;
        entry.insert("keys", "F1");
        entry.insert("description", "Help");
        entries.append(entry);
    }

    // F2 — Settings
    {
        QVariantMap entry;
        entry.insert("keys", "F2");
        entry.insert("description", "Settings");
        entries.append(entry);
    }

    // Right-click — Back / up one level
    {
        QVariantMap entry;
        entry.insert("keys", "Right-click");
        entry.insert("description", "Back / up one level");
        entries.append(entry);
    }

    globalGroup.insert("entries", entries);

    return globalGroup;
}
