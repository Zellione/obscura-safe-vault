#include "rhi_quad_renderer.h"

#include <QFile>
#include <QMatrix4x4>
#include <rhi/qshader.h>
#include <QtCore/qloggingcategory.h>

Q_LOGGING_CATEGORY(lcRhiQuad, "osv.rhi_quad")

void TexQuadRendererBase::initialize(QRhiCommandBuffer* cb)
{
    QRhi* rhi = this->rhi();

    // Create vertex buffer: textured quad as 2 triangles (6 vertices, normalized coords)
    struct Vertex {
        float pos[4];      // x, y, 0, 1
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
        qCWarning(lcRhiQuad) << "Failed to create vertex buffer";
        return;
    }

    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
    batch->uploadStaticBuffer(vbuf_.get(), vertices);
    cb->resourceUpdate(batch);

    // Create uniform buffer for MVP matrix (64 bytes = 4x4 matrix)
    ubuf_.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    if (!ubuf_->create()) {
        qCWarning(lcRhiQuad) << "Failed to create uniform buffer";
        return;
    }

    // Create sampler (shared by all of the subclass's textures)
    sampler_.reset(rhi->newSampler(
        QRhiSampler::Linear,  // magFilter
        QRhiSampler::Linear,  // minFilter
        QRhiSampler::None,    // mipmapMode
        QRhiSampler::ClampToEdge,
        QRhiSampler::ClampToEdge));
    if (!sampler_->create()) {
        qCWarning(lcRhiQuad) << "Failed to create sampler";
        return;
    }

    resourcesDirty_ = true;
}

void TexQuadRendererBase::rebuildResources()
{
    if (!sampler_ || !vbuf_ || !ubuf_) return;

    QRhi* rhi = this->rhi();

    QVector<QRhiShaderResourceBinding> bindings;
    bindings << QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage, ubuf_.get());
    bindings += textureBindings();

    srb_.reset(rhi->newShaderResourceBindings());
    srb_->setBindings(bindings.begin(), bindings.end());
    if (!srb_->create()) {
        qCWarning(lcRhiQuad) << "Failed to create shader resource bindings";
        return;
    }

    resourcesDirty_ = false;
}

void TexQuadRendererBase::ensurePipeline()
{
    if (pipeline_) return;

    QRhi* rhi = this->rhi();

    // Load shaders from resources
    QFile vs_file(QStringLiteral(":/osvqt/shaders/texquad.vert.qsb"));
    if (!vs_file.open(QIODevice::ReadOnly)) {
        qCWarning(lcRhiQuad) << "Failed to load vertex shader";
        return;
    }
    QShader vs = QShader::fromSerialized(vs_file.readAll());
    vs_file.close();

    QFile fs_file(QString::fromLatin1(fragmentShaderResource()));
    if (!fs_file.open(QIODevice::ReadOnly)) {
        qCWarning(lcRhiQuad) << "Failed to load fragment shader" << fragmentShaderResource();
        return;
    }
    QShader fs = QShader::fromSerialized(fs_file.readAll());
    fs_file.close();

    if (!vs.isValid() || !fs.isValid()) {
        qCWarning(lcRhiQuad) << "Invalid shaders loaded";
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
        qCWarning(lcRhiQuad) << "Failed to create graphics pipeline";
        pipeline_.reset();
        return;
    }
}

void TexQuadRendererBase::finishRender(QRhiCommandBuffer* cb, QRhiResourceUpdateBatch* batch, bool canDraw)
{
    // Rebuild SRB if textures changed
    if (resourcesDirty_) {
        rebuildResources();
    }

    // Update MVP matrix. Vertices are in normalized coordinates (0-1), so
    // ortho maps 0-1 to clip space: a vertex at (1,1) fills the render target.
    if (ubuf_) {
        QMatrix4x4 mvp;
        mvp.ortho(0.0f, 1.0f,  // left, right (in normalized coords)
                  1.0f, 0.0f,  // top, bottom (in normalized coords, flipped)
                  -1.0f, 1.0f);

        batch->updateDynamicBuffer(ubuf_.get(), 0, 64, mvp.constData());
    }

    ensurePipeline();

    const QColor clear(0, 0, 0, 0);
    cb->resourceUpdate(batch);
    cb->beginPass(renderTarget(), clear, {1.0f, 0});

    if (canDraw && pipeline_) {
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
