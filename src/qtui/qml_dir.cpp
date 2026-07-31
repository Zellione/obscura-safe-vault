#include "qml_dir.h"

#include <QCoreApplication>
#include <QProcessEnvironment>

QString resolveQmlDir()
{
    const auto env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("OSV_QT_QML_DIR"));
    if (!env.isEmpty()) {
        return env;
    }
    return QCoreApplication::applicationDirPath() + QStringLiteral("/qml");
}
