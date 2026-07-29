#include "video_frame_item.h"

#include <QFile>
#include <QMatrix4x4>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>
#include <QtQuick/qquickrhiitem.h>
#include <QtCore/qloggingcategory.h>

Q_LOGGING_CATEGORY(lcVideoFrame, "osv.video_frame")

class VideoFrameRenderer : public QQuickRhiItemRenderer {
public:
    VideoFrameRenderer() = default;

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    void rebuildResources();
    void ensurePipeline();

    VideoFrameItem* item_ = nullptr;
    std::shared_ptr<const FrameBox> toUpload_;
    QSize frameSize_;
    media::FramePixelFormat frameFmt_ = media::FramePixelFormat::I420;

    // Three R8 textures: Y (full), U (1/4), V (1/4) for I420
    std::unique_ptr<QRhiTexture> texY_;
    std::unique_ptr<QRhiTexture> texU_;
    std::unique_ptr<QRhiTexture> texV_;

    std::unique_ptr<QRhiSampler> sampler_;
    std::unique_ptr<QRhiBuffer> vbuf_;
    std::unique_ptr<QRhiBuffer> ubuf_;
    std::unique_ptr<QRhiShaderResourceBindings> srb_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    bool resourcesDirty_ = true;
};

void VideoFrameRenderer::initialize(QRhiCommandBuffer* cb)
{
    QRhi* rhi = this->rhi();

    // Create vertex buffer: textured quad as 2 triangles (6 vertices, normalized coords)
    struct Vertex {
        float pos[4];      // x, y, 0, 1 (normalized 0-1 coords)
        float uv[2];       // u, v
    };

    const Vertex vertices[] = {
        // Triangle 1 (top-left)
        {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},  // top-left
        {{1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},  // top-right
        {{0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},  // bottom-left
        // Triangle 2 (bottom-right)
        {{1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},  // top-right
        {{1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},  // bottom-right
        {{0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},  // bottom-left
    };

    vbuf_.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(vertices)));
    if (!vbuf_->create()) {
        qCWarning(lcVideoFrame) << "Failed to create vertex buffer";
        return;
    }

    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
    batch->uploadStaticBuffer(vbuf_.get(), vertices);
    cb->resourceUpdate(batch);

    // Create uniform buffer for MVP matrix (64 bytes = 4x4 matrix)
    ubuf_.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    if (!ubuf_->create()) {
        qCWarning(lcVideoFrame) << "Failed to create uniform buffer";
        return;
    }

    // Create sampler for all three YUV textures
    sampler_.reset(rhi->newSampler(
        QRhiSampler::Linear,  // magFilter
        QRhiSampler::Linear,  // minFilter
        QRhiSampler::None,    // mipmapMode
        QRhiSampler::ClampToEdge,
        QRhiSampler::ClampToEdge));
    if (!sampler_->create()) {
        qCWarning(lcVideoFrame) << "Failed to create sampler";
        return;
    }

    resourcesDirty_ = true;
}

void VideoFrameRenderer::rebuildResources()
{
    if (!sampler_ || !vbuf_ || !ubuf_) return;

    QRhi* rhi = this->rhi();

    // Create shader resource bindings: MVP uniform + three texture samplers
    QRhiShaderResourceBinding::StageFlags vsStage = QRhiShaderResourceBinding::VertexStage;
    QRhiShaderResourceBinding::StageFlags fsStage = QRhiShaderResourceBinding::FragmentStage;

    QVector<QRhiShaderResourceBinding> bindings;
    bindings << QRhiShaderResourceBinding::uniformBuffer(0, vsStage, ubuf_.get());

    if (texY_) {
        bindings << QRhiShaderResourceBinding::sampledTexture(1, fsStage, texY_.get(), sampler_.get());
    }
    if (texU_) {
        bindings << QRhiShaderResourceBinding::sampledTexture(2, fsStage, texU_.get(), sampler_.get());
    }
    if (texV_) {
        bindings << QRhiShaderResourceBinding::sampledTexture(3, fsStage, texV_.get(), sampler_.get());
    }

    srb_.reset(rhi->newShaderResourceBindings());
    srb_->setBindings(bindings.begin(), bindings.end());
    if (!srb_->create()) {
        qCWarning(lcVideoFrame) << "Failed to create shader resource bindings";
        return;
    }

    resourcesDirty_ = false;
}

void VideoFrameRenderer::ensurePipeline()
{
    if (pipeline_) return;

    QRhi* rhi = this->rhi();

    // Load vertex shader (reuse texquad.vert)
    QFile vs_file(QStringLiteral(":/osvqt/shaders/texquad.vert.qsb"));
    if (!vs_file.open(QIODevice::ReadOnly)) {
        qCWarning(lcVideoFrame) << "Failed to load vertex shader";
        return;
    }
    QShader vs = QShader::fromSerialized(vs_file.readAll());
    vs_file.close();

    // Load YUV fragment shader
    QFile fs_file(QStringLiteral(":/osvqt/shaders/yuv.frag.qsb"));
    if (!fs_file.open(QIODevice::ReadOnly)) {
        qCWarning(lcVideoFrame) << "Failed to load YUV fragment shader";
        return;
    }
    QShader fs = QShader::fromSerialized(fs_file.readAll());
    fs_file.close();

    if (!vs.isValid() || !fs.isValid()) {
        qCWarning(lcVideoFrame) << "Invalid shaders loaded";
        return;
    }

    // Create graphics pipeline
    pipeline_.reset(rhi->newGraphicsPipeline());

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    pipeline_->setTargetBlends({blend});

    pipeline_->setShaderStages({
        QRhiShaderStage(QRhiShaderStage::Vertex, vs),
        QRhiShaderStage(QRhiShaderStage::Fragment, fs)
    });

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({
        {6 * sizeof(float)}  // stride: 4 floats pos + 2 floats uv
    });
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float4, 0},                  // position
        {0, 1, QRhiVertexInputAttribute::Float2, 4 * sizeof(float)}   // texcoord
    });
    pipeline_->setVertexInputLayout(inputLayout);

    pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!srb_) {
        rebuildResources();
    }
    pipeline_->setShaderResourceBindings(srb_.get());

    if (!pipeline_->create()) {
        qCWarning(lcVideoFrame) << "Failed to create graphics pipeline";
        pipeline_.reset();
        return;
    }
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

    // Rebuild SRB if textures changed
    if (resourcesDirty_) {
        rebuildResources();
    }

    // Update MVP matrix (ortho 0-1, same as SecureImageItem)
    if (ubuf_) {
        QMatrix4x4 mvp;
        mvp.ortho(0.0f, 1.0f,  // left, right
                  1.0f, 0.0f,  // top, bottom (flipped)
                  -1.0f, 1.0f);

        batch->updateDynamicBuffer(ubuf_.get(), 0, 64, mvp.constData());
    }

    ensurePipeline();

    const QColor clear(0, 0, 0, 0);
    cb->resourceUpdate(batch);
    cb->beginPass(renderTarget(), clear, {1.0f, 0});

    if (texY_ && pipeline_) {
        cb->setGraphicsPipeline(pipeline_.get());
        QSize sz = renderTarget()->pixelSize();
        cb->setViewport({0.0f, 0.0f, static_cast<float>(sz.width()),
                         static_cast<float>(sz.height())});
        cb->setShaderResources(srb_.get());
        const QRhiCommandBuffer::VertexInput vin(vbuf_.get(), 0);
        cb->setVertexInput(0, 1, &vin);
        cb->draw(6);  // 6 vertices = 2 triangles
    }

    cb->endPass();
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
