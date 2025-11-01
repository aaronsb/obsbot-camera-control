#include "CameraPreviewWidget.h"

#include "FilterPreviewWidget.h"
#include "VirtualCameraStreamer.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QComboBox>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QSignalBlocker>
#include <QStringList>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <QtGlobal>
#include <QVideoFrameFormat>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QString formatIdFor(const QCameraFormat &format)
{
    if (format.isNull()) {
        return QString();
    }

    const QSize resolution = format.resolution();
    if (!resolution.isValid()) {
        return QString();
    }

    const int fps = static_cast<int>(std::round(format.maxFrameRate()));
    return QStringLiteral("%1x%2@%3")
        .arg(resolution.width())
        .arg(resolution.height())
        .arg(fps);
}

QString describeFormat(const QCameraFormat &format)
{
    if (format.isNull()) {
        return QStringLiteral("Unknown");
    }

    const QSize resolution = format.resolution();
    const int fps = static_cast<int>(std::round(format.maxFrameRate()));
    return QStringLiteral("%1 × %2 @ %3 fps")
        .arg(resolution.width())
        .arg(resolution.height())
        .arg(fps);
}

} // namespace

CameraPreviewWidget::CameraPreviewWidget(QWidget *parent)
    : QWidget(parent)
    , m_camera(nullptr)
    , m_captureSession(nullptr)
    , m_videoSink(nullptr)
    , m_filterPreviewWidget(nullptr)
    , m_formatCombo(nullptr)
    , m_statusLabel(nullptr)
    , m_controlRow(nullptr)
    , m_virtualCameraStreamer(nullptr)
    , m_selectedFormatId(QStringLiteral("auto"))
    , m_previewEnabled(false)
    , m_isApplyingFormat(false)
{
    setupUI();
}

CameraPreviewWidget::~CameraPreviewWidget()
{
    stopPreview();
}

void CameraPreviewWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_controlRow = new QWidget(this);
    m_controlRow->setObjectName("controlRow");
    QHBoxLayout *controlLayout = new QHBoxLayout(m_controlRow);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(8);

    QLabel *formatLabel = new QLabel(tr("Preview Quality:"), this);
    formatLabel->setStyleSheet("font-size: 11px;");

    m_formatCombo = new QComboBox(this);
    m_formatCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_formatCombo->addItem(tr("Auto"), QStringLiteral("auto"));
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraPreviewWidget::onFormatSelectionChanged);

    controlLayout->addWidget(formatLabel);
    controlLayout->addWidget(m_formatCombo, 1);
    layout->addWidget(m_controlRow);

    m_statusLabel = new QLabel(tr("Preview disabled"), this);
    m_statusLabel->setStyleSheet("color: palette(mid); font-size: 11px;");
    layout->addWidget(m_statusLabel);

    m_filterPreviewWidget = new FilterPreviewWidget(this);
    m_filterPreviewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_filterPreviewWidget, 1);

    connect(m_filterPreviewWidget, &FilterPreviewWidget::processedFrameReady,
            this, [this](const QImage &image) {
                if (m_virtualCameraStreamer) {
                    m_virtualCameraStreamer->onProcessedFrameReady(image);
                }
            });
}

bool CameraPreviewWidget::isPreviewEnabled() const
{
    return m_previewEnabled;
}

void CameraPreviewWidget::setCameraDeviceId(const QString &deviceId)
{
    if (m_requestedDeviceId == deviceId) {
        return;
    }

    m_requestedDeviceId = deviceId;

    if (m_previewEnabled) {
        stopPreview();
        startPreview();
    }
}

void CameraPreviewWidget::enablePreview(bool enabled)
{
    if (enabled == m_previewEnabled) {
        return;
    }

    if (enabled) {
        startPreview();
    } else {
        stopPreview();
    }
}

void CameraPreviewWidget::setPreferredFormatId(const QString &formatId)
{
    const QString requestedId = formatId.isEmpty() ? QStringLiteral("auto") : formatId;

    if (!m_formatCombo) {
        m_selectedFormatId = requestedId;
        return;
    }

    QSignalBlocker blocker(m_formatCombo);
    int index = m_formatCombo->findData(requestedId);
    if (index < 0) {
        index = 0;
    }
    m_formatCombo->setCurrentIndex(index);
    m_selectedFormatId = m_formatCombo->itemData(index).toString();
}

