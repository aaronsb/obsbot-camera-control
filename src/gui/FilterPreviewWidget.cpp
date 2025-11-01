#include "FilterPreviewWidget.h"

#include <QDebug>
#include <QOpenGLFunctions>
#include <QVector2D>
#include <QVideoFrame>
#include <QtMath>
#include <cmath>

namespace {

const char *kVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;

uniform vec2 u_scale;

out vec2 v_texCoord;

void main()
{
    vec2 scaledPos = vec2(a_position.x / u_scale.x, a_position.y / u_scale.y);
    gl_Position = vec4(scaledPos, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
)";

const char *kFragmentShaderSource = R"(#version 330 core
uniform sampler2D u_texture;
uniform vec2 u_texelSize;
uniform float u_brightness;
uniform float u_contrast;
uniform float u_exposure;
uniform float u_highlights;
uniform float u_shadows;
uniform float u_saturation;
uniform float u_vibrance;
uniform float u_temperature;
uniform float u_tint;
uniform float u_noise;
uniform float u_blur;
uniform float u_sharpen;
uniform float u_glow;
uniform float u_bloom;
uniform float u_softFocus;
uniform float u_duoToneIntensity;
uniform vec3 u_duoToneShadow;
uniform vec3 u_duoToneHighlight;
uniform int u_horizontalFlip;

in vec2 v_texCoord;
out vec4 fragColor;

float luminance(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

float random(vec2 co)
{
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec2 uv = vec2(v_texCoord.x, 1.0 - v_texCoord.y);
    if (u_horizontalFlip == 1) {
        uv.x = 1.0 - uv.x;
    }

    vec4 src = texture(u_texture, uv);
    vec3 color = src.rgb;

    // Precompute blur kernel if needed
    vec3 blurColor = color;
    if (u_blur > 0.0 || u_sharpen > 0.0 || u_glow > 0.0 || u_bloom > 0.0 || u_softFocus > 0.0) {
        vec2 offsets[9] = vec2[](
            vec2(-1.0, -1.0), vec2(0.0, -1.0), vec2(1.0, -1.0),
            vec2(-1.0,  0.0), vec2(0.0,  0.0), vec2(1.0,  0.0),
            vec2(-1.0,  1.0), vec2(0.0,  1.0), vec2(1.0,  1.0)
        );
        float kernel[9] = float[](1.0, 2.0, 1.0,
                                  2.0, 4.0, 2.0,
                                  1.0, 2.0, 1.0);
        vec3 accum = vec3(0.0);
        float weightSum = 0.0;
        for (int i = 0; i < 9; ++i) {
            vec2 sampleUv = uv + offsets[i] * u_texelSize;
            vec3 sampleColor = texture(u_texture, clamp(sampleUv, vec2(0.0), vec2(1.0))).rgb;
            accum += sampleColor * kernel[i];
            weightSum += kernel[i];
        }
        blurColor = accum / weightSum;
    }

    // Basic adjustments
    color += vec3(u_brightness);
    color = (color - 0.5) * (1.0 + u_contrast) + 0.5;
    color *= pow(2.0, u_exposure);

    float luma = luminance(color);
    float shadowMask = clamp((0.5 - luma) * 2.0, 0.0, 1.0);
    float highlightMask = clamp((luma - 0.5) * 2.0, 0.0, 1.0);
    color += vec3(u_shadows) * shadowMask;
    color += vec3(u_highlights) * highlightMask;

    // Color adjustments
    float newLuma = luminance(color);
    vec3 gray = vec3(newLuma);
    float satFactor = clamp(1.0 + u_saturation, 0.0, 2.0);
    color = mix(gray, color, satFactor);

    float currentSat = length(color - gray);
    float vibranceFactor = clamp(1.0 + u_vibrance * (1.0 - clamp(currentSat, 0.0, 1.0)), 0.0, 2.0);
    color = mix(gray, color, vibranceFactor);

    color.r += u_temperature;
    color.b -= u_temperature;
    color.g += u_tint;

    // Detail adjustments
    if (u_blur > 0.0) {
        color = mix(color, blurColor, clamp(u_blur, 0.0, 1.0));
    }

    if (u_sharpen > 0.0) {
        vec3 sharpened = color + (color - blurColor) * (u_sharpen * 1.5);
        color = mix(color, sharpened, clamp(u_sharpen, 0.0, 1.0));
    }

    if (u_softFocus > 0.0) {
        color = mix(color, blurColor, clamp(u_softFocus, 0.0, 1.0));
    }

    if (u_glow > 0.0) {
        color += blurColor * (u_glow * 0.5);
    }

    if (u_bloom > 0.0) {
        color = mix(color, max(color, blurColor), clamp(u_bloom, 0.0, 1.0));
    }

    if (u_noise > 0.0) {
        float noiseVal = random(uv * 1000.0);
        color += (noiseVal - 0.5) * u_noise;
    }

    if (u_duoToneIntensity > 0.0) {
        float tone = luminance(color);
        vec3 duo = mix(u_duoToneShadow, u_duoToneHighlight, tone);
        color = mix(color, duo, clamp(u_duoToneIntensity, 0.0, 1.0));
    }

    color = clamp(color, 0.0, 1.0);
    fragColor = vec4(color, src.a);
}
)";

} // namespace

FilterPreviewWidget::FilterPreviewWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_textureDirty(false)
    , m_emitPending(false)
    , m_effectSettings(VideoEffectsSettings::defaults())
    , m_vertexBuffer(QOpenGLBuffer::VertexBuffer)
    , m_geometryInitialized(false)
{
    setMinimumSize(320, 240);
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);
}

