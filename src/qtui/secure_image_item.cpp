#include "secure_image_item.h"
#include "rhi_quad_renderer.h"
#include "thumb_cache.h"

#include <rhi/qrhi.h>
#include <QtCore/qloggingcategory.h>

Q_LOGGING_CATEGORY(lcSecureImage, "osv.secure_image")

class SecureImageRenderer : public TexQuadRendererBase {
public:
    SecureImageRenderer() = default;

protected:
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;
    QVector<QRhiShaderResourceBinding> textureBindings() override;
    const char* fragmentShaderResource() const override
    {
        return ":/osvqt/shaders/texquad.frag.qsb";
    }

private:
    SecureImageItem* item_ = nullptr;  // stored in synchronize()
    std::shared_ptr<const PixelBuffer> toUpload_;
    std::unique_ptr<QRhiTexture> tex_;
};

QVector<QRhiShaderResourceBinding> SecureImageRenderer::textureBindings()
{
    QVector<QRhiShaderResourceBinding> bindings;
    if (tex_) {
        bindings << QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, tex_.get(), sampler_.get());
    }
    return bindings;
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

    finishRender(cb, batch, static_cast<bool>(tex_));
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

    // CRITICAL: After request() and connect(), re-check cache to avoid race condition.
    // The worker thread may have decoded and stored the thumbnail between our
    // pixels(key) check and the connect() call above. If so, the ready(key) signal
    // has already been emitted and our just-made connection will never receive it.
    // Re-checking after connect() ensures we don't miss a fast decode.
    auto pixelsAgain = cache->pixels(key);
    if (pixelsAgain) {
        setImage(pixelsAgain);
    }
}

void SecureImageItem::onThumbReady(quintptr key)
{
    qDebug() << "SecureImageItem::onThumbReady" << "key=" << key << "nodeKey_=" << nodeKey_ << "this=" << this;

    // Guard: only accept if this is for our current key
    if (key != nodeKey_) {
        qDebug() << "  -> stale key, ignoring";
        return;
    }

    auto cache = ThumbCache::instance();
    if (!cache) {
        qWarning() << "  -> cache is null!";
        return;
    }

    auto pixels = cache->pixels(key);
    if (pixels) {
        qDebug() << "  -> got pixels" << pixels->width << "x" << pixels->height << ", calling setImage";
        setImage(pixels);
    } else {
        qWarning() << "  -> pixels() returned null!";
    }
}
