#include "platform/paths.h"

#include <QStandardPaths>
#include <QString>
#include <filesystem>

namespace platform {

std::filesystem::path config_dir()
{
    // Use Qt's standard paths instead of SDL to avoid static linkage conflicts
    // with Qt's Wayland platform plugin.
    QString configPath = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);

    if (configPath.isEmpty()) {
        return {};
    }

    return std::filesystem::path{configPath.toStdString()};
}

} // namespace platform
