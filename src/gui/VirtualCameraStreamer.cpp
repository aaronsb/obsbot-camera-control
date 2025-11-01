#include "VirtualCameraStreamer.h"

#include <QByteArray>
#include <QImage>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMetaType>
#include <QQueue>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(VirtualCameraLog, "obsbot.virtualcamera")

namespace {

constexpr const char *kDefaultDevicePath = "/dev/video42";

QString errnoString()
{
    return QString::fromLocal8Bit(strerror(errno));
}

inline uint8_t clampToByte(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

struct YuvComponents {
    uint8_t y;
    uint8_t u;
    uint8_t v;
};

YuvComponents rgbToYuv(uint8_t r, uint8_t g, uint8_t b)
{
    // BT.601 conversion with integer math
    const int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
    const int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    const int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

    return {clampToByte(y), clampToByte(u), clampToByte(v)};
}

bool convertRgbToYuyv(const QImage &image, QByteArray &outBuffer)
{
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0) {
        return false;
    }

    outBuffer.resize(width * height * 2);
    uint8_t *dst = reinterpret_cast<uint8_t *>(outBuffer.data());

    for (int y = 0; y < height; ++y) {
        const uint8_t *src = reinterpret_cast<const uint8_t *>(image.constScanLine(y));
        uint8_t *rowPtr = dst + (y * width * 2);

        int x = 0;
        for (; x + 1 < width; x += 2) {
            const uint8_t *p0 = src + (x * 3);
            const uint8_t *p1 = src + ((x + 1) * 3);

            const YuvComponents yuv0 = rgbToYuv(p0[0], p0[1], p0[2]);
            const YuvComponents yuv1 = rgbToYuv(p1[0], p1[1], p1[2]);

            const uint8_t u = clampToByte((static_cast<int>(yuv0.u) + static_cast<int>(yuv1.u)) / 2);
            const uint8_t v = clampToByte((static_cast<int>(yuv0.v) + static_cast<int>(yuv1.v)) / 2);

            rowPtr[(x * 2) + 0] = yuv0.y;
            rowPtr[(x * 2) + 1] = u;
            rowPtr[(x * 2) + 2] = yuv1.y;
            rowPtr[(x * 2) + 3] = v;
        }

        if (x < width) {
            const uint8_t *p0 = src + (x * 3);
            const YuvComponents yuv0 = rgbToYuv(p0[0], p0[1], p0[2]);

            rowPtr[(x * 2) + 0] = yuv0.y;
            rowPtr[(x * 2) + 1] = yuv0.u;
            rowPtr[(x * 2) + 2] = yuv0.y;
            rowPtr[(x * 2) + 3] = yuv0.v;
        }
    }

    return true;
}

} // namespace

class VirtualCameraStreamerWorker : public QObject
{
    Q_OBJECT

public:
    VirtualCameraStreamerWorker()
        : m_fd(-1)
        , m_devicePath(QString::fromLatin1(kDefaultDevicePath))
        , m_enabled(false)
        , m_deviceConfigured(false)
        , m_frameWidth(0)
        , m_frameHeight(0)
        , m_processing(false)
    {
    }

    ~VirtualCameraStreamerWorker() override
    {
        shutdown();
    }

public slots:
    void setDevicePath(const QString &path)
    {
        const QString normalized = path.trimmed().isEmpty()
            ? QString::fromLatin1(kDefaultDevicePath)
            : path.trimmed();

        if (normalized == m_devicePath) {
            return;
        }

        m_devicePath = normalized;
        closeDevice();
    }

    void setForcedResolution(const QSize &resolution)
    {
        const QSize normalized = resolution.isValid() ? resolution : QSize();
        if (normalized == m_forcedResolution) {
            return;
        }

        m_forcedResolution = normalized;
        m_deviceConfigured = false;
    }

    void setEnabled(bool enabled)
    {
        if (m_enabled == enabled) {
            return;
        }

        m_enabled = enabled;
        if (!m_enabled) {
            clearQueue();
            closeDevice();
        }

        emit streamingStateChanged(m_enabled);

        if (m_enabled && !m_frameQueue.isEmpty() && !m_processing) {
            m_processing = true;
            QMetaObject::invokeMethod(this, &VirtualCameraStreamerWorker::processNextFrame, Qt::QueuedConnection);
        }
    }

    void processFrame(const QImage &frame)
    {
        if (!m_enabled || frame.isNull()) {
            return;
        }

        if (m_frameQueue.size() >= 3) {
            m_frameQueue.dequeue(); // Drop oldest frame to avoid backlog
        }

        m_frameQueue.enqueue(frame);

        if (!m_processing) {
            m_processing = true;
            QMetaObject::invokeMethod(this, &VirtualCameraStreamerWorker::processNextFrame, Qt::QueuedConnection);
        }
    }