FilterPreviewWidget::~FilterPreviewWidget()
{
    if (isValid()) {
        makeCurrent();
        cleanupGLResources();
        doneCurrent();
    }
}

void FilterPreviewWidget::setVideoEffects(const VideoEffectsSettings &settings)
{
    if (m_effectSettings == settings) {
        return;
    }
    m_effectSettings = settings;
    update();
}

void FilterPreviewWidget::updateVideoFrame(const QVideoFrame &frame)
{
    QVideoFrame copy(frame);
    if (!copy.isValid()) {
        return;
    }

    QImage image = copy.toImage();
    if (image.isNull()) {
        return;
    }

    if (image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }

    m_currentImage = image;
    m_textureDirty = true;
    m_emitPending = true;
    update();
}

void FilterPreviewWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.f, 0.f, 0.f, 1.f);

    if (context()) {
        connect(context(), &QOpenGLContext::aboutToBeDestroyed,
                this, &FilterPreviewWidget::handleContextAboutToBeDestroyed,
                Qt::DirectConnection);
    }

    cleanupGLResources();

    ensureProgram();
    ensureGeometry();
}

void FilterPreviewWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w);
    Q_UNUSED(h);
}

void FilterPreviewWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_currentImage.isNull()) {
        m_emitPending = false;
        return;
    }

    ensureProgram();
    ensureGeometry();
    uploadTextureIfNeeded();

    if (!m_program || !m_texture) {
        return;
    }

    const QSize frameSize = m_currentImage.size();
    m_program->bind();
    m_program->setUniformValue("u_texelSize", QVector2D(1.0f / frameSize.width(), 1.0f / frameSize.height()));
    applyEffectsUniforms();

    ensureFramebuffer(frameSize);

    if (m_emitPending && m_framebuffer) {
        m_framebuffer->bind();
        renderToCurrentTarget(frameSize);

        QImage output(frameSize, QImage::Format_RGBA8888);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, frameSize.width(), frameSize.height(),
                     GL_RGBA, GL_UNSIGNED_BYTE, output.bits());
        m_framebuffer->release();

        emit processedFrameReady(output.flipped(Qt::Vertical));
        m_emitPending = false;
    }

    renderToCurrentTarget(size());
    m_program->release();
}

