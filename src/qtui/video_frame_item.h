#pragma once

#include <QQuickRhiItem>
#include <memory>
#include <vector>
#include <array>
#include <cstdint>

#include "media/decoded_frame.h"

class VideoFrameRenderer;  // forward declare

// FrameBox: decoded video frame with owned storage.
// planes are re-pointed into storage after move.
struct FrameBox {
    media::DecodedFrame meta;        // width, height, pix_fmt, pts_seconds, linesizes
    std::vector<uint8_t> storage;    // owns the pixel data
};

// QRhi-based YUV video frame renderer: uploads I420 planes into three R8 textures,
// renders with yuv.frag shader. QML never sees pixel data.
class VideoFrameItem : public QQuickRhiItem {
    Q_OBJECT
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)
    Q_PROPERTY(double pts READ pts NOTIFY ptsChanged)

public:
    QQuickRhiItemRenderer* createRenderer() override;

    // GUI thread: store the frame and signal render-thread upload
    void setFrame(std::shared_ptr<const FrameBox> frame);

    [[nodiscard]] QSize sourceSize() const;
    [[nodiscard]] double pts() const { return pts_; }

    // Test-only: how many times has render() been called
    [[nodiscard]] int testOnlyRenderCount() const { return renderCount_; }
    void testOnlyIncrementRenderCount() { ++renderCount_; }

signals:
    void sourceSizeChanged();
    void ptsChanged();

private:
    friend class VideoFrameRenderer;

    std::shared_ptr<const FrameBox> pending_;  // read in synchronize()
    QSize sourceSize_;
    double pts_ = 0.0;
    int renderCount_ = 0;  // test-only counter
};