    void shutdown()
    {
        m_enabled = false;
        clearQueue();
        closeDevice();
    }

signals:
    void errorOccurred(const QString &message);
    void streamingStateChanged(bool enabled);

private:
    void processNextFrame()
    {
        if (!m_enabled) {
            m_processing = false;
            clearQueue();
            return;
        }

        if (m_frameQueue.isEmpty()) {
            m_processing = false;
            return;
        }

        QImage image = prepareFrame(m_frameQueue.dequeue());
        if (!image.isNull()) {
            const int width = image.width();
            const int height = image.height();
            if (ensureDevice(width, height)) {
                if (!writeFrame(image)) {
                    closeDevice();
                }
            }
        }

        if (!m_frameQueue.isEmpty() && m_enabled) {
            QMetaObject::invokeMethod(this, &VirtualCameraStreamerWorker::processNextFrame, Qt::QueuedConnection);
        } else {
            m_processing = false;
        }
    }

    QImage prepareFrame(const QImage &frame) const
    {
        if (frame.isNull()) {
            return QImage();
        }

        QImage image = frame;
        if (image.format() != QImage::Format_RGB888) {
            image = image.convertToFormat(QImage::Format_RGB888);
            if (image.isNull()) {
                qCWarning(VirtualCameraLog) << "Failed to convert frame to RGB888 format";
                return QImage();
            }
        }

        QSize targetSize = m_forcedResolution.isValid() ? m_forcedResolution : image.size();
        if (targetSize.width() <= 0 || targetSize.height() <= 0) {
            return QImage();
        }

        if (image.size() != targetSize) {
            if (m_forcedResolution.isValid()) {
                QImage scaled = image.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                if (scaled.isNull()) {
                    qCWarning(VirtualCameraLog) << "Failed to scale frame to forced resolution" << targetSize;
                    return QImage();
                }

                if (scaled.size() != targetSize) {
                    const int xOffset = std::max(0, (scaled.width() - targetSize.width()) / 2);
                    const int yOffset = std::max(0, (scaled.height() - targetSize.height()) / 2);
                    image = scaled.copy(xOffset, yOffset, targetSize.width(), targetSize.height());
                } else {
                    image = scaled;
                }
            } else {
                image = image.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
        }

        if (image.format() != QImage::Format_RGB888) {
            image = image.convertToFormat(QImage::Format_RGB888);
        }

        return image;
    }

    bool ensureDevice(int width, int height)
    {
        if (m_fd == -1) {
            m_fd = ::open(m_devicePath.toLocal8Bit().constData(), O_WRONLY);
            if (m_fd == -1) {
                emit errorOccurred(tr("Cannot open virtual camera device %1: %2")
                    .arg(m_devicePath, errnoString()));
                qCWarning(VirtualCameraLog) << "Failed to open device" << m_devicePath << errnoString();
                m_enabled = false;
                emit streamingStateChanged(false);
                return false;
            }
            m_deviceConfigured = false;
        }

        if (!m_deviceConfigured || width != m_frameWidth || height != m_frameHeight) {
            if (!configureFormat(width, height)) {
                closeDevice();
                m_enabled = false;
                emit streamingStateChanged(false);
                return false;
            }
            m_deviceConfigured = true;
            m_frameWidth = width;
            m_frameHeight = height;
        }

        return true;
    }

    bool configureFormat(int width, int height)
    {
        if (m_fd == -1) {
            return false;
        }

        struct v4l2_format format;
        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        format.fmt.pix.width = width;
        format.fmt.pix.height = height;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        format.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
        format.fmt.pix.bytesperline = width * 2;
        format.fmt.pix.sizeimage = format.fmt.pix.bytesperline * height;

        if (ioctl(m_fd, VIDIOC_S_FMT, &format) == -1) {
            emit errorOccurred(tr("Failed to configure virtual camera format: %1")
                .arg(errnoString()));
            qCWarning(VirtualCameraLog) << "VIDIOC_S_FMT failed" << errnoString();
            return false;
        }

        return true;
    }

    bool writeFrame(const QImage &image)
    {
        if (m_fd == -1) {
            return false;
        }

        QByteArray buffer;
        if (!convertRgbToYuyv(image, buffer)) {
            emit errorOccurred(tr("Failed to convert frame for virtual camera output"));
            qCWarning(VirtualCameraLog) << "Frame conversion to YUYV failed";
            return false;
        }

        const ssize_t frameSize = buffer.size();
        const ssize_t written = ::write(m_fd, buffer.constData(), frameSize);
        if (written != frameSize) {
            emit errorOccurred(tr("Failed to write frame to virtual camera: %1")
                .arg(errnoString()));
            qCWarning(VirtualCameraLog) << "Short write to device" << written << "expected" << frameSize;
            return false;
        }

        return true;
    }

    void closeDevice()
    {
        if (m_fd != -1) {
            ::close(m_fd);
            m_fd = -1;
        }
        m_deviceConfigured = false;
        m_frameWidth = 0;
        m_frameHeight = 0;
    }

    void clearQueue()
    {
        m_frameQueue.clear();
        m_processing = false;
    }

    int m_fd;
    QString m_devicePath;
    bool m_enabled;
    bool m_deviceConfigured;
    int m_frameWidth;
    int m_frameHeight;
    QSize m_forcedResolution;
    QQueue<QImage> m_frameQueue;
    bool m_processing;
};

VirtualCameraStreamer::VirtualCameraStreamer(QObject *parent)
    : QObject(parent)
    , m_devicePath(QString::fromLatin1(kDefaultDevicePath))
    , m_enabled(false)
    , m_forcedResolution()
    , m_workerThread(nullptr)
    , m_worker(nullptr)
    , m_workerInitialized(false)
{
    qRegisterMetaType<QImage>("QImage");
}

VirtualCameraStreamer::~VirtualCameraStreamer()
{
    if (m_workerInitialized && m_workerThread && m_worker) {
        QMetaObject::invokeMethod(m_worker, &VirtualCameraStreamerWorker::shutdown, Qt::BlockingQueuedConnection);
        m_workerThread->quit();
        m_workerThread->wait();
        m_worker = nullptr;
        m_workerThread = nullptr;
        m_workerInitialized = false;
    }
}

void VirtualCameraStreamer::setDevicePath(const QString &path)
{
    const QString normalized = path.trimmed().isEmpty()
        ? QString::fromLatin1(kDefaultDevicePath)
        : path.trimmed();

    if (normalized == m_devicePath) {
        return;
    }

    m_devicePath = normalized;
    ensureWorker();
    const QString devicePathCopy = m_devicePath;
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, devicePathCopy]() {
            worker->setDevicePath(devicePathCopy);
        },
        Qt::QueuedConnection);
}