void CameraPreviewWidget::setControlsVisible(bool visible)
{
    if (m_controlRow) {
        m_controlRow->setVisible(visible);
    }
}

void CameraPreviewWidget::setVirtualCameraStreamer(VirtualCameraStreamer *streamer)
{
    if (m_virtualCameraStreamer == streamer) {
        return;
    }

    m_virtualCameraStreamer = streamer;
}

void CameraPreviewWidget::setVideoEffects(const FilterPreviewWidget::VideoEffectsSettings &settings)
{
    if (!m_filterPreviewWidget) {
        return;
    }
    m_filterPreviewWidget->setVideoEffects(settings);
}

FilterPreviewWidget::VideoEffectsSettings CameraPreviewWidget::videoEffects() const
{
    if (!m_filterPreviewWidget) {
        return FilterPreviewWidget::VideoEffectsSettings::defaults();
    }
    return m_filterPreviewWidget->videoEffects();
}

void CameraPreviewWidget::startPreview()
{
    if (m_previewEnabled) {
        return;
    }

    stopPreview();

    if (!initializeCamera()) {
        return;
    }

    applySelectedFormat();
    updateStatus(tr("Opening camera..."));

    m_camera->start();
    if (m_camera->error() != QCamera::NoError) {
        const QString errorString = m_camera->errorString();
        stopPreview();
        emit previewFailed(errorString);
        updateStatus(tr("Failed to start: %1").arg(errorString));
        return;
    }

    m_previewEnabled = true;
    emit previewStateChanged(true);
}

void CameraPreviewWidget::stopPreview()
{
    if (m_videoSink) {
        disconnect(m_videoSink, nullptr, this, nullptr);
        delete m_videoSink;
        m_videoSink = nullptr;
    }

    if (m_camera) {
        disconnect(m_camera, nullptr, this, nullptr);
        m_camera->stop();
    }

    if (m_captureSession) {
        m_captureSession->setCamera(nullptr);
        m_captureSession->setVideoOutput(nullptr);
        delete m_captureSession;
        m_captureSession = nullptr;
    }

    if (m_camera) {
        delete m_camera;
        m_camera = nullptr;
    }

    if (m_previewEnabled) {
        m_previewEnabled = false;
        emit previewStateChanged(false);
    }

    updateStatus(tr("Preview disabled"));
}

bool CameraPreviewWidget::initializeCamera()
{
    const QCameraDevice device = resolveCameraDevice();
    if (device.isNull()) {
        const QString message = tr("No compatible camera detected");
        emit previewFailed(message);
        updateStatus(message);
        return false;
    }

    refreshFormatOptions(device);

    m_camera = new QCamera(device, this);
    connect(m_camera, &QCamera::errorOccurred,
            this, &CameraPreviewWidget::onCameraError);
    connect(m_camera, &QCamera::activeChanged,
            this, &CameraPreviewWidget::onCameraActiveChanged);

    m_captureSession = new QMediaCaptureSession(this);
    m_captureSession->setCamera(m_camera);

    m_videoSink = new QVideoSink(this);
    connect(m_videoSink, &QVideoSink::videoFrameChanged,
            this, &CameraPreviewWidget::handleIncomingFrame);

    m_captureSession->setVideoOutput(m_videoSink);

    return true;
}

void CameraPreviewWidget::handleIncomingFrame(const QVideoFrame &frame)
{
    if (!m_filterPreviewWidget) {
        return;
    }

    m_filterPreviewWidget->updateVideoFrame(frame);

    if (frame.isValid() && frame.width() > 0 && frame.height() > 0) {
        emit aspectRatioChanged(static_cast<double>(frame.width()) /
                                static_cast<double>(frame.height()));
    }
}

