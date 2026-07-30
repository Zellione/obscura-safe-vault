#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <string>

#include "ui/settings_model.h"

// Controller for the settings overlay.
// Adapts ui::settings_model to Qt/QML by exposing theme selection,
// vault-scoped browsing defaults (sort order, tiles-show-tags), auto-lock,
// and tag category CRUD with validation.
//
// Theme persistence: uses platform::ThemePref to read/write theme.conf.
// Vault-scoped operations are gated behind vaultUnlocked (no-op/error when locked).
//
// Threading: GUI-thread only (no locking; all calls are Q_INVOKABLE).
class SettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList themeList READ themeList CONSTANT)
    Q_PROPERTY(int currentThemeIndex READ currentThemeIndex WRITE setCurrentThemeIndex NOTIFY themeIndexChanged)
    Q_PROPERTY(bool vaultUnlocked READ vaultUnlocked WRITE setVaultUnlocked NOTIFY vaultUnlockedChanged)
    Q_PROPERTY(QStringList sortOrderList READ sortOrderList CONSTANT)
    Q_PROPERTY(int currentSortOrderIndex READ currentSortOrderIndex WRITE setCurrentSortOrderIndex NOTIFY sortOrderChanged)
    Q_PROPERTY(bool tilesShowTags READ tilesShowTags WRITE setTilesShowTags NOTIFY tilesShowTagsChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(QString errorLine READ errorLine NOTIFY errorLineChanged)

public:
    explicit SettingsController(QObject* parent = nullptr);
    ~SettingsController();

    // Read-only properties
    [[nodiscard]] QStringList themeList() const { return themeList_; }
    [[nodiscard]] int currentThemeIndex() const { return currentThemeIndex_; }
    [[nodiscard]] bool vaultUnlocked() const { return vaultUnlocked_; }
    [[nodiscard]] QStringList sortOrderList() const { return sortOrderList_; }
    [[nodiscard]] int currentSortOrderIndex() const { return currentSortOrderIndex_; }
    [[nodiscard]] bool tilesShowTags() const { return tilesShowTags_; }
    [[nodiscard]] QVariantList categories() const;
    [[nodiscard]] QString errorLine() const { return errorLine_; }

    // Setters
    void setCurrentThemeIndex(int index);
    void setVaultUnlocked(bool unlocked);
    void setCurrentSortOrderIndex(int index);
    void setTilesShowTags(bool show);

    // For testing: override theme persistence path (normally uses XDG default)
    void setThemePersistPath(const std::string& path);

    // Category CRUD operations; return empty string on success, error message on failure
    Q_INVOKABLE QString addCategory(QString name);
    Q_INVOKABLE QString renameCategory(int index, QString name);
    Q_INVOKABLE void removeCategory(int index);

signals:
    void themeIndexChanged();
    void vaultUnlockedChanged();
    void sortOrderChanged();
    void tilesShowTagsChanged();
    void categoriesChanged();
    void errorLineChanged();

private:
    void updateErrorLine(const QString& error);
    void notifyThemeChange();

    QStringList themeList_;
    QStringList sortOrderList_;
    int currentThemeIndex_ = 0;
    int currentSortOrderIndex_ = 0;
    bool vaultUnlocked_ = false;
    bool tilesShowTags_ = true;
    QString errorLine_;

    std::string themePersistPath_;
    ui::SettingsState settingsState_;
};
