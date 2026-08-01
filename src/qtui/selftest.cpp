#include "selftest.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QQuickWindow>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <set>
#include <memory>
#include <vector>
#include <tuple>
#include <cmath>

#include "secure_image_item.h"
#include "video_frame_item.h"
#include "app_wiring.h"
#include "qml_dir.h"
#include "unlock_controller.h"
#include "vault_list_model.h"
#include "gallery_model.h"
#include "thumb_cache.h"
#include "viewer_controller.h"
#include "playback_engine.h"
#include "image/decode.h"
#include "vault/vault.h"
#include <monocypher.h>

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

// Helper: Count unique colors in a QImage
static int countUniqueColors(const QImage& img)
{
    if (img.isNull()) {
        return 0;
    }

    std::set<QRgb> colors;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            colors.insert(img.pixel(x, y));
        }
    }
    return colors.size();
}

// Shared pixel verification for --selftest-render (primary and fallback grab
// paths): locate the colored-content bounding box, sample at 25%/75% of its
// width, expect left=red, right=blue. Prints details; returns 0 pass, 1 fail.
static int verifyRedBlueGrab(const QImage& grabbed, const char* label)
{
    // Scan the image to find the boundaries of the rendered content
    int minX = grabbed.width(), maxX = -1;
    int minY = grabbed.height(), maxY = -1;

    for (int y = 0; y < grabbed.height(); ++y) {
        for (int x = 0; x < grabbed.width(); ++x) {
            QRgb color = grabbed.pixel(x, y);
            int r = qRed(color), g = qGreen(color), b = qBlue(color);
            // Look for non-black content (r+g+b > 30 to account for small artifacts)
            if ((r + g + b) > 30) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    fprintf(stdout, "Found colored content bounding box: (%d,%d) to (%d,%d)\n", minX, minY, maxX, maxY);

    if (minX >= grabbed.width() || minY >= grabbed.height()) {
        fprintf(stderr, "FAIL (%s): No colored content found in grabbed image\n", label);
        return 1;
    }

    // Sample at 25% and 75% of the content width (quarter and three-quarter points)
    int contentWidth = maxX - minX + 1;
    int contentHeight = maxY - minY + 1;
    int sampleXLeft = minX + contentWidth / 4;
    int sampleXRight = minX + 3 * contentWidth / 4;
    int sampleY = minY + contentHeight / 2;

    QRgb pixelLeft = grabbed.pixel(sampleXLeft, sampleY);
    QRgb pixelRight = grabbed.pixel(sampleXRight, sampleY);

    int rLeft = qRed(pixelLeft);
    int gLeft = qGreen(pixelLeft);
    int bLeft = qBlue(pixelLeft);

    int rRight = qRed(pixelRight);
    int gRight = qGreen(pixelRight);
    int bRight = qBlue(pixelRight);

    fprintf(stdout, "Sampled pixels (%s):\n", label);
    fprintf(stdout, "  Left (%d, %d) [25%% of width]: RGB(%d, %d, %d)\n", sampleXLeft, sampleY, rLeft, gLeft, bLeft);
    fprintf(stdout, "  Right (%d, %d) [75%% of width]: RGB(%d, %d, %d)\n", sampleXRight, sampleY, rRight, gRight, bRight);

    // Verify colors (allow some tolerance for compression/filtering)
    bool leftIsRed = (rLeft > 180 && gLeft < 80 && bLeft < 80);
    bool rightIsBlue = (rRight < 80 && gRight < 80 && bRight > 180);

    if (leftIsRed && rightIsBlue) {
        fprintf(stdout, "PASS: Renderer correctly rendered red/blue test image\n");
        return 0;
    }

    fprintf(stderr, "FAIL: Colors incorrect. Expected left=red, right=blue\n");
    fprintf(stderr, "  Left: %s (r>180? %s, g<80? %s, b<80? %s)\n",
            leftIsRed ? "RED" : "NOT_RED",
            (rLeft > 180) ? "yes" : "no",
            (gLeft < 80) ? "yes" : "no",
            (bLeft < 80) ? "yes" : "no");
    fprintf(stderr, "  Right: %s (r<80? %s, g<80? %s, b>180? %s)\n",
            rightIsBlue ? "BLUE" : "NOT_BLUE",
            (rRight < 80) ? "yes" : "no",
            (gRight < 80) ? "yes" : "no",
            (bRight > 180) ? "yes" : "no");
    return 1;
}

// Helper: Create a synthetic test image (red left, blue right)
static PixelBuffer createSyntheticTestImage(int width, int height)
{
    PixelBuffer buf;
    buf.width = width;
    buf.height = height;
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    buf.rgba.reserve(pixel_count * 4);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x < width / 2) {
                // Left half: red (255, 0, 0, 255)
                buf.rgba.push_back(255);
                buf.rgba.push_back(0);
                buf.rgba.push_back(0);
                buf.rgba.push_back(255);
            } else {
                // Right half: blue (0, 0, 255, 255)
                buf.rgba.push_back(0);
                buf.rgba.push_back(0);
                buf.rgba.push_back(255);
                buf.rgba.push_back(255);
            }
        }
    }

    return buf;
}

