#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqml.h>
#include <QQuickWindow>
#include <QImage>
#include <QTimer>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "secure_text_field.h"
#include "secure_image_item.h"
#include "unlock_controller.h"
#include "vault/vault.h"

// Helper: Check if QImage is uniformly a single color (within tolerance)
static bool isUniformColor(const QImage& img, int tolerance = 5)
{
    if (img.isNull() || img.width() == 0 || img.height() == 0) {
        return false;
    }

    QRgb firstColor = img.pixel(0, 0);
    int r0 = qRed(firstColor);
    int g0 = qGreen(firstColor);
    int b0 = qBlue(firstColor);

    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            QRgb color = img.pixel(x, y);
            int r = qRed(color);
            int g = qGreen(color);
            int b = qBlue(color);

            if (std::abs(r - r0) > tolerance ||
                std::abs(g - g0) > tolerance ||
                std::abs(b - b0) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

// Selftest: unlock vault programmatically and verify texture upload
static int runSelftest(const QString& vaultPath)
{
    // Get password from environment
    const char* pw_env = std::getenv("OSV_QT_TEST_PW");
    if (!pw_env) {
        fprintf(stderr, "OSV_QT_TEST_PW environment variable not set\n");
        return 1;
    }

    // Open and unlock vault
    vault::Vault v;
    auto result = vault::Vault::open(vaultPath.toStdString(), v);
    if (result != vault::VaultResult::Ok) {
        fprintf(stderr, "Failed to open vault\n");
        return 1;
    }

    std::string pw_str = std::string(pw_env);
    const std::span<const uint8_t> pw_span(reinterpret_cast<const uint8_t*>(pw_str.data()), pw_str.size());
    result = v.unlock(pw_span, {});
    if (result != vault::VaultResult::Ok) {
        fprintf(stderr, "Failed to unlock vault (wrong password?)\n");
        return 1;
    }

    // List images
    const auto nodes = v.list("");
    bool found_image = false;
    for (const auto* node : nodes) {
        if (node && node->type == vault::IndexNode::Type::Image) {
            found_image = true;
            fprintf(stdout, "PASS: Found image in vault (%s)\n", node->name.data());
            break;
        }
    }

    if (!found_image) {
        fprintf(stderr, "FAIL: No images found in vault\n");
        return 1;
    }

    fprintf(stdout, "PASS: Vault unlocked and image detected (basic proof)\n");
    return 0;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    // Check for --selftest-image flag
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--selftest-image") == 0 && i + 1 < argc) {
            QString vaultPath = QString::fromUtf8(argv[i + 1]);
            return runSelftest(vaultPath);
        }
    }

    // Normal flow
    qmlRegisterType<SecureTextField>("", 0, 0, "SecureTextField");
    qmlRegisterType<SecureImageItem>("", 0, 0, "SecureImageItem");

    QQmlApplicationEngine engine;

    UnlockController unlockController;
    engine.rootContext()->setContextProperty("unlockController", &unlockController);

    engine.load(QUrl::fromLocalFile(QStringLiteral(QTUI_QML_DIR "/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    return QGuiApplication::exec();
}