void CameraPreviewWidget::applySelectedFormat()
{
    if (!m_camera) {
        return;
    }

    QCameraFormat format = findFormatById(m_selectedFormatId);
    if (format.isNull()) {
        QCameraFormat fallback = chooseDefaultFormat();
        if (!fallback.isNull()) {
            const QString fallbackId = formatIdFor(fallback);
            if (!fallbackId.isEmpty() && fallbackId != m_selectedFormatId) {
                m_selectedFormatId = fallbackId;
                setPreferredFormatId(fallbackId);
                emit preferredFormatChanged(m_selectedFormatId);
            }
            format = fallback;
        }
    }

    if (!format.isNull()) {
        m_isApplyingFormat = true;
        m_camera->setCameraFormat(format);
        m_isApplyingFormat = false;
        updateAspectRatioFromFormat(format);
    } else {
        updateAspectRatioFromFormat(m_camera->cameraFormat());
    }
}

void CameraPreviewWidget::onCameraError(QCamera::Error error)
{
    if (error == QCamera::NoError || !m_camera) {
        return;
    }

    const QString errorString = m_camera->errorString();
    emit previewFailed(errorString.isEmpty() ? tr("Unknown camera error") : errorString);

    stopPreview();
    updateStatus(tr("Camera error: %1").arg(errorString));
}

void CameraPreviewWidget::onCameraActiveChanged(bool active)
{
    if (!m_camera) {
        return;
    }

    if (active) {
        updateStatus(tr("Preview running (%1)")
                         .arg(describeFormat(m_camera->cameraFormat())));
        emit previewStarted();
        updateAspectRatioFromFormat(m_camera->cameraFormat());
    } else if (m_previewEnabled) {
        updateStatus(tr("Preview paused"));
    }
}

void CameraPreviewWidget::onFormatSelectionChanged(int index)
{
    if (!m_formatCombo || index < 0) {
        return;
    }

    if (m_isApplyingFormat) {
        return;
    }

    QString id = m_formatCombo->itemData(index).toString();

    if (id == m_selectedFormatId) {
        return;
    }

    m_selectedFormatId = id;
    emit preferredFormatChanged(m_selectedFormatId);

    if (m_previewEnabled) {
        const bool wasEnabled = m_previewEnabled;
        stopPreview();
        if (wasEnabled) {
            startPreview();
        }
    }
}

QCameraFormat CameraPreviewWidget::findFormatById(const QString &id) const
{
    if (id.isEmpty()) {
        return QCameraFormat();
    }

    for (const QCameraFormat &format : m_availableFormats) {
        if (formatIdFor(format) == id) {
            return format;
        }
    }
    return QCameraFormat();
}

QCameraDevice CameraPreviewWidget::resolveCameraDevice() const
{
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty()) {
        return QCameraDevice();
    }

    auto matchesDeviceId = [this](const QCameraDevice &device) -> bool {
        if (m_requestedDeviceId.isEmpty()) {
            return false;
        }

        const QString idString = QString::fromUtf8(device.id());
        if (idString == m_requestedDeviceId) {
            return true;
        }

        if (idString.contains(m_requestedDeviceId, Qt::CaseInsensitive)) {
            return true;
        }

        const QString description = device.description();
        if (description.contains(m_requestedDeviceId, Qt::CaseInsensitive)) {
            return true;
        }

        return false;
    };

    for (const QCameraDevice &device : cameras) {
        if (matchesDeviceId(device)) {
            return device;
        }
    }

    for (const QCameraDevice &device : cameras) {
        if (device.description().contains(QStringLiteral("OBSBOT"), Qt::CaseInsensitive) ||
            device.description().contains(QStringLiteral("Meet"), Qt::CaseInsensitive)) {
            return device;
        }
    }

    return cameras.first();
}