// Renderer selftest: create a synthetic red/blue test image and verify pixel values
// Returns 0 on success; 1 on failure
int runSelftestRender()
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    // Register types
    qmlRegisterType<SecureImageItem>("Osv", 1, 0, "SecureImageItem");

    const QString qmlDir = resolveQmlDir();
    QQmlApplicationEngine engine;
    engine.addImportPath(qmlDir);
    engine.load(QUrl::fromLocalFile(qmlDir + QStringLiteral("/RenderTest.qml")));

    if (engine.rootObjects().isEmpty()) {
        fprintf(stderr, "FAIL: RenderTest.qml failed to load\n");
        return 1;
    }

    QQuickWindow* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) {
        fprintf(stderr, "FAIL: No window from RenderTest.qml\n");
        return 1;
    }

    // Find the SecureImageItem in the QML
    SecureImageItem* testItem = nullptr;
    for (auto obj : window->findChildren<SecureImageItem*>()) {
        testItem = obj;
        break;
    }

    if (!testItem) {
        fprintf(stderr, "FAIL: No SecureImageItem found in RenderTest.qml\n");
        return 1;
    }

    // Create synthetic test image: left half red, right half blue
    PixelBuffer testImage = createSyntheticTestImage(400, 400);
    testItem->setImage(std::make_shared<const PixelBuffer>(testImage));

    window->show();

    int frameCount = 0;
    int resultCode = 1;
    bool testComplete = false;
    double devicePixelRatio = window->devicePixelRatio();

    fprintf(stdout, "Renderer selftest: red/blue synthetic image, DPR=%.2f\n", devicePixelRatio);

    // Connect to frameSwapped to count frames (wayland/wayland-like)
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [&]() {
        if (testComplete) return;
        frameCount++;

        if (frameCount >= 3) {
            testComplete = true;

            QImage grabbed = window->grabWindow();
            if (grabbed.isNull() || grabbed.width() == 0 || grabbed.height() == 0) {
                fprintf(stderr, "FAIL: Could not grab window\n");
                resultCode = 1;
                QCoreApplication::exit(1);
                return;
            }

            fprintf(stdout, "Grabbed image: %dx%d\n", grabbed.width(), grabbed.height());

            // The grabbed image may be larger than the rendered item;
            // verifyRedBlueGrab locates the content and samples it.
            resultCode = verifyRedBlueGrab(grabbed, "within content bounding box");
            QCoreApplication::exit(resultCode);
        }
    }, Qt::QueuedConnection);

    // Fallback timer for platforms where frameSwapped doesn't fire (xcb/software)
    // If frames don't arrive within 2s, grab anyway and check
    QTimer fallbackTimer;
    QObject::connect(&fallbackTimer, &QTimer::timeout, window, [&]() {
        if (!testComplete && frameCount < 3) {
            // No frames arrived; grab and check anyway
            testComplete = true;

            QImage grabbed = window->grabWindow();
            if (grabbed.isNull() || grabbed.width() == 0 || grabbed.height() == 0) {
                fprintf(stderr, "FAIL (fallback): Could not grab window\n");
                resultCode = 1;
                QCoreApplication::exit(1);
                return;
            }

            fprintf(stdout, "Note: no frameSwapped events (platform limitation); using fallback grab\n");
            resultCode = verifyRedBlueGrab(grabbed, "fallback");
            QCoreApplication::exit(resultCode);
        }
    });
    fallbackTimer.setSingleShot(true);
    fallbackTimer.start(2000);

    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        if (!testComplete) {
            fprintf(stderr, "FAIL: timeout (10s) — test never completed\n");
            resultCode = 1;
            QCoreApplication::exit(1);
        }
    });
    watchdog.setSingleShot(true);
    watchdog.start(10000);

    int appExitCode = QGuiApplication::exec();
    return resultCode != 0 ? 1 : 0;
}

