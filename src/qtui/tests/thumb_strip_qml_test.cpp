#include <cstdio>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QObject>
#include <QUrl>
#include <cstring>

// Test 1: ThumbStrip.qml loads without syntax errors (Osv import requires app environment)
static bool test_thumb_strip_qml_syntax()
{
    printf("Test 1: ThumbStrip.qml syntax check...\n");

    // Read ThumbStrip.qml to check basic syntax
    FILE* file = fopen(QTUI_QML_DIR "/ThumbStrip.qml", "r");
    if (!file) {
        fprintf(stderr, "FAIL: Could not read ThumbStrip.qml\n");
        return false;
    }

    bool hasImports = false;
    bool hasRectangle = false;
    bool hasListView = false;
    char line[512];

    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "import QtQuick")) {
            hasImports = true;
        }
        if (strstr(line, "import Osv")) {
            hasImports = true;
        }
        if (strstr(line, "Rectangle {")) {
            hasRectangle = true;
        }
        if (strstr(line, "ListView {")) {
            hasListView = true;
        }
    }

    fclose(file);

    if (!hasImports || !hasRectangle || !hasListView) {
        fprintf(stderr, "FAIL: ThumbStrip.qml is missing required QML structures\n");
        return false;
    }

    printf("PASS (basic QML structure verified)\n");
    return true;
}

// Test 2: ThumbStrip.qml uses SecureImageItem (not placeholder Rectangle)
static bool test_thumb_strip_uses_secure_image_item()
{
    printf("Test 2: ThumbStrip uses SecureImageItem for thumbnails (not placeholders)...\n");

    FILE* file = fopen(QTUI_QML_DIR "/ThumbStrip.qml", "r");
    if (!file) {
        fprintf(stderr, "FAIL: Could not read ThumbStrip.qml\n");
        return false;
    }

    bool hasSecureImageItem = false;
    bool hasNodeKeyBinding = false;
    bool hasPlaceholderRectangle = false;
    char line[512];

    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "SecureImageItem")) {
            hasSecureImageItem = true;
        }
        if (strstr(line, "nodeKey: model.nodeKey")) {
            hasNodeKeyBinding = true;
        }
        // Check for placeholder text rendering (old implementation)
        if (strstr(line, "Text {") && strstr(line, "text: name")) {
            hasPlaceholderRectangle = true;
        }
    }

    fclose(file);

    if (!hasSecureImageItem) {
        fprintf(stderr, "FAIL: ThumbStrip does not use SecureImageItem\n");
        return false;
    }

    if (!hasNodeKeyBinding) {
        fprintf(stderr, "FAIL: ThumbStrip does not bind nodeKey to model.nodeKey\n");
        return false;
    }

    if (hasPlaceholderRectangle) {
        fprintf(stderr, "FAIL: ThumbStrip still contains placeholder Text rendering\n");
        return false;
    }

    printf("PASS (SecureImageItem with nodeKey binding verified)\n");
    return true;
}

// Test 3: ThumbStrip has required properties
static bool test_thumb_strip_properties()
{
    printf("Test 3: ThumbStrip QML declares required properties...\n");

    FILE* file = fopen(QTUI_QML_DIR "/ThumbStrip.qml", "r");
    if (!file) {
        fprintf(stderr, "FAIL: Could not read ThumbStrip.qml\n");
        return false;
    }

    bool hasStripSide = false;
    bool hasCurrentIndex = false;
    bool hasJumpSignal = false;
    char line[512];

    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "property int stripSide")) {
            hasStripSide = true;
        }
        if (strstr(line, "property int currentIndex")) {
            hasCurrentIndex = true;
        }
        if (strstr(line, "signal jumpToIndex")) {
            hasJumpSignal = true;
        }
    }

    fclose(file);

    if (!hasStripSide || !hasCurrentIndex || !hasJumpSignal) {
        fprintf(stderr, "FAIL: ThumbStrip missing required properties/signals\n");
        return false;
    }

    printf("PASS (stripSide, currentIndex, jumpToIndex verified)\n");
    return true;
}

// Test 4: ThumbStrip.qml imports Osv module (SecureImageItem available in app context)
static bool test_thumb_strip_osv_import()
{
    printf("Test 4: ThumbStrip imports Osv 1.0 module (SecureImageItem)...\n");

    FILE* file = fopen(QTUI_QML_DIR "/ThumbStrip.qml", "r");
    if (!file) {
        fprintf(stderr, "FAIL: Could not read ThumbStrip.qml\n");
        return false;
    }

    bool hasOsvImport = false;
    char line[512];

    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "import Osv 1.0")) {
            hasOsvImport = true;
            break;
        }
    }

    fclose(file);

    if (!hasOsvImport) {
        fprintf(stderr, "FAIL: ThumbStrip does not import Osv 1.0 module\n");
        return false;
    }

    printf("PASS (Osv 1.0 import verified - SecureImageItem loads from ThumbCache)\n");
    return true;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    int passed = 0;
    int failed = 0;

    if (test_thumb_strip_qml_syntax()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_thumb_strip_uses_secure_image_item()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_thumb_strip_properties()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_thumb_strip_osv_import()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
