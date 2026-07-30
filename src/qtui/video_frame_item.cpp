#include "video_frame_item.h"
#include "rhi_quad_renderer.h"

#include <rhi/qrhi.h>
#include <QtCore/qloggingcategory.h>

Q_LOGGING_CATEGORY(lcVideoFrame, "osv.video_frame")

class VideoFrameRenderer : public TexQuadRendererBase {
public:
    VideoFrameRenderer() = default;

protected:
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;
    QVector<QRhiShaderResourceBinding> textureBindings() override;
    const char* fragmentShaderResource() const override
    {
        return ":/osvqt/shaders/yuv.frag.qsb";
    }

private:
    VideoFrameItem* item_ = nullptr;
    std::shared_ptr<const FrameBox> toUpload_;
    QSize frameSize_;
    media::FramePixelFormat frameFmt_ = media::FramePixelFormat::I420;

    // Three R8 textures: Y (full), U (1/4), V (1/4) for I420
    std::unique_ptr<QRhiTexture> texY_;
    std::unique_ptr<QRhiTexture> texU_;
    std::unique_ptr<QRhiTexture> texV_;
};

QVector<QRhiShaderResourceBinding> VideoFrameRenderer::textureBindings()
{
    QRhiShaderResourceBinding::StageFlags fsStage = QRhiShaderResourceBinding::FragmentStage;

    QVector<QRhiShaderResourceBinding> bindings;
    if (texY_) {
        bindings << QRhiShaderResourceBinding::sampledTexture(1, fsStage, texY_.get(), sampler_.get());
    }
    if (texU_) {
        bindings << QRhiShaderResourceBinding::sampledTexture(2, fsStage, texU_.get(), sampler_.get());
    }
    if (texV_) {
        bindings << QRhiShaderResourceBinding::sampledTexture(3, fsStage, texV_.get(), sampler_.get());
    }
    return bindings;
}

void VideoFrameRenderer::synchronize(QQuickRhiItem* item)
{
    auto* video_item = static_cast<VideoFrameItem*>(item);
    item_ = video_item;
    // Always consume pending frame if present (not just when it changes)
    if (video_item->pending_) {
        toUpload_ = video_item->pending_;
        video_item->pending_.reset();
    }
}

void VideoFrameRenderer::render(QRhiCommandBuffer* cb)
{
    QRhi* rhi = this->rhi();
    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();

    // Test-only: increment render counter
    if (item_) {
        item_->testOnlyIncrementRenderCount();
    }

    // Upload new textures if pending
    if (toUpload_) {
        const auto& frame = toUpload_->meta;
        const auto& storage = toUpload_->storage;

        // Only support I420 for now (as per brief)
        if (frame.pix_fmt != media::FramePixelFormat::I420) {
            qCWarning(lcVideoFrame) << "Only I420 format supported, got:" << static_cast<int>(frame.pix_fmt);
            toUpload_.reset();
        } else {
            // Compute texture dimensions
            int y_width = frame.width;
            int y_height = frame.height;
            int u_width = (frame.width + 1) / 2;
            int u_height = (frame.height + 1) / 2;

            // Recreate textures if size changed
            if (!texY_ || texY_->pixelSize() != QSize(y_width, y_height)) {
                if (texY_) {
                    texY_->destroy();
                }
                texY_.reset(rhi->newTexture(QRhiTexture::R8, QSize(y_width, y_height)));
                texY_->create();
                resourcesDirty_ = true;
            }

            if (!texU_ || texU_->pixelSize() != QSize(u_width, u_height)) {
                if (texU_) {
                    texU_->destroy();
                }
                texU_.reset(rhi->newTexture(QRhiTexture::R8, QSize(u_width, u_height)));
                texU_->create();
                resourcesDirty_ = true;
            }

            if (!texV_ || texV_->pixelSize() != QSize(u_width, u_height)) {
                if (texV_) {
                    texV_->destroy();
                }
                texV_.reset(rhi->newTexture(QRhiTexture::R8, QSize(u_width, u_height)));
                texV_->create();
                resourcesDirty_ = true;
            }

            // Upload Y plane (full resolution)
            QRhiTextureSubresourceUploadDescription y_sub(frame.planes[0],
                                                          static_cast<quint32>(y_height * frame.linesizes[0]));
            y_sub.setDataStride(frame.linesizes[0]);
            batch->uploadTexture(texY_.get(), QRhiTextureUploadDescription({0, 0, y_sub}));

            // Upload U plane (quarter resolution)
            QRhiTextureSubresourceUploadDescription u_sub(frame.planes[1],
                                                          static_cast<quint32>(u_height * frame.linesizes[1]));
            u_sub.setDataStride(frame.linesizes[1]);
            batch->uploadTexture(texU_.get(), QRhiTextureUploadDescription({0, 0, u_sub}));

            // Upload V plane (quarter resolution)
            QRhiTextureSubresourceUploadDescription v_sub(frame.planes[2],
                                                          static_cast<quint32>(u_height * frame.linesizes[2]));
            v_sub.setDataStride(frame.linesizes[2]);
            batch->uploadTexture(texV_.get(), QRhiTextureUploadDescription({0, 0, v_sub}));

            frameSize_ = QSize(frame.width, frame.height);
            toUpload_.reset();
        }
    }

    finishRender(cb, batch, static_cast<bool>(texY_));
}

//==============================================================================
// VideoFrameItem implementation
//==============================================================================

QQuickRhiItemRenderer* VideoFrameItem::createRenderer()
{
    return new VideoFrameRenderer();
}

void VideoFrameItem::setFrame(std::shared_ptr<const FrameBox> frame)
{
    if (frame) {
        if (sourceSize_.width() != frame->meta.width || sourceSize_.height() != frame->meta.height) {
            sourceSize_ = QSize(frame->meta.width, frame->meta.height);
            emit sourceSizeChanged();
        }
        if (std::abs(pts_ - frame->meta.pts_seconds) > 1e-9) {
            pts_ = frame->meta.pts_seconds;
            emit ptsChanged();
        }
    }
    pending_ = frame;
    // Increment frameCounter to force Qt Quick to detect a property change and re-render
    ++frameCounter_;
    emit frameCounterChanged();
    update();
}

QSize VideoFrameItem::sourceSize() const
{
    return sourceSize_;
}