void FilterPreviewWidget::ensureProgram()
{
    if (m_program) {
        return;
    }

    m_program = std::make_unique<QOpenGLShaderProgram>();
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSource)) {
        qWarning() << "Failed to compile vertex shader:" << m_program->log();
    }
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSource)) {
        qWarning() << "Failed to compile fragment shader:" << m_program->log();
    }
    if (!m_program->link()) {
        qWarning() << "Failed to link shader program:" << m_program->log();
        m_program.reset();
        return;
    }
}

void FilterPreviewWidget::ensureGeometry()
{
    if (m_geometryInitialized) {
        return;
    }

    static const float vertexData[] = {
        // position   // texCoord
        -1.f, -1.f,   0.f, 1.f,
         1.f, -1.f,   1.f, 1.f,
        -1.f,  1.f,   0.f, 0.f,
         1.f,  1.f,   1.f, 0.f
    };

    m_vertexArray.create();
    QOpenGLVertexArrayObject::Binder binder(&m_vertexArray);

    m_vertexBuffer.create();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(vertexData, sizeof(vertexData));

    if (m_program) {
        m_program->bind();
        m_program->enableAttributeArray(0);
        m_program->enableAttributeArray(1);
        m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
        m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
        m_program->release();
    }

    m_vertexBuffer.release();
    m_geometryInitialized = true;
}

void FilterPreviewWidget::ensureFramebuffer(const QSize &size)
{
    if (size.isEmpty()) {
        m_framebuffer.reset();
        return;
    }

    if (m_framebuffer && m_framebuffer->size() == size) {
        return;
    }

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    format.setTextureTarget(GL_TEXTURE_2D);
    format.setInternalTextureFormat(GL_RGBA8);

    m_framebuffer = std::make_unique<QOpenGLFramebufferObject>(size, format);
    if (!m_framebuffer->isValid()) {
        qWarning() << "Failed to create framebuffer object for filter preview";
        m_framebuffer.reset();
    }
}

void FilterPreviewWidget::uploadTextureIfNeeded()
{
    if (!m_textureDirty || m_currentImage.isNull()) {
        return;
    }

    const QSize frameSize = m_currentImage.size();
    if (!m_texture) {
        m_texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    }

    if (!m_texture->isCreated() ||
        m_texture->width() != frameSize.width() ||
        m_texture->height() != frameSize.height()) {
        m_texture->destroy();
        m_texture->create();
        m_texture->bind();
        m_texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
        m_texture->setSize(frameSize.width(), frameSize.height());
        m_texture->setMipLevels(1);
        m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        m_texture->setMinificationFilter(QOpenGLTexture::Linear);
        m_texture->setMagnificationFilter(QOpenGLTexture::Linear);
        m_texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    } else {
        m_texture->bind();
    }

    QImage glImage = m_currentImage.flipped(Qt::Vertical);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    frameSize.width(), frameSize.height(),
                    GL_RGBA, GL_UNSIGNED_BYTE, glImage.constBits());
    m_texture->release();

    m_textureDirty = false;
}

void FilterPreviewWidget::renderToCurrentTarget(const QSize &targetSize)
{
    if (!m_program || !m_texture) {
        return;
    }

    const qreal dpr = devicePixelRatioF();
    const QSize physicalSize = (QSizeF(targetSize) * dpr).toSize();

    glViewport(0, 0, physicalSize.width(), physicalSize.height());
    glClear(GL_COLOR_BUFFER_BIT);

    m_program->bind();
    m_program->setUniformValue("u_texture", 0);
    const QSizeF frameSize = frameAspectSize();
    const qreal frameAspect = frameSize.width() / frameSize.height();
    const qreal targetAspect = static_cast<qreal>(targetSize.width()) / targetSize.height();

    QVector2D scale(1.0f, 1.0f);
    if (frameAspect > targetAspect) {
        scale.setY(frameAspect / targetAspect);
    } else {
        scale.setX(targetAspect / frameAspect);
    }

    m_program->setUniformValue("u_scale", scale);

    glActiveTexture(GL_TEXTURE0);
    m_texture->bind();

    QOpenGLVertexArrayObject::Binder binder(&m_vertexArray);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_texture->release();
    m_program->release();
}

