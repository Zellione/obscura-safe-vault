#include "secure_image_item.h"
#include "thumb_cache.h"

#include <QFile>
#include <QMatrix4x4>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>
#include <QtQuick/qquickrhiitem.h>
#include <QtCore/qloggingcategory.h>

Q_LOGGING_CATEGORY(lcSecureImage, "osv.secure_image")

class SecureImageRenderer : public QQuickRhiItemRenderer {
public:
    SecureImageRenderer() = default;

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    void rebuildResources();
    void ensurePipeline();

    SecureImageItem* item_ = nullptr;  // stored in synchronize()
    std::shared_ptr<const PixelBuffer> toUpload_;
    std::unique_ptr<QRhiTexture> tex_;
    std::unique_ptr<QRhiSampler> sampler_;
    std::unique_ptr<QRhiBuffer> vbuf_;
    std::unique_ptr<QRhiBuffer> ubuf_;
    std::unique_ptr<QRhiShaderResourceBindings> srb_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    bool resourcesDirty_ = true;
};

void SecureImageRenderer::initialize(QRhiCommandBuffer* cb)
{
    QRhi* rhi = this->rhi();

    // Create vertex buffer: textured quad
    struct Vertex {
        float pos[4];      // x, y, 0, 1
        float uv[2];       // u, v
    };

    const Vertex vertices[] = {
        {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},  // top-left
        {{1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},  // top-right
        {{1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},  // bottom-right
        {{0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},  // bottom-left
    };

    vbuf_.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(vertices)));
    if (!vbuf_->create()) {
        qCWarning(lcSecureImage) << "Failed to create vertex buffer";
        return;
    }

    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
    batch->uploadStaticBuffer(vbuf_.get(), vertices);
    cb->resourceUpdate(batch);

    // Create uniform buffer for MVP matrix (64 bytes = 4x4 matrix)
    ubuf_.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    if (!ubuf_->create()) {
        qCWarning(lcSecureImage) << "Failed to create uniform buffer";
        return;
    }

    // Create sampler
    sampler_.reset(rhi->newSampler(
        QRhiSampler::Linear,  // magFilter
        QRhiSampler::Linear,  // minFilter
        QRhiSampler::None,    // mipmapMode
        QRhiSampler::ClampToEdge,
        QRhiSampler::ClampToEdge));
    if (!sampler_->create()) {
        qCWarning(lcSecureImage) << "Failed to create sampler";
        return;
    }

    resourcesDirty_ = true;
}

void SecureImageRenderer::rebuildResources()
{
    if (!sampler_ || !vbuf_ || !ubuf_) return;

    QRhi* rhi = this->rhi();

    // Create shader resource bindings
    QRhiShaderResourceBinding::StageFlags vsStage = QRhiShaderResourceBinding::VertexStage;
    QRhiShaderResourceBinding::StageFlags fsStage = QRhiShaderResourceBinding::FragmentStage;

    QVector<QRhiShaderResourceBinding> bindings;
    bindings << QRhiShaderResourceBinding::uniformBuffer(0, vsStage, ubuf_.get());
    if (tex_) {
        bindings << QRhiShaderResourceBinding::sampledTexture(1, fsStage, tex_.get(), sampler_.get());
    }

    srb_.reset(rhi->newShaderResourceBindings());
    srb_->setBindings(bindings.begin(), bindings.end());
    if (!srb_->create()) {
        qCWarning(lcSecureImage) << "Failed to create shader resource bindings";
        return;
    }

    resourcesDirty_ = false;
}

void SecureImageRenderer::ensurePipeline()
{
    if (pipeline_) return;

    QRhi* rhi = this->rhi();

    // Load shaders from resources
    QFile vs_file(QStringLiteral(":/osvqt/shaders/texquad.vert.qsb"));
    if (!vs_file.open(QIODevice::ReadOnly)) {
        qCWarning(lcSecureImage) << "Failed to load vertex shader";
        return;
    }
    QShader vs = QShader::fromSerialized(vs_file.readAll());
    vs_file.close();

    QFile fs_file(QStringLiteral(":/osvqt/shaders/texquad.frag.qsb"));
    if (!fs_file.open(QIODevice::ReadOnly)) {
        qCWarning(lcSecureImage) << "Failed to load fragment shader";
        return;
    }
    QShader fs = QShader::fromSerialized(fs_file.readAll());
    fs_file.close();

    if (!vs.isValid() || !fs.isValid()) {
        qCWarning(lcSecureImage) << "Invalid shaders loaded";
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
        qCWarning(lcSecureImage) << "Failed to create graphics pipeline";
        pipeline_.reset();
        return;
    }
}

void SecureImageRenderer::synchronize(QQuickRhiItem* item)
{
    auto* image_item = static_cast<SecureImageItem*>(item);
    item_ = image_item;  // store for render() use
    toUpload_ = image_item->pending_;
    image_item->pending_.reset();
}

void SecureImageRenderer::render(QRhiCommandBuffer* cb)
{
    QRhi* rhi = this->rhi();
    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();

    // Test-only: increment render counter (proof that this render path executed)
    if (item_) {
        item_->testOnlyIncrementRenderCount();
    }

    // Upload new texture if pending
    if (toUpload_) {
        const auto& px = *toUpload_;
        if (!tex_ || tex_->pixelSize() != QSize(px.width, px.height)) {
            // Destroy old texture per documented QRhiResource lifecycle.
            // QRhiResource::destroy() (qrhi.h:835) is the safe pattern to release GPU resources
            // before heap deallocation. The destructor alone does not release GPU state.
            if (tex_) {
                tex_->destroy();
            }
            tex_.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(px.width, px.height)));
            tex_->create();
            resourcesDirty_ = true;
        }

        QRhiTextureSubresourceUploadDescription sub(px.rgba.data(),
                                                    static_cast<quint32>(px.rgba.size()));
        batch->uploadTexture(tex_.get(), QRhiTextureUploadDescription({0, 0, sub}));
        toUpload_.reset();
    }

    // Rebuild SRB if texture changed
    if (resourcesDirty_) {
        rebuildResources();
    }

    // Update MVP matrix
    if (ubuf_) {
        QSize sz = renderTarget()->pixelSize();
        QMatrix4x4 mvp;
        mvp.ortho(0.0f, static_cast<float>(sz.width()),
                  static_cast<float>(sz.height()), 0.0f,
                  -1.0f, 1.0f);

        batch->updateDynamicBuffer(ubuf_.get(), 0, 64, mvp.constData());
    }

    ensurePipeline();

    const QColor clear(0, 0, 0, 0);
    cb->resourceUpdate(batch);
    cb->beginPass(renderTarget(), clear, {1.0f, 0});

    if (tex_ && pipeline_) {
        cb->setGraphicsPipeline(pipeline_.get());
        QSize sz = renderTarget()->pixelSize();
        cb->setViewport({0.0f, 0.0f, static_cast<float>(sz.width()),
                         static_cast<float>(sz.height())});
        cb->setShaderResources(srb_.get());
        const QRhiCommandBuffer::VertexInput vin(vbuf_.get(), 0);
        cb->setVertexInput(0, 1, &vin);
        cb->draw(4);
    }

    cb->endPass();
}