void VirtualCameraStreamer::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    ensureWorker();
    const bool enabledCopy = m_enabled;
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, enabledCopy]() {
            worker->setEnabled(enabledCopy);
        },
        Qt::QueuedConnection);
}

void VirtualCameraStreamer::setForcedResolution(const QSize &resolution)
{
    const QSize normalized = resolution.isValid() ? resolution : QSize();
    if (normalized == m_forcedResolution) {
        return;
    }

    m_forcedResolution = normalized;
    ensureWorker();
    const QSize resolutionCopy = m_forcedResolution;
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, resolutionCopy]() {
            worker->setForcedResolution(resolutionCopy);
        },
        Qt::QueuedConnection);
}

void VirtualCameraStreamer::onProcessedFrameReady(const QImage &frame)
{
    if (!m_enabled || frame.isNull()) {
        return;
    }

    ensureWorker();
    scheduleFrameDelivery(frame);
}

void VirtualCameraStreamer::ensureWorker()
{
    if (m_workerInitialized) {
        return;
    }

    m_workerThread = new QThread(this);
    m_worker = new VirtualCameraStreamerWorker();
    m_worker->moveToThread(m_workerThread);
    connect(m_worker, &VirtualCameraStreamerWorker::errorOccurred,
            this, &VirtualCameraStreamer::errorOccurred);
    connect(m_worker, &VirtualCameraStreamerWorker::streamingStateChanged,
            this, &VirtualCameraStreamer::handleWorkerStreamingStateChanged);
    connect(m_workerThread, &QThread::finished,
            m_worker, &QObject::deleteLater);

    m_workerThread->start();
    m_workerInitialized = true;
    const QString devicePathCopy = m_devicePath;
    const QSize resolutionCopy = m_forcedResolution;
    const bool enabledCopy = m_enabled;

    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, devicePathCopy]() {
            worker->setDevicePath(devicePathCopy);
        },
        Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, resolutionCopy]() {
            worker->setForcedResolution(resolutionCopy);
        },
        Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, enabledCopy]() {
            worker->setEnabled(enabledCopy);
        },
        Qt::QueuedConnection);
}

void VirtualCameraStreamer::scheduleFrameDelivery(const QImage &frame)
{
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, image = frame]() {
            worker->processFrame(image);
        },
        Qt::QueuedConnection);
}

void VirtualCameraStreamer::handleWorkerStreamingStateChanged(bool enabled)
{
    m_enabled = enabled;
}

#include "VirtualCameraStreamer.moc"