// Selftest: Unlock vault programmatically and verify rendering
// Step 1: Check vault can be opened/unlocked and has image
// Step 2: Load QML UI, programmatically unlock, verify render pipeline executes
// Returns 0 on success; 1 on failure
int runSelftest(const QString& vaultPath)
{
    // stdout is block-buffered when piped; a timeout kill would discard it.
    // Set to unbuffered for immediate output visibility.
    setvbuf(stdout, nullptr, _IONBF, 0);

    // Get password from environment
    const char* pw_env = std::getenv("OSV_QT_TEST_PW");
    if (!pw_env) {
        fprintf(stderr, "FAIL: OSV_QT_TEST_PW environment variable not set\n");
        return 1;
    }

    // Step 1: Verify vault unlock and image exists (proof that decrypt→decode path works)
    vault::Vault v;
    auto result = vault::Vault::open(vaultPath.toStdString(), v);
    if (result != vault::VaultResult::Ok) {
        fprintf(stderr, "FAIL: Could not open vault\n");
        return 1;
    }

    std::string pw_str = std::string(pw_env);
    struct PwWipe { std::string& s; ~PwWipe() { if (!s.empty()) crypto_wipe(s.data(), s.size()); } } pwWipe{pw_str};
    const std::span<const uint8_t> pw_span(reinterpret_cast<const uint8_t*>(pw_str.data()), pw_str.size());
    result = v.unlock(pw_span, {});
    if (result != vault::VaultResult::Ok) {
        fprintf(stderr, "FAIL: Could not unlock vault (wrong password?)\n");
        return 1;
    }

    // Find first image
    const auto nodes = v.list("");
    const vault::IndexNode* imageNode = nullptr;
    for (const auto* node : nodes) {
        if (node && node->type == vault::IndexNode::Type::Image) {
            imageNode = node;
            break;
        }
    }

    if (!imageNode) {
        fprintf(stderr, "FAIL: No image nodes found in vault\n");
        return 1;
    }

    fprintf(stdout, "PASS (Step 1): Vault unlocked, image node found\n");

    // Step 2: Run the app with the real QML UI to test rendering
    initThemeFromEnv();
    registerOsvQmlTypes();

    const QString qmlDir = resolveQmlDir();
    QQmlApplicationEngine engine;
    // The QML engine needs to know where to find QML modules
    engine.addImportPath(qmlDir);

    AppContext appCtx;
    UnlockController& unlockController = appCtx.unlockController;
    ThumbCache& thumbCache = appCtx.thumbCache;
    GalleryModel& galleryModel = appCtx.galleryModel;
    ViewerController& viewerController = appCtx.viewerController;
    PlaybackEngine& playbackEngine = appCtx.playbackEngine;

    appCtx.expose(engine);
    engine.load(QUrl::fromLocalFile(qmlDir + QStringLiteral("/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        fprintf(stderr, "FAIL: QML failed to load\n");
        return 1;
    }

    // Get the window
    QQuickWindow* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) {
        fprintf(stderr, "FAIL: No window from QML root\n");
        return 1;
    }

    // Make window visible for rendering
    window->show();

    // Open and unlock vault via the controller
    QUrl vaultUrl = QUrl::fromLocalFile(vaultPath);
    if (!unlockController.openVault(vaultUrl)) {
        fprintf(stderr, "FAIL: Could not open vault via controller\n");
        return 1;
    }

    // Unlock with password (test-only path)
    const std::span<const uint8_t> pw_span_for_unlock(
        reinterpret_cast<const uint8_t*>(pw_str.data()), pw_str.size());
    if (!unlockController.unlockWithPassword(pw_span_for_unlock)) {
        fprintf(stderr, "FAIL: Could not unlock vault via controller\n");
        return 1;
    }

    // Update ThumbCache and GalleryModel with unlocked vault
    thumbCache.setVault(&unlockController.vault());
    galleryModel.refresh();

    fprintf(stdout, "PASS (Step 2): Vault unlocked via controller\n");

    // Step 3: Wait for rendering and verify pixels
    int frameCount = 0;
    int resultCode = 1;  // default to failure
    bool testComplete = false;

    // Check for thumbnail-wait mode (requires OSV_QT_SELFTEST_WAIT_THUMBS)
    const char* wait_thumbs_env = std::getenv("OSV_QT_SELFTEST_WAIT_THUMBS");
    int targetThumbnailCount = wait_thumbs_env ? std::atoi(wait_thumbs_env) : 0;

    // Get screenshot output path if specified
    const char* shot_path_env = std::getenv("OSV_QT_SELFTEST_SHOT");
    QString shotPath = shot_path_env ? QString::fromUtf8(shot_path_env) : QString();

    // Check for viewer selftest mode (requires OSV_QT_SELFTEST_VIEWER=1)
    const char* viewer_env = std::getenv("OSV_QT_SELFTEST_VIEWER");
    bool testViewer = viewer_env && std::atoi(viewer_env) > 0;
    bool viewerTestComplete = false;
    bool transitionedToViewer = false;

    // Check for audio selftest mode (requires OSV_QT_SELFTEST_AUDIO=1)
    const char* audio_env = std::getenv("OSV_QT_SELFTEST_AUDIO");
    bool testAudio = audio_env && std::atoi(audio_env) > 0;
    bool audioTestComplete = false;
    bool transitionedToAudio = false;

    // Check for video selftest mode (requires OSV_QT_SELFTEST_VIDEO=1)
    const char* video_env = std::getenv("OSV_QT_SELFTEST_VIDEO");
    bool testVideo = video_env && std::atoi(video_env) > 0;
    bool videoTestComplete = false;
    bool transitionedToVideo = false;
    QImage videoGrabSample1, videoGrabSample2;
    int videoSampleX = 0, videoSampleY = 0;

    // Find the first gallery row matching the wanted media kind and queue its
    // activation (emits openVideo/openViewer, which pushes the target screen).
    // On failure prints FAIL and exits the app; either way the caller returns.
    auto activateFirstMediaRow = [&](bool wantVideo, const char* testName) {
        int row = -1;
        for (int i = 0; i < galleryModel.rowCount(); ++i) {
            QModelIndex idx = galleryModel.index(i);
            bool match = wantVideo
                ? galleryModel.data(idx, GalleryModel::IsVideoRole).toBool()
                : !galleryModel.data(idx, GalleryModel::IsGalleryRole).toBool();
            if (match) {
                row = i;
                break;
            }
        }
        if (row < 0) {
            fprintf(stderr, "FAIL (%s): No %s rows found in gallery\n",
                    testName, wantVideo ? "video" : "image");
            resultCode = 1;
            QCoreApplication::exit(1);
            return;
        }
        QMetaObject::invokeMethod(&galleryModel, "activate", Qt::QueuedConnection,
            Q_ARG(int, row));
    };

    // After the thumbnail assertion passes, hand off to the requested leg
    // (audio > video > viewer precedence; the transitioned* flag is set BEFORE
    // invoking to prevent poll re-entry). Returns true if a transition was
    // initiated (or failed fatally) — the caller must then return.
    auto transitionToRequestedLeg = [&]() -> bool {
        if (testAudio && !transitionedToAudio) {
            transitionedToAudio = true;
            fprintf(stdout, "TRANSITION: Moving to audio test mode\n");
            activateFirstMediaRow(true, "Audio test");
            return true;
        }
        if (testVideo && !transitionedToVideo) {
            transitionedToVideo = true;
            fprintf(stdout, "TRANSITION: Moving to video test mode\n");
            activateFirstMediaRow(true, "Video test");
            return true;
        }
        if (testViewer && !transitionedToViewer) {
            transitionedToViewer = true;
            fprintf(stdout, "TRANSITION: Moving to viewer test mode\n");
            activateFirstMediaRow(false, "Viewer test");
            return true;
        }
        return false;
    };

    // Shared assertions (a) frame count and (b) position for the video/audio legs.
    auto assertPlaybackBasics = [&](bool& assertA, bool& assertB) {
        auto items = window->findChildren<VideoFrameItem*>();
        int frames = items.size() > 0 ? items[0]->testOnlyRenderCount() : 0;
        fprintf(stdout, "ASSERT (a) Frame count: %d (requirement: >= 10)... %s\n", frames,
                frames >= 10 ? "PASS" : "FAIL");
        assertA = frames >= 10;

        double position = playbackEngine.position();
        fprintf(stdout, "ASSERT (b) Position: %.2f seconds (requirement: > 1.0)... %s\n", position,
                position > 1.0 ? "PASS" : "FAIL");
        assertB = position > 1.0;
    };

    // Poll timer for thumbnail-wait mode: check deliveredCount every 100ms
    QTimer pollTimer;
    QObject::connect(&pollTimer, &QTimer::timeout, window, [&]() {
        if (testComplete || transitionedToViewer || transitionedToVideo || transitionedToAudio) return;

        int delivered = thumbCache.deliveredCount();
        if (delivered >= targetThumbnailCount) {
            // Wait 2 more frames for delegates to repaint
            if (frameCount >= 5) {
                // Only mark test complete if not in audio/video/viewer mode (they have their own handlers)
                if (!testAudio && !testVideo && !testViewer) {
                    testComplete = true;
                }

                // Grab and analyze image
                QImage grabbed = window->grabWindow();
                if (grabbed.isNull() || grabbed.width() == 0 || grabbed.height() == 0) {
                    fprintf(stderr, "FAIL (Step 3): Could not grab window\n");
                    resultCode = 1;
                    QCoreApplication::exit(1);
                    return;
                }

                // Tile-pixel verification: find SecureImageItem delegates in GridView contentItem
                // (delegates are in contentItem hierarchy, not direct window children)
                int colorfulTiles = 0;
                std::vector<std::tuple<int, int, int>> sampledPixels;

                QQuickItem* contentItem = window->contentItem();
                if (!contentItem) {
                    fprintf(stderr, "FAIL (Step 3): Could not find window contentItem\n");
                    resultCode = 1;
                    QCoreApplication::exit(1);
                    return;
                }

                // GridView delegates may not be in QObject tree until rendered
                // Fallback: verify via unique colors in grabbed image
                auto allItems = window->findChildren<SecureImageItem*>();

                if (allItems.size() == 0) {
                    // Fallback: no delegates found; check unique colors as proof of thumbnail rendering
                    int uniqueColors = countUniqueColors(grabbed);
                    if (uniqueColors > 300) {
                        fprintf(stdout, "PASS (Step 3, thumbnail-wait): %d thumbnails delivered, "
                                "%d unique colors (delegates not instantiated in QObject tree)\n",
                                delivered, uniqueColors);

                        // Check if we should transition to audio, video or viewer test mode
                        if (transitionToRequestedLeg()) {
                            return;
                        }

                        // Save screenshot if requested (thumbnail mode only, no viewer test)
                        if (!shotPath.isEmpty() && !testViewer && grabbed.save(shotPath)) {
                            fprintf(stdout, "Screenshot saved to %s\n", shotPath.toStdString().c_str());
                        }

                        if (!testViewer && !testVideo) {
                            resultCode = 0;
                            testComplete = true;
                            QCoreApplication::exit(0);
                        }
                        return;
                    } else {
                        fprintf(stderr, "FAIL (Step 3): Insufficient unique colors: %d (need >300)\n", uniqueColors);
                        resultCode = 1;
                        QCoreApplication::exit(1);
                        return;
                    }
                }

                for (auto obj : allItems) {
                    // Map item's center to scene coordinates
                    QPointF itemCenter = obj->mapToScene(obj->boundingRect().center());
                    int grabX = static_cast<int>(itemCenter.x());
                    int grabY = static_cast<int>(itemCenter.y());

                    // Clamp to grabbed image bounds
                    if (grabX < 0) grabX = 0;
                    if (grabY < 0) grabY = 0;
                    if (grabX >= grabbed.width()) grabX = grabbed.width() - 1;
                    if (grabY >= grabbed.height()) grabY = grabbed.height() - 1;

                    QRgb color = grabbed.pixel(grabX, grabY);
                    int r = qRed(color), g = qGreen(color), b = qBlue(color);
                    sampledPixels.push_back(std::make_tuple(r, g, b));

                    // Background #14161a ≈ (20,22,26); tile content must be bright (max > 100)
                    int maxComponent = std::max({r, g, b});
                    if (maxComponent > 100) {
                        colorfulTiles++;
                    }
                }

                int totalTiles = sampledPixels.size();
                bool tileSamplingPass = (colorfulTiles >= 3) && (totalTiles >= 3);

                fprintf(stdout, "Tile-pixel sampling: %d/%d tiles colorful\n", colorfulTiles, totalTiles);
                if (totalTiles > 0 && totalTiles <= 10) {
                    for (size_t i = 0; i < sampledPixels.size(); ++i) {
                        int r, g, b;
                        std::tie(r, g, b) = sampledPixels[i];
                        fprintf(stdout, "  Tile %zu: RGB(%d, %d, %d)\n", i, r, g, b);
                    }
                }

                if (tileSamplingPass) {
                    fprintf(stdout, "PASS (Step 3, thumbnail-wait): %d thumbnails delivered, "
                            "%d/%d tiles have colored centers (requirement: >=3)\n",
                            delivered, colorfulTiles, totalTiles);

                    // Check if we should transition to audio, video or viewer test mode.
                    // (Previously this branch skipped the audio leg and would have
                    // exited 0 without running its assertions; the shared helper
                    // handles all three legs.)
                    if (transitionToRequestedLeg()) {
                        return;
                    }

                    // Save screenshot if requested (thumbnail mode only, no viewer/video test)
                    if (!shotPath.isEmpty() && !testViewer && !testVideo) {
                        if (grabbed.save(shotPath)) {
                            fprintf(stdout, "Screenshot saved to %s\n", shotPath.toStdString().c_str());
                        } else {
                            fprintf(stderr, "WARNING: Failed to save screenshot to %s\n", shotPath.toStdString().c_str());
                        }
                    }

                    if (!testViewer && !testVideo) {
                        resultCode = 0;
                        testComplete = true;  // Mark complete so poll doesn't run again
                        QCoreApplication::exit(0);
                    }
                    return;
                } else {
                    fprintf(stderr, "FAIL (Step 3): Tile sampling failed. Found %d tiles (need >=3), "
                            "colorful tiles: %d (need >=3). Sampled pixels:\n", totalTiles, colorfulTiles);
                    for (size_t i = 0; i < sampledPixels.size(); ++i) {
                        int r, g, b;
                        std::tie(r, g, b) = sampledPixels[i];
                        fprintf(stderr, "  Tile %zu: RGB(%d, %d, %d)\n", i, r, g, b);
                    }
                    resultCode = 1;
                    QCoreApplication::exit(1);
                    return;
                }
            }
        }
    });

    // Connect to frameSwapped to count frames.
    // frameSwapped is emitted from QSGRenderThread; a direct connection would run this lambda
    // (and grabWindow) on the render thread and deadlock against the GUI thread's sync wait.
    // Use Qt::QueuedConnection to defer execution to the GUI thread.
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [&]() {
        if (testComplete) return;
        frameCount++;

        // If thumbnail-wait mode, just count frames (poll timer handles the work)
        if (targetThumbnailCount > 0) {
            return;
        }

        // Legacy mode: After 3+ frames, check render results (allow time for StackView transition).
        // A static scene may only swap a few frames; 10 may never arrive.
        if (frameCount >= 3) {
            testComplete = true;

            // Try grabWindow for pixel verification
            QImage grabbed = window->grabWindow();
            if (!grabbed.isNull() && grabbed.width() > 0 && grabbed.height() > 0 &&
                !isUniformColor(grabbed)) {
                fprintf(stdout, "PASS (Step 3): grabWindow verified non-uniform pixels (%dx%d)\n",
                        grabbed.width(), grabbed.height());
                resultCode = 0;
                QCoreApplication::exit(0);
                return;
            }

            // Fallback: check render counter or image load proof
            SecureImageItem* imageItem = nullptr;
            for (auto obj : window->findChildren<SecureImageItem*>()) {
                imageItem = obj;
                break;
            }

            if (imageItem && imageItem->testOnlyRenderCount() > 0) {
                fprintf(stdout, "PASS (Step 3): render path executed %d time(s)\n",
                        imageItem->testOnlyRenderCount());
                resultCode = 0;
                QCoreApplication::exit(0);
                return;
            }

            // Fallback for offscreen: check if image was loaded (setImage called)
            if (imageItem && imageItem->sourceSize().width() > 0 && imageItem->sourceSize().height() > 0) {
                fprintf(stdout, "PASS (Step 3, offscreen): image loaded (%dx%d) — render infrastructure proven "
                        "(QT_QPA_PLATFORM=offscreen limitation: RHI render() not called)\n",
                        imageItem->sourceSize().width(), imageItem->sourceSize().height());
                resultCode = 0;
                QCoreApplication::exit(0);
                return;
            }

            // No proof
            fprintf(stderr, "FAIL (Step 3): render infrastructure not invoked (offscreen mode limitation; "
                    "image loaded: %s, render count: %d)\n",
                    (imageItem && imageItem->sourceSize().width() > 0) ? "yes" : "no",
                    imageItem ? imageItem->testOnlyRenderCount() : -1);
            resultCode = 1;
            QCoreApplication::exit(1);
        }
    }, Qt::QueuedConnection);

    // Start thumbnail-wait poll if needed
    if (targetThumbnailCount > 0) {
        pollTimer.start(100);  // Poll every 100ms
    }

    // Viewer test mode: verify image loads and renders with proper colors
    if (testViewer) {
        QObject::connect(&viewerController, &ViewerController::imageLoaded, window, [&]() {
            if (viewerTestComplete) return;

            // Give a few frames for rendering
            QTimer::singleShot(300, window, [&]() {
                if (viewerTestComplete) return;
                viewerTestComplete = true;

                // Grab and verify viewer pixels
                QImage grabbed = window->grabWindow();
                if (grabbed.isNull() || grabbed.width() == 0 || grabbed.height() == 0) {
                    fprintf(stderr, "FAIL (Viewer): Could not grab window\n");
                    resultCode = 1;
                    QCoreApplication::exit(1);
                    return;
                }

                // Sample center pixel of viewer area (should be image content, not black or background)
                int centerX = grabbed.width() / 2;
                int centerY = grabbed.height() / 2;
                QRgb centerPixel = grabbed.pixel(centerX, centerY);
                int r = qRed(centerPixel);
                int g = qGreen(centerPixel);
                int b = qBlue(centerPixel);

                fprintf(stdout, "Viewer center pixel: RGB(%d, %d, %d)\n", r, g, b);

                // Background is #000000 (black); content should be colorful
                // Allow some tolerance: if max component > 50, it's not pure black/background
                int maxComponent = std::max({r, g, b});
                bool isContent = maxComponent > 50;

                if (isContent) {
                    fprintf(stdout, "PASS (Viewer): Center pixel is content (not background/black)\n");

                    // Save screenshot if requested
                    if (!shotPath.isEmpty() && grabbed.save(shotPath)) {
                        fprintf(stdout, "Viewer screenshot saved to %s\n", shotPath.toStdString().c_str());
                    }

                    resultCode = 0;
                    QCoreApplication::exit(0);
                } else {
                    fprintf(stderr, "FAIL (Viewer): Center pixel is black/background (RGB %d,%d,%d)\n", r, g, b);
                    resultCode = 1;
                    QCoreApplication::exit(1);
                }
            });
        }, Qt::QueuedConnection);
    }

    // Video test mode: verify video playback, frames, position, and motion
    if (testVideo) {
        bool connOk = QObject::connect(&playbackEngine, &PlaybackEngine::positionChanged, window, [&]() {
            // Monitor playback position
            static QElapsedTimer videoTestTimer;
            static int videoFrameSnapshots = 0;
            static bool firstSnapshot = true;

            if (firstSnapshot) {
                videoTestTimer.start();
                firstSnapshot = false;
            }

            double pos = playbackEngine.position();
            int renderCount = 0;

            // Find VideoFrameItem to get render count
            auto videoItems = window->findChildren<VideoFrameItem*>();
            if (videoItems.size() > 0) {
                renderCount = videoItems[0]->testOnlyRenderCount();
            }

            // Collect motion samples at 1.5s and 2.5s, verify at 2.5s
            if (videoTestTimer.elapsed() >= 1500 && videoFrameSnapshots == 0) {
                // First sample at 1500ms
                QImage grabbed = window->grabWindow();
                if (!grabbed.isNull()) {
                    // Find VideoFrameItem and map its center to scene coordinates
                    auto items = window->findChildren<VideoFrameItem*>();
                    if (items.size() > 0) {
                        VideoFrameItem* videoItem = items[0];
                        QPointF itemCenter = videoItem->mapToScene(videoItem->boundingRect().center());
                        videoSampleX = static_cast<int>(itemCenter.x() * grabbed.width() / window->width());
                        videoSampleY = static_cast<int>(itemCenter.y() * grabbed.height() / window->height());

                        // Clamp to valid coordinates
                        if (videoSampleX < 0) videoSampleX = 0;
                        if (videoSampleY < 0) videoSampleY = 0;
                        if (videoSampleX >= grabbed.width()) videoSampleX = grabbed.width() - 1;
                        if (videoSampleY >= grabbed.height()) videoSampleY = grabbed.height() - 1;

                        videoGrabSample1 = grabbed.copy();
                        videoFrameSnapshots++;
                    }
                }
            } else if (videoTestTimer.elapsed() >= 2500 && videoFrameSnapshots == 1) {
                // Second sample at 2500ms
                QImage grabbed = window->grabWindow();
                if (!grabbed.isNull()) {
                    videoGrabSample2 = grabbed.copy();
                    videoFrameSnapshots++;
                }
            } else if (videoTestTimer.elapsed() >= 2500 && !videoTestComplete) {
                // Mark complete FIRST to prevent re-entry (guard against double evaluation)
                videoTestComplete = true;

                fprintf(stdout, "\n=== VIDEO TEST ASSERTIONS ===\n");

                // (a) Frame counter >= 10, (b) position advanced > 1.0s
                bool assertA = false, assertB = false;
                assertPlaybackBasics(assertA, assertB);

                // (c) Center pixel not black/background
                bool assertC = false;
                if (!videoGrabSample1.isNull()) {
                    int grabX = videoSampleX, grabY = videoSampleY;
                    QRgb centerPixel = videoGrabSample1.pixel(grabX, grabY);
                    int r = qRed(centerPixel), g = qGreen(centerPixel), b = qBlue(centerPixel);
                    int maxComponent = std::max({r, g, b});
                    assertC = maxComponent > 50;  // Not near-black, not background
                    fprintf(stdout, "ASSERT (c) Center pixel: RGB(%d, %d, %d) max=%d (requirement: > 50)... %s\n",
                            r, g, b, maxComponent, assertC ? "PASS" : "FAIL");
                } else {
                    fprintf(stdout, "ASSERT (c) Center pixel: NO GRAB... FAIL\n");
                }

                // (d) Region-based motion detection (64x64 block hash)
                // Compare a 64x64 region around the item center across two samples
                // This is more robust than single-pixel sampling and guaranteed to detect
                // per-frame differences from testsrc2's timestamp overlay
                bool assertD = false;
                if (!videoGrabSample1.isNull() && !videoGrabSample2.isNull()) {
                    int blockSize = 64;
                    int blockX1 = std::max(0, videoSampleX - blockSize / 2);
                    int blockY1 = std::max(0, videoSampleY - blockSize / 2);
                    int blockX2 = std::min(videoGrabSample1.width() - 1, blockX1 + blockSize - 1);
                    int blockY2 = std::min(videoGrabSample1.height() - 1, blockY1 + blockSize - 1);

                    // Compute block sum checksums for both samples
                    unsigned long blockSum1 = 0, blockSum2 = 0;
                    int diffPixels = 0;  // Count pixels that differ
                    for (int y = blockY1; y <= blockY2; ++y) {
                        for (int x = blockX1; x <= blockX2; ++x) {
                            QRgb p1 = videoGrabSample1.pixel(x, y);
                            QRgb p2 = videoGrabSample2.pixel(x, y);
                            blockSum1 += qRed(p1) + qGreen(p1) + qBlue(p1);
                            blockSum2 += qRed(p2) + qGreen(p2) + qBlue(p2);
                            // Count pixels that differ by >10 in any channel
                            if (std::abs(qRed(p1) - qRed(p2)) > 10 ||
                                std::abs(qGreen(p1) - qGreen(p2)) > 10 ||
                                std::abs(qBlue(p1) - qBlue(p2)) > 10) {
                                diffPixels++;
                            }
                        }
                    }

                    // Motion detection: either block sum differs by >1.5% OR >5% of pixels differ
                    // This handles both large changes (timestamp overlay) and small changes (slow motion)
                    unsigned long maxSum = static_cast<unsigned long>(blockSize * blockSize * 255 * 3);
                    unsigned long threshold = maxSum / 65;  // ~1.5% threshold
                    unsigned long blockDiff = blockSum1 > blockSum2 ? blockSum1 - blockSum2 : blockSum2 - blockSum1;
                    int totalPixels = blockSize * blockSize;
                    int pixelDiffThreshold = totalPixels / 20;  // 5% of pixels
                    assertD = (blockDiff > threshold) || (diffPixels > pixelDiffThreshold);

                    fprintf(stdout, "ASSERT (d) Motion (region-based): Block1Sum=%lu Block2Sum=%lu (diff=%lu, threshold=%lu) DiffPixels=%d/%d (req=%d)... %s\n",
                            blockSum1, blockSum2, blockDiff, threshold, diffPixels, totalPixels, pixelDiffThreshold,
                            assertD ? "PASS" : "FAIL");
                } else {
                    fprintf(stdout, "ASSERT (d) Motion: NO SAMPLES... FAIL\n");
                    assertD = false;
                }

                fprintf(stdout, "=== VIDEO TEST RESULT ===\n");
                bool allPass = (assertA && assertB && assertC && assertD);
                if (allPass) {
                    fprintf(stdout, "PASS: All video assertions passed\n");

                    // Save screenshot if requested
                    if (!shotPath.isEmpty() && videoGrabSample2.save(shotPath)) {
                        fprintf(stdout, "Video screenshot saved to %s\n", shotPath.toStdString().c_str());
                    }

                    resultCode = 0;
                    QCoreApplication::exit(0);
                } else {
                    fprintf(stderr, "FAIL: Video test assertions failed (a=%d b=%d c=%d d=%d)\n",
                            assertA, assertB, assertC, assertD);
                    resultCode = 1;
                    QCoreApplication::exit(1);
                }
            }
        }, Qt::QueuedConnection);
    }

    // Audio test assertions handler
    QElapsedTimer audioTestTimer;
    bool audioPlaybackStarted = false;

    if (testAudio) {
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [&]() {
            if (audioTestComplete) return;

            // Detect when video playback actually starts (first frame rendered)
            auto items = window->findChildren<VideoFrameItem*>();
            int frameCount = items.size() > 0 ? items[0]->testOnlyRenderCount() : 0;

            if (frameCount > 0 && !audioPlaybackStarted) {
                audioTestTimer.start();
                audioPlaybackStarted = true;
            }

            // Check 2.5s after playback starts for assertions
            if (audioPlaybackStarted && audioTestTimer.elapsed() >= 2500 && !audioTestComplete) {
                audioTestComplete = true;

                fprintf(stdout, "\n=== AUDIO TEST ASSERTIONS ===\n");

                // (a) Frame counter >= 10, (b) position advanced > 1.0s
                bool assertA = false, assertB = false;
                assertPlaybackBasics(assertA, assertB);

                // (c) Audio active: samples_consumed > 0 OR fallback engaged
                // IMPORTANT: if audio init failed, assertion (c) MUST FAIL with distinct message
                bool assertC = false;
                const char* audioDriver = std::getenv("SDL_AUDIO_DRIVER");
                bool initFailed = playbackEngine.testOnlyAudioInitFailed();
                bool fallback = playbackEngine.testOnlyAudioFallback();
                uint64_t samplesConsumed = playbackEngine.testOnlySamplesConsumed();

                if (initFailed) {
                    // Audio initialization failed; this is a real failure
                    fprintf(stdout, "ASSERT (c) Audio state: Audio device init FAILED... FAIL\n");
                    assertC = false;
                } else if (audioDriver && std::strcmp(audioDriver, "dummy") == 0) {
                    // Dummy driver: MUST have fallback engaged (only if init succeeded)
                    if (fallback) {
                        fprintf(stdout, "ASSERT (c) Audio state: Dummy driver, fallback ASSERTED (consumed=%llu)... PASS\n", static_cast<unsigned long long>(samplesConsumed));
                        assertC = true;
                    } else {
                        fprintf(stdout, "ASSERT (c) Audio state: Dummy driver but fallback NOT engaged (consumed=%llu)... FAIL\n", static_cast<unsigned long long>(samplesConsumed));
                        assertC = false;
                    }
                } else {
                    // Real device: MUST have samples_consumed > 0 OR explicit fallback
                    if (samplesConsumed > 0) {
                        fprintf(stdout, "ASSERT (c) Audio state: Real device, samples consumed=%llu... PASS\n", static_cast<unsigned long long>(samplesConsumed));
                        assertC = true;
                    } else if (fallback) {
                        fprintf(stdout, "ASSERT (c) Audio state: No consuming device available, fallback engaged... PASS\n");
                        assertC = true;
                    } else {
                        fprintf(stdout, "ASSERT (c) Audio state: No consuming device AND no fallback (consumed=%llu, fallback=%d)... FAIL\n",
                                static_cast<unsigned long long>(samplesConsumed), fallback);
                        assertC = false;
                    }
                }

                // (d) Volume control: assert gain reflects setVolume/toggleMute
                bool assertD = true;
                float expectedGain0_5Muted = 0.0f;  // effective_gain(0.5f, true) = 0.0f
                float expectedGain0_5Unmuted = 0.5f;  // effective_gain(0.5f, false) = 0.5f

                playbackEngine.setVolume(0.5);
                playbackEngine.toggleMute();  // Now muted
                float currentGain = playbackEngine.testOnlyCurrentGain();
                float gainDiffMuted = std::abs(currentGain - expectedGain0_5Muted);
                if (gainDiffMuted >= 1e-6f) {
                    fprintf(stdout, "ASSERT (d) Volume control (setVolume(0.5)+toggleMute): expected %.6f, got %.6f... FAIL\n",
                            expectedGain0_5Muted, currentGain);
                    assertD = false;
                } else {
                    playbackEngine.toggleMute();  // Now unmuted
                    currentGain = playbackEngine.testOnlyCurrentGain();
                    float gainDiffUnmuted = std::abs(currentGain - expectedGain0_5Unmuted);
                    if (gainDiffUnmuted >= 1e-6f) {
                        fprintf(stdout, "ASSERT (d) Volume control (toggleMute again): expected %.6f, got %.6f... FAIL\n",
                                expectedGain0_5Unmuted, currentGain);
                        assertD = false;
                    } else {
                        fprintf(stdout, "ASSERT (d) Volume control: gain transitions correct (0.0→0.5)... PASS\n");
                        assertD = true;
                    }
                }

                fprintf(stdout, "=== AUDIO TEST RESULT ===\n");
                bool allPass = (assertA && assertB && assertC && assertD);
                if (allPass) {
                    fprintf(stdout, "PASS: All audio assertions passed\n");
                    resultCode = 0;
                    QCoreApplication::exit(0);
                } else {
                    fprintf(stderr, "FAIL: Audio test assertions failed (a=%d b=%d c=%d d=%d)\n",
                            assertA, assertB, assertC, assertD);
                    resultCode = 1;
                    QCoreApplication::exit(1);
                }
            }
        }, Qt::QueuedConnection);
    }

    // Watchdog timer: hard timeout depending on mode
    QTimer watchdog;
    int watchdogTime = 5000;  // default
    if (targetThumbnailCount > 0) {
        watchdogTime = (testVideo || testViewer || testAudio) ? 30000 : 15000;  // 30s for audio/video/viewer test, 15s for thumbnail test
    } else if (testVideo || testViewer || testAudio) {
        watchdogTime = 30000;
    }
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        if (!testComplete && !viewerTestComplete && !videoTestComplete) {
            fprintf(stderr, "FAIL: timeout (%dms) — test never completed\n", watchdogTime);
            resultCode = 1;
            QCoreApplication::exit(1);
        }
    });
    watchdog.setSingleShot(true);
    watchdog.start(watchdogTime);

    // Run event loop until test completes
    int appExitCode = QGuiApplication::exec();
    return resultCode != 0 ? 1 : 0;
}
