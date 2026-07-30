#include "settings_controller.h"

#include <QtCore/qloggingcategory.h>

#include "gfx/theme.h"
#include "platform/theme_pref.h"
#include "ui/tag_inherit.h"

Q_LOGGING_CATEGORY(lcSettingsController, "osv.settings_controller")

SettingsController::SettingsController(QObject* parent)
    : QObject(parent)
{
    qCDebug(lcSettingsController) << "SettingsController constructed";

    // Build theme list from gfx API
    for (int i = 0; i < gfx::THEME_COUNT; ++i) {
        themeList_.append(
            QString::fromUtf8(gfx::theme_name(static_cast<gfx::ThemeId>(i)))
        );
    }

    // Initialize settings model
    settingsState_.vault_unlocked = false;
    settingsState_.theme = gfx::active_theme_id();
    currentThemeIndex_ = static_cast<int>(gfx::active_theme_id());
}

SettingsController::~SettingsController()
{
    qCDebug(lcSettingsController) << "SettingsController destroyed";
}

void SettingsController::setCurrentThemeIndex(int index)
{
    qCDebug(lcSettingsController) << "setCurrentThemeIndex(" << index << ")";

    if (index < 0 || index >= gfx::THEME_COUNT) {
        qCWarning(lcSettingsController) << "Invalid theme index:" << index;
        return;
    }

    if (index == currentThemeIndex_) {
        return;  // No change
    }

    currentThemeIndex_ = index;

    // Apply theme immediately
    auto themeId = static_cast<gfx::ThemeId>(index);
    gfx::set_theme(themeId);

    // Persist theme to disk
    std::string pathToUse = themePersistPath_;
    if (pathToUse.empty()) {
        auto pref = platform::ThemePref::default_location();
        pathToUse = pref.file().string();
    }
    auto pref = platform::ThemePref(pathToUse);
    bool saved = pref.save(themeId);
    if (!saved) {
        qCWarning(lcSettingsController) << "Failed to persist theme";
    }

    // Update internal state
    settingsState_.theme = themeId;

    emit themeIndexChanged();
}

void SettingsController::setVaultUnlocked(bool unlocked)
{
    qCDebug(lcSettingsController) << "setVaultUnlocked(" << unlocked << ")";

    if (unlocked == vaultUnlocked_) {
        return;
    }

    vaultUnlocked_ = unlocked;
    settingsState_.vault_unlocked = unlocked;

    emit vaultUnlockedChanged();
}

void SettingsController::setThemePersistPath(const std::string& path)
{
    themePersistPath_ = path;

    // Load persisted theme from the new path
    auto pref = platform::ThemePref(themePersistPath_);
    auto loadedTheme = pref.load();
    currentThemeIndex_ = static_cast<int>(loadedTheme);
    settingsState_.theme = loadedTheme;
}

QString SettingsController::addCategory(QString name)
{
    qCDebug(lcSettingsController) << "addCategory(" << name << ")";

    if (!vaultUnlocked_) {
        QString err = "Unlock a vault to configure";
        updateErrorLine(err);
        return err;
    }

    // Use the model's category add function
    std::string nameStd = name.toStdString();
    bool success = ui::settings_add_category(settingsState_, nameStd);

    if (success) {
        updateErrorLine("");
        emit categoriesChanged();
        return "";
    }

    // Determine the error message
    QString error;

    // Trim to check if blank
    std::string trimmed = nameStd;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
        trimmed.erase(0, 1);
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
        trimmed.pop_back();
    }

    if (trimmed.empty()) {
        error = "Category name cannot be empty";
    } else if (nameStd.length() > vault::INDEX_MAX_CATEGORY_BYTES) {
        error = "Category name too long";
    } else {
        // Check for case-insensitive duplicate
        for (const auto& cat : settingsState_.draft.categories) {
            if (ui::tag_ci_equal(cat.name, nameStd)) {
                error = "Category already exists (case-insensitive)";
                break;
            }
        }
        if (error.isEmpty()) {
            error = "Failed to add category";
        }
    }

    updateErrorLine(error);
    return error;
}

QString SettingsController::renameCategory(int index, QString name)
{
    qCDebug(lcSettingsController) << "renameCategory(" << index << ", " << name << ")";

    if (!vaultUnlocked_) {
        QString err = "Unlock a vault to configure";
        updateErrorLine(err);
        return err;
    }

    // Validate index
    if (index < 0 || index >= static_cast<int>(settingsState_.draft.categories.size())) {
        QString err = "Invalid category index";
        updateErrorLine(err);
        return err;
    }

    std::string nameStd = name.toStdString();
    bool success = ui::settings_rename_category(settingsState_, index, nameStd);

    if (success) {
        updateErrorLine("");
        emit categoriesChanged();
        return "";
    }

    // Determine the error message
    QString error;

    // Trim to check if blank
    std::string trimmed = nameStd;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
        trimmed.erase(0, 1);
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
        trimmed.pop_back();
    }

    if (trimmed.empty()) {
        error = "Category name cannot be empty";
    } else if (nameStd.length() > vault::INDEX_MAX_CATEGORY_BYTES) {
        error = "Category name too long";
    } else if (ui::tag_ci_equal(settingsState_.draft.categories[index].name, nameStd)) {
        error = "New name is the same as current (case-insensitive)";
    } else {
        // Check for case-insensitive duplicate with other categories
        for (int i = 0; i < static_cast<int>(settingsState_.draft.categories.size()); ++i) {
            if (i != index && ui::tag_ci_equal(settingsState_.draft.categories[i].name, nameStd)) {
                error = "Category already exists (case-insensitive)";
                break;
            }
        }
        if (error.isEmpty()) {
            error = "Failed to rename category";
        }
    }

    updateErrorLine(error);
    return error;
}

void SettingsController::removeCategory(int index)
{
    qCDebug(lcSettingsController) << "removeCategory(" << index << ")";

    if (!vaultUnlocked_) {
        updateErrorLine("Unlock a vault to configure");
        return;
    }

    ui::settings_remove_category(settingsState_, index);
    updateErrorLine("");
    emit categoriesChanged();
}

QVariantList SettingsController::categories() const
{
    QVariantList result;

    for (const auto& cat : settingsState_.draft.categories) {
        QVariantMap item;
        item.insert("name", QString::fromStdString(cat.name));
        item.insert("swatchIndex", static_cast<int>(cat.swatch));
        result.append(item);
    }

    return result;
}

void SettingsController::updateErrorLine(const QString& error)
{
    if (error == errorLine_) {
        return;
    }

    errorLine_ = error;
    emit errorLineChanged();
}
