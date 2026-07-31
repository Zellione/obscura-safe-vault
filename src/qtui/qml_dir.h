#pragma once

#include <QString>

// Resolves the directory osv-qt loads its QML from. A packaged release binary
// isn't run from the source tree it was built in, so this can't be a
// compile-time path (unlike the test executables, which stay in-tree and use
// QTUI_QML_DIR directly) — it's the build/package layout's "qml/" next to the
// executable, overridable via OSV_QT_QML_DIR for local development.
QString resolveQmlDir();
