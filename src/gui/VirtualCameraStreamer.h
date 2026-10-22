#ifndef VIRTUALCAMERASTREAMER_H
#define VIRTUALCAMERASTREAMER_H

#include <QElapsedTimer>
#include <QObject>
#include <QImage>
#include <QString>
#include <QSize>

class QThread;
class VirtualCameraStreamerWorker;

/**
 * @brief Streams preview frames into a v4l2loopback virtual camera device.
 *
 * The streamer opens the requested V4L2 video output device and writes
 * frames in YUYV (YUY2) format. An optional forced resolution keeps the
 * virtual camera output stable for conferencing apps that dislike runtime
 * format changes.
 */
class VirtualCameraStreamer : public QObject
{
    Q_OBJECT

public:
    explicit VirtualCameraStreamer(QObject *parent = nullptr);
    ~VirtualCameraStreamer() override;

    void setDevicePath(const QString &path);
    QString devicePath() const { return m_devicePath; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    void setForcedResolution(const QSize &resolution);
    QSize forcedResolution() const { return m_forcedResolution; }

    // Caps how many frames per second are forwarded to the virtual camera
    // device, independent of the capture/preview frame rate. 0 = unlimited.
    void setMaxFrameRate(int fps);
    int maxFrameRate() const { return m_maxFrameRate; }

public slots:
    void onProcessedFrameReady(const QImage &frame);

signals:
    void errorOccurred(const QString &message);

private slots:
    void handleWorkerStreamingStateChanged(bool enabled);

private:
    void ensureWorker();
    void scheduleFrameDelivery(const QImage &frame);
    bool shouldForwardFrame();

    QString m_devicePath;
    bool m_enabled;
    QSize m_forcedResolution;
    int m_maxFrameRate;
    QElapsedTimer m_frameRateTimer;
    qint64 m_nextFrameDueNs;
    QThread *m_workerThread;
    VirtualCameraStreamerWorker *m_worker;
    bool m_workerInitialized;
};

#endif // VIRTUALCAMERASTREAMER_H