QSizeF FilterPreviewWidget::frameAspectSize() const
{
    if (m_currentImage.isNull()) {
        return QSizeF(16.0, 9.0);
    }
    return QSizeF(m_currentImage.size());
}

void FilterPreviewWidget::cleanupGLResources()
{
    if (m_texture) {
        m_texture->destroy();
        m_texture.reset();
    }
    if (m_framebuffer) {
        m_framebuffer.reset();
    }
    if (m_vertexBuffer.isCreated()) {
        m_vertexBuffer.destroy();
    }
    if (m_vertexArray.isCreated()) {
        m_vertexArray.destroy();
    }
    m_program.reset();
    m_geometryInitialized = false;
}

void FilterPreviewWidget::handleContextAboutToBeDestroyed()
{
    if (!isValid()) {
        return;
    }

    makeCurrent();
    cleanupGLResources();
    doneCurrent();
}

void FilterPreviewWidget::applyEffectsUniforms()
{
    if (!m_program) {
        return;
    }

    const auto clamp01 = [](float value) {
        return qBound(0.0f, value, 1.0f);
    };

    m_program->setUniformValue("u_brightness", m_effectSettings.brightness);
    m_program->setUniformValue("u_contrast", m_effectSettings.contrast);
    m_program->setUniformValue("u_exposure", m_effectSettings.exposure);
    m_program->setUniformValue("u_highlights", m_effectSettings.highlights);
    m_program->setUniformValue("u_shadows", m_effectSettings.shadows);
    m_program->setUniformValue("u_saturation", m_effectSettings.saturation);
    m_program->setUniformValue("u_vibrance", m_effectSettings.vibrance);
    m_program->setUniformValue("u_temperature", m_effectSettings.temperature);
    m_program->setUniformValue("u_tint", m_effectSettings.tint);
    m_program->setUniformValue("u_noise", clamp01(m_effectSettings.noise));
    m_program->setUniformValue("u_blur", clamp01(m_effectSettings.blur));
    m_program->setUniformValue("u_sharpen", clamp01(m_effectSettings.sharpen));
    m_program->setUniformValue("u_glow", clamp01(m_effectSettings.glow));
    m_program->setUniformValue("u_bloom", clamp01(m_effectSettings.bloom));
    m_program->setUniformValue("u_softFocus", clamp01(m_effectSettings.softFocus));
    m_program->setUniformValue("u_duoToneIntensity", clamp01(m_effectSettings.duoToneIntensity));

    QVector3D shadowColor = srgbColorToLinearVec3(m_effectSettings.duoToneShadow);
    QVector3D highlightColor = srgbColorToLinearVec3(m_effectSettings.duoToneHighlight);
    m_program->setUniformValue("u_duoToneShadow", shadowColor);
    m_program->setUniformValue("u_duoToneHighlight", highlightColor);
    m_program->setUniformValue("u_horizontalFlip", m_effectSettings.horizontalFlip ? 1 : 0);
}

QVector3D FilterPreviewWidget::srgbColorToLinearVec3(const QColor &color) const
{
    auto toLinear = [](float channel) {
        if (channel <= 0.04045f) {
            return channel / 12.92f;
        }
        return std::pow((channel + 0.055f) / 1.055f, 2.4f);
    };
    return QVector3D(
        toLinear(color.redF()),
        toLinear(color.greenF()),
        toLinear(color.blueF()));
}
