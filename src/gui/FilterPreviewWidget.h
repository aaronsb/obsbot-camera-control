#ifndef FILTERPREVIEWWIDGET_H
#define FILTERPREVIEWWIDGET_H

#include <QColor>
#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QVideoFrame>
#include <memory>
#include <QtGlobal>
#include <QVector3D>

class FilterPreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    struct VideoEffectsSettings {
        float brightness = 0.0f;      // -1.0 to 1.0
        float contrast = 0.0f;        // -1.0 to 1.0
        float exposure = 0.0f;        // -2.0 to 2.0
        float highlights = 0.0f;      // -1.0 to 1.0
        float shadows = 0.0f;         // -1.0 to 1.0
        float saturation = 0.0f;      // -1.0 to 1.0
        float vibrance = 0.0f;        // -1.0 to 1.0
        float temperature = 0.0f;     // -1.0 to 1.0
        float tint = 0.0f;            // -1.0 to 1.0
        float noise = 0.0f;           // 0.0 to 1.0
        float blur = 0.0f;            // 0.0 to 1.0
        float sharpen = 0.0f;         // 0.0 to 1.0
        float glow = 0.0f;            // 0.0 to 1.0
        float bloom = 0.0f;           // 0.0 to 1.0
        float softFocus = 0.0f;       // 0.0 to 1.0
        float duoToneIntensity = 0.0f; // 0.0 to 1.0
        QColor duoToneShadow = QColor(30, 30, 60);
        QColor duoToneHighlight = QColor(220, 180, 160);
        bool horizontalFlip = false;

        bool operator==(const VideoEffectsSettings &other) const
        {
            return qFuzzyCompare(1.0f + brightness, 1.0f + other.brightness)
                && qFuzzyCompare(1.0f + contrast, 1.0f + other.contrast)
                && qFuzzyCompare(1.0f + exposure, 1.0f + other.exposure)
                && qFuzzyCompare(1.0f + highlights, 1.0f + other.highlights)
                && qFuzzyCompare(1.0f + shadows, 1.0f + other.shadows)
                && qFuzzyCompare(1.0f + saturation, 1.0f + other.saturation)
                && qFuzzyCompare(1.0f + vibrance, 1.0f + other.vibrance)
                && qFuzzyCompare(1.0f + temperature, 1.0f + other.temperature)
                && qFuzzyCompare(1.0f + tint, 1.0f + other.tint)
                && qFuzzyCompare(1.0f + noise, 1.0f + other.noise)
                && qFuzzyCompare(1.0f + blur, 1.0f + other.blur)
                && qFuzzyCompare(1.0f + sharpen, 1.0f + other.sharpen)
                && qFuzzyCompare(1.0f + glow, 1.0f + other.glow)
                && qFuzzyCompare(1.0f + bloom, 1.0f + other.bloom)
                && qFuzzyCompare(1.0f + softFocus, 1.0f + other.softFocus)
                && qFuzzyCompare(1.0f + duoToneIntensity, 1.0f + other.duoToneIntensity)
                && duoToneShadow == other.duoToneShadow
                && duoToneHighlight == other.duoToneHighlight
                && horizontalFlip == other.horizontalFlip;
        }

        bool operator!=(const VideoEffectsSettings &other) const
        {
            return !(*this == other);
        }

        static VideoEffectsSettings defaults() { return VideoEffectsSettings{}; }
    };

    explicit FilterPreviewWidget(QWidget *parent = nullptr);
    ~FilterPreviewWidget() override;

    void setVideoEffects(const VideoEffectsSettings &settings);
    VideoEffectsSettings videoEffects() const { return m_effectSettings; }
    void updateVideoFrame(const QVideoFrame &frame);

signals:
    void processedFrameReady(const QImage &frame);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void ensureProgram();
    void ensureGeometry();
    void ensureFramebuffer(const QSize &size);
    void uploadTextureIfNeeded();
    void renderToCurrentTarget(const QSize &targetSize);
    void applyEffectsUniforms();
    QVector3D srgbColorToLinearVec3(const QColor &color) const;

    QSizeF frameAspectSize() const;
    void cleanupGLResources();

    QImage m_currentImage;
    bool m_textureDirty;
    bool m_emitPending;
    VideoEffectsSettings m_effectSettings;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLTexture> m_texture;
    std::unique_ptr<QOpenGLFramebufferObject> m_framebuffer;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLVertexArrayObject m_vertexArray;
    bool m_geometryInitialized;

private slots:
    void handleContextAboutToBeDestroyed();
};

#endif // FILTERPREVIEWWIDGET_H
