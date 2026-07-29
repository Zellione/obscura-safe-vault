#pragma once

#include <QQuickRhiItem>
#include <memory>

#include "pixel_buffer.h"

class SecureImageRenderer;  // forward declare

// QRhi-based image renderer: uploads decrypted PixelBuffer into a texture,
// renders a textured quad. QML never sees pixel data.
class SecureImageItem : public QQuickRhiItem {
    Q_OBJECT
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)
    Q_PROPERTY(quintptr nodeKey READ nodeKey WRITE setNodeKey NOTIFY nodeKeyChanged)

public:
    QQuickRhiItemRenderer* createRenderer() override;

    // GUI thread: store the buffer and signal render-thread upload
    void setImage(std::shared_ptr<const PixelBuffer> px);

    [[nodiscard]] QSize sourceSize() const;

    // Node key property: on set, request thumbnail from cache or connect to ready signal
    [[nodiscard]] quintptr nodeKey() const { return nodeKey_; }
    void setNodeKey(quintptr key);

    // Test-only: how many times has render() been called (proof of render path execution)
    [[nodiscard]] int testOnlyRenderCount() const { return renderCount_; }
    void testOnlyIncrementRenderCount() { ++renderCount_; }

signals:
    void sourceSizeChanged();
    void nodeKeyChanged();

private:
    friend class SecureImageRenderer;
    void onThumbReady(quintptr key);  // slot: called when cache signals ready(key)

    std::shared_ptr<const PixelBuffer> pending_;  // read in synchronize()
    QSize sourceSize_;
    int renderCount_ = 0;  // test-only counter
    quintptr nodeKey_ = 0;  // opaque node pointer
};
