#ifndef CAMERAPREVIEWWIDGET_H
#define CAMERAPREVIEWWIDGET_H

#include <QWidget>
#include <QCamera>
#include <QCameraFormat>
#include "FilterPreviewWidget.h"

class QCameraDevice;
class QComboBox;
class QLabel;
class QMediaCaptureSession;
class QVideoSink;
class QWidget;
class VirtualCameraStreamer;

/**
 * @brief Camera preview widget with enable/disable control
 *
 * Features:
 * - Starts disabled by default
 * - Auto-disables when window is minimized or hidden
 * - Opens camera in shared mode (doesn't block other apps)
 * - Allows user to review effects of camera settings
 */
class CameraPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CameraPreviewWidget(QWidget *parent = nullptr);
    ~CameraPreviewWidget();

    bool isPreviewEnabled() const;
    void enablePreview(bool enabled);
    void setCameraDeviceId(const QString &deviceId);
    QString cameraDeviceId() const { return m_requestedDeviceId; }
    QString preferredFormatId() const { return m_selectedFormatId; }
    void setPreferredFormatId(const QString &formatId);
    void setControlsVisible(bool visible);
    void setVirtualCameraStreamer(VirtualCameraStreamer *streamer);
    void setVideoEffects(const FilterPreviewWidget::VideoEffectsSettings &settings);
    FilterPreviewWidget::VideoEffectsSettings videoEffects() const;

signals:
    void previewStateChanged(bool enabled);
    void aspectRatioChanged(double ratio);  // width / height
    void previewStarted();  // Emitted when preview successfully starts
    void previewFailed(const QString &error);  // Emitted when preview fails to start
    void preferredFormatChanged(const QString &formatId);

private slots:
    void onCameraError(QCamera::Error error);
    void onFormatSelectionChanged(int index);
    void onCameraActiveChanged(bool active);

private:
    void setupUI();
    bool initializeCamera();
    void startPreview();
    void stopPreview();
    void handleIncomingFrame(const QVideoFrame &frame);
    QCameraFormat findFormatById(const QString &id) const;
    QCameraDevice resolveCameraDevice() const;
    void refreshFormatOptions(const QCameraDevice &device);
    void applySelectedFormat();
    void updateAspectRatioFromFormat(const QCameraFormat &format);
    void updateStatus(const QString &message);
    QCameraFormat selectBestFallbackFormat() const;
    QCameraFormat chooseDefaultFormat() const;
    bool isJpegFormat(const QCameraFormat &format) const;

    QCamera *m_camera;
    QMediaCaptureSession *m_captureSession;
    QVideoSink *m_videoSink;
    FilterPreviewWidget *m_filterPreviewWidget;
    QComboBox *m_formatCombo;
    QLabel *m_statusLabel;
    QWidget *m_controlRow;
    VirtualCameraStreamer *m_virtualCameraStreamer;
    QString m_selectedFormatId;
    QString m_requestedDeviceId;
    QList<QCameraFormat> m_availableFormats;

    bool m_previewEnabled;
    bool m_isApplyingFormat;
};

#endif // CAMERAPREVIEWWIDGET_H
