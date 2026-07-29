#pragma once

#include <QQuickRhiItem>
#include <memory>

#include "pixel_buffer.h"

// QRhi-based image renderer: uploads decrypted PixelBuffer into a texture,
// renders a textured quad. QML never sees pixel data.
class SecureImageItem : public QQuickRhiItem {
    Q_OBJECT
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)

public:
    QQuickRhiItemRenderer* createRenderer() override;

    // GUI thread: store the buffer and signal render-thread upload
    void setImage(std::shared_ptr<const PixelBuffer> px);

    [[nodiscard]] QSize sourceSize() const;

signals:
    void sourceSizeChanged();

private:
    friend class SecureImageRenderer;
    std::shared_ptr<const PixelBuffer> pending_;  // read in synchronize()
    QSize sourceSize_;
};