void CameraPreviewWidget::refreshFormatOptions(const QCameraDevice &device)
{
    if (!m_formatCombo) {
        return;
    }

    QList<QCameraFormat> mjpegFormats;
    QList<QCameraFormat> otherFormats;
    const QList<QCameraFormat> formats = device.videoFormats();

    for (const QCameraFormat &format : formats) {
        if (isJpegFormat(format)) {
            mjpegFormats.append(format);
        } else {
            otherFormats.append(format);
        }
    }

    m_availableFormats = !mjpegFormats.isEmpty() ? mjpegFormats : otherFormats;

    std::sort(m_availableFormats.begin(), m_availableFormats.end(),
              [](const QCameraFormat &a, const QCameraFormat &b) {
                  const QSize aSize = a.resolution();
                  const QSize bSize = b.resolution();
                  const int aPixels = aSize.width() * aSize.height();
                  const int bPixels = bSize.width() * bSize.height();
                  if (aPixels == bPixels) {
                      return a.maxFrameRate() > b.maxFrameRate();
                  }
                  return aPixels > bPixels;
              });

    QSignalBlocker blocker(m_formatCombo);
    m_formatCombo->clear();

    const QString previousSelection = m_selectedFormatId;
    bool updatedSelection = false;

    QStringList seen;
    for (const QCameraFormat &format : m_availableFormats) {
        const QString id = formatIdFor(format);
        if (id.isEmpty() || seen.contains(id)) {
            continue;
        }
        seen.append(id);

        QString label = describeFormat(format);
        if (isJpegFormat(format)) {
            label += tr(" (MJPEG)");
        }
        m_formatCombo->addItem(label, id);
    }

    int index = -1;
    if (!m_selectedFormatId.isEmpty()) {
        index = m_formatCombo->findData(m_selectedFormatId);
    }
    if (index < 0) {
        const QCameraFormat defaultFormat = chooseDefaultFormat();
        const QString defaultId = formatIdFor(defaultFormat);
        if (!defaultId.isEmpty()) {
            index = m_formatCombo->findData(defaultId);
            if (index >= 0) {
                m_selectedFormatId = defaultId;
                updatedSelection = (previousSelection != m_selectedFormatId);
            }
        }
    }
    if (index < 0 && m_formatCombo->count() > 0) {
        index = 0;
        m_selectedFormatId = m_formatCombo->itemData(index).toString();
        updatedSelection = (previousSelection != m_selectedFormatId);
    }
    if (index >= 0) {
        m_formatCombo->setCurrentIndex(index);
    }

    if (updatedSelection) {
        emit preferredFormatChanged(m_selectedFormatId);
    }
}

void CameraPreviewWidget::updateAspectRatioFromFormat(const QCameraFormat &format)
{
    const QSize resolution = format.resolution();
    if (!resolution.isValid()) {
        emit aspectRatioChanged(16.0 / 9.0);
        return;
    }

    if (resolution.height() == 0) {
        emit aspectRatioChanged(16.0 / 9.0);
        return;
    }

    const double ratio = static_cast<double>(resolution.width()) /
                         static_cast<double>(resolution.height());
    emit aspectRatioChanged(ratio);
}

void CameraPreviewWidget::updateStatus(const QString &message)
{
    if (m_statusLabel) {
        m_statusLabel->setText(message);
    }
}

QCameraFormat CameraPreviewWidget::selectBestFallbackFormat() const
{
    for (const QCameraFormat &format : m_availableFormats) {
        if (format.isNull()) {
            continue;
        }
        return format;
    }

    if (!m_availableFormats.isEmpty()) {
        return m_availableFormats.first();
    }

    return QCameraFormat();
}

QCameraFormat CameraPreviewWidget::chooseDefaultFormat() const
{
    QCameraFormat best;
    int bestArea = std::numeric_limits<int>::max();

    for (const QCameraFormat &format : m_availableFormats) {
        const QSize res = format.resolution();
        if (!res.isValid()) {
            continue;
        }

        const int area = res.width() * res.height();
        if (res.width() >= 1920 && res.height() >= 1080) {
            if (area < bestArea) {
                best = format;
                bestArea = area;
            }
            if (res.width() == 1920 && res.height() == 1080) {
                return format;
            }
        }
    }

    if (!best.isNull()) {
        return best;
    }

    return selectBestFallbackFormat();
}

bool CameraPreviewWidget::isJpegFormat(const QCameraFormat &format) const
{
    return format.pixelFormat() == QVideoFrameFormat::Format_Jpeg;
}
