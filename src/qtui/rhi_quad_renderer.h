#pragma once

#include <QtQuick/qquickrhiitem.h>
#include <rhi/qrhi.h>
#include <memory>

// Shared scaffolding for the QQuickRhiItem renderers (SecureImageItem,
// VideoFrameItem): a full-target textured quad, an MVP uniform buffer, a
// linear clamp sampler, lazy pipeline construction from .qsb resources, and
// the common render-pass tail. Subclasses own their textures: they upload in
// render(), provide their sampledTexture bindings via textureBindings(), and
// end render() with finishRender().
class TexQuadRendererBase : public QQuickRhiItemRenderer {
protected:
    void initialize(QRhiCommandBuffer* cb) override;

    // Sampled-texture bindings for slots 1..N (slot 0 is the MVP uniform,
    // added by rebuildResources). Called with sampler_/vbuf_/ubuf_ valid.
    virtual QVector<QRhiShaderResourceBinding> textureBindings() = 0;

    // Resource path of this renderer's fragment shader (.qsb). The vertex
    // shader is the shared texquad.vert.
    virtual const char* fragmentShaderResource() const = 0;

    // Rebuild srb_ from the MVP uniform + textureBindings(); clears
    // resourcesDirty_ on success.
    void rebuildResources();

    // Common render() tail: SRB rebuild if dirty, MVP update, lazy pipeline,
    // then the begin/draw/end pass. canDraw gates the draw call (subclass
    // passes "texture uploaded at least once").
    void finishRender(QRhiCommandBuffer* cb, QRhiResourceUpdateBatch* batch, bool canDraw);

    std::unique_ptr<QRhiSampler> sampler_;
    std::unique_ptr<QRhiBuffer> vbuf_;
    std::unique_ptr<QRhiBuffer> ubuf_;
    std::unique_ptr<QRhiShaderResourceBindings> srb_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    bool resourcesDirty_ = true;

private:
    void ensurePipeline();
};