//==============================================================================
// SecureImageItem implementation
//==============================================================================

QQuickRhiItemRenderer* SecureImageItem::createRenderer()
{
    return new SecureImageRenderer();
}

void SecureImageItem::setImage(std::shared_ptr<const PixelBuffer> px)
{
    if (px && (sourceSize_.width() != px->width || sourceSize_.height() != px->height)) {
        sourceSize_ = QSize(px->width, px->height);
        emit sourceSizeChanged();
    }
    pending_ = px;
    update();
}

QSize SecureImageItem::sourceSize() const
{
    return sourceSize_;
}

void SecureImageItem::setNodeKey(quintptr key)
{
    if (nodeKey_ == key)
        return;

    nodeKey_ = key;
    qDebug() << "SecureImageItem::setNodeKey" << key;
    emit nodeKeyChanged();

    // Try to get cached pixels now
    auto cache = ThumbCache::instance();
    if (!cache) {
        qWarning() << "SecureImageItem::setNodeKey: ThumbCache::instance() is null!";
        return;
    }

    auto pixels = cache->pixels(key);
    if (pixels) {
        // Already in cache, load it
        setImage(pixels);
        return;
    }

    // Not in cache yet, request decode and connect to ready signal.
    // Qt::UniqueConnection prevents multiple identical connections from this item.
    connect(cache, &ThumbCache::ready, this, &SecureImageItem::onThumbReady, Qt::UniqueConnection);

    cache->request(key);
}

void SecureImageItem::onThumbReady(quintptr key)
{
    // Guard: only accept if this is for our current key
    if (key != nodeKey_)
        return;

    auto cache = ThumbCache::instance();
    if (!cache)
        return;

    auto pixels = cache->pixels(key);
    if (pixels) {
        setImage(pixels);
    }
}
