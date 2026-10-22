#include "CameraController.h"
#include "ShutterSpeed.h"
#include <QThread>
#include <QDebug>
#include <algorithm>

static_assert(ShutterSpeed::kFirstIndex == Device::DevShutterTime_1_8000 &&
              ShutterSpeed::kDefaultIndex == Device::DevShutterTime_1_30 &&
              ShutterSpeed::kLastIndex == Device::DevShutterTime_1_2,
              "ShutterSpeed indices must match the SDK's DevShutterTimeType");

static constexpr int kDefaultWhiteBalanceKelvin = 4800;
// Measured on a Meet 2: ai_mode reflects a disable after 1.0-1.55s. Generous margin on top.
static constexpr int kAiModeSwitchTimeoutMs = 4000;
// Sentinel for expectAiMode(): accept any mode other than AiWorkModeNone.
static constexpr int kAnyActiveAiMode = -1;

CameraController::CameraController(QObject *parent)
    : QObject(parent)
    , m_connected(false)
    , m_settlingTimer(nullptr)
{
    m_currentState = {};
    m_cachedState = {};
    m_currentState.autoFocusEnabled = true;
    m_cachedState.autoFocusEnabled = true;
    m_currentState.whiteBalanceKelvin = 5000;
    m_cachedState.whiteBalanceKelvin = 5000;
    m_currentState.exposureAuto = true;
    m_currentState.exposure = ShutterSpeed::kDefaultIndex;
    m_currentState.iso = 1;
    m_cachedState.exposureAuto = true;
    m_cachedState.exposure = ShutterSpeed::kDefaultIndex;
    m_cachedState.iso = 1;
    m_lastRequestedWhiteBalance = static_cast<int>(Device::DevWhiteBalanceAuto);
    m_whiteBalanceFallbackActive = false;
    m_fallbackWhiteBalanceMode = static_cast<int>(Device::DevWhiteBalanceAuto);

    // Create settling timer
    m_settlingTimer = new QTimer(this);
    m_settlingTimer->setSingleShot(true);

    resetControlRanges();
}

CameraController::~CameraController()
{
}

void CameraController::connectToCamera()
{
    connectToCamera(QString());
}

void CameraController::connectToCamera(const QString &devicePath)
{
    m_selectedDevicePath = devicePath;

    auto pickDevice = [this](const std::list<std::shared_ptr<Device>> &list)
        -> std::shared_ptr<Device>
    {
        if (list.empty()) return nullptr;
        if (m_selectedDevicePath.isEmpty()) return list.front();

        for (const auto &dev : list) {
            if (QString::fromStdString(dev->videoDevPath()) == m_selectedDevicePath)
                return dev;
        }
        qDebug() << "CameraController: device" << m_selectedDevicePath
                 << "not found in SDK list, using first available";
        return list.front();
    };

    auto onDevChanged = [this, pickDevice](std::string /*dev_sn*/, bool connected, void * /*param*/) {
        // The SDK calls this from its discovery thread. Handle the change on the controller's
        // thread instead, so it is serialized with the V4L2 fallback, camera commands and UI reads.
        QMetaObject::invokeMethod(this, [this, pickDevice, connected]() {
            if (connected) {
                auto dev_list = Devices::get().getDevList();
                auto dev = pickDevice(dev_list);
                if (dev) {
                    // The SDK path is strictly more capable than the V4L2 fallback and always wins
                    if (m_v4l2Only) {
                        m_v4l2.close();
                    }
                    if (m_v4l2ScanTimer) {
                        m_v4l2ScanTimer->stop();
                    }
                    m_device = dev;
                    m_connected = true;
                    m_v4l2Only = false;
                    m_cameraInfo.name = QString::fromStdString(m_device->devName());
                    m_cameraInfo.serialNumber = QString::fromStdString(m_device->devSn());
                    m_cameraInfo.version = QString::fromStdString(m_device->devVersion());
                    m_cameraInfo.productType = m_device->productType();
                    m_cameraInfo.connected = true;
                    refreshControlRanges();
                    emit cameraConnected(m_cameraInfo);
                    updateState();
                }
            } else {
                if (m_v4l2Only) {
                    // SDK discovery keeps running alongside a V4L2-only connection; an SDK
                    // "device gone" event does not concern the V4L2 handle we actually use.
                    return;
                }
                m_connected = false;
                m_cameraInfo.connected = false;
                resetControlRanges();
                emit cameraDisconnected();
            }
        }, Qt::QueuedConnection);
    };

    Devices::get().setDevChangedCallback(onDevChanged, nullptr);
    Devices::get().setEnableMdnsScan(false);

    auto dev_list = Devices::get().getDevList();
    if (!dev_list.empty() && !m_connected) {
        auto dev = pickDevice(dev_list);
        if (dev) {
            m_device = dev;
            m_connected = true;
            m_cameraInfo.name = QString::fromStdString(m_device->devName());
            m_cameraInfo.serialNumber = QString::fromStdString(m_device->devSn());
            m_cameraInfo.version = QString::fromStdString(m_device->devVersion());
            m_cameraInfo.productType = m_device->productType();
            m_cameraInfo.connected = true;
            refreshControlRanges();
            emit cameraConnected(m_cameraInfo);
            updateState();
        }
    } else if (!m_connected) {
        tryV4l2Fallback();
    }
}

void CameraController::tryV4l2Fallback()
{
    if (m_connected) {
        // The SDK may have connected first; don't downgrade to V4L2-only.
        return;
    }

    auto path = V4l2Backend::findObsbotDevice();
    if (!path.empty()) {
        connectV4l2(path);
        return;
    }

    if (!m_v4l2ScanTimer) {
        m_v4l2ScanTimer = new QTimer(this);
        m_v4l2ScanTimer->setSingleShot(false);
        connect(m_v4l2ScanTimer, &QTimer::timeout, this, [this]() {
            if (m_connected)
                return;
            auto path = V4l2Backend::findObsbotDevice();
            if (!path.empty()) {
                m_v4l2ScanTimer->stop();
                connectV4l2(path);
            }
        });
    }
    m_v4l2ScanTimer->start(3000);
}

void CameraController::connectV4l2(const std::string &devicePath)
{
    if (m_connected) {
        // The SDK already connected (e.g. while we were scanning /dev); don't downgrade.
        return;
    }

    if (!m_v4l2.open(devicePath))
        return;

    // Deliberately leave SDK discovery (Devices::get()) running. Closing it here would let
    // the SDK's callback still hand us a Device whose status thread is gone: commands keep
    // succeeding but cameraStatus() never updates again, so the UI would fight a frozen
    // snapshot forever. Keeping discovery alive also lets the SDK upgrade this V4L2-only
    // connection to a full one if it finds the camera later. Its callback runs on this
    // thread, so that upgrade can only happen after this method has finished.
    m_connected = true;
    m_v4l2Only = true;
    m_v4l2DevicePath = QString::fromStdString(devicePath);

    std::string card = m_v4l2.cardName();
    auto colon = card.find(':');
    if (colon != std::string::npos)
        card = card.substr(0, colon);
    if (card.empty())
        card = "OBSBOT";
    m_cameraInfo.name = QString::fromStdString(card) + QStringLiteral(" (V4L2)");
    m_cameraInfo.serialNumber = QString();
    m_cameraInfo.version = QString();
    m_cameraInfo.productType = -1;
    m_cameraInfo.connected = true;

    refreshV4l2ControlRanges();

    m_v4l2.setWhiteBalanceAuto(false);
    m_v4l2.setWhiteBalanceTemperature(kDefaultWhiteBalanceKelvin);

    emit cameraConnected(m_cameraInfo);
    updateV4l2State();
}

void CameraController::refreshV4l2ControlRanges()
{
    auto convert = [](V4l2Backend::ControlRange r) -> ParamRange {
        return {r.min, r.max, r.step, r.defaultValue, r.valid};
    };

    // V4L2 counts exposure in 100 µs units; offer the shutter speeds that fit in its range
    auto shutterRange = [](V4l2Backend::ControlRange r) -> ParamRange {
        ParamRange range;
        if (!r.valid)
            return range;
        for (int index = ShutterSpeed::kFirstIndex; index <= ShutterSpeed::kLastIndex; ++index) {
            const int exposure = ShutterSpeed::toV4l2Exposure(index);
            if (exposure < r.min || exposure > r.max)
                continue;
            if (!range.valid)
                range.min = index;
            range.max = index;
            range.valid = true;
        }
        if (range.valid)
            range.defaultValue = std::clamp(ShutterSpeed::fromV4l2Exposure(r.defaultValue), range.min, range.max);
        return range;
    };

    m_brightnessRange = convert(m_v4l2.getBrightnessRange());
    m_contrastRange = convert(m_v4l2.getContrastRange());
    m_saturationRange = convert(m_v4l2.getSaturationRange());
    m_whiteBalanceKelvinRange = convert(m_v4l2.getWhiteBalanceTemperatureRange());
    m_exposureRange = shutterRange(m_v4l2.getExposureAbsoluteRange());
    m_isoRange = convert(m_v4l2.getGainRange());

    m_supportedWhiteBalanceTypes.clear();
    m_supportedWhiteBalanceTypes.push_back(static_cast<int>(Device::DevWhiteBalanceAuto));
    m_supportedWhiteBalanceTypes.push_back(static_cast<int>(Device::DevWhiteBalanceManual));
}

void CameraController::updateV4l2State()
{
    if (!m_v4l2.isOpen())
        return;

    m_currentState.brightness = m_v4l2.getBrightness();
    m_currentState.contrast = m_v4l2.getContrast();
    m_currentState.saturation = m_v4l2.getSaturation();

    bool autoWb = m_v4l2.getWhiteBalanceAuto();
    m_currentState.whiteBalance = autoWb
        ? static_cast<int>(Device::DevWhiteBalanceAuto)
        : static_cast<int>(Device::DevWhiteBalanceManual);
    m_currentState.whiteBalanceKelvin = m_v4l2.getWhiteBalanceTemperature();

    auto zoomRange = m_v4l2.getZoomRange();
    int zoomMax = zoomRange.valid ? zoomRange.max : 100;
    int zoomRaw = m_v4l2.getZoomAbsolute();
    m_currentState.zoom = (zoomMax > 0) ? (static_cast<double>(zoomRaw) / zoomMax + 1.0) : 1.0;

    auto panRange = m_v4l2.getPanRange();
    auto tiltRange = m_v4l2.getTiltRange();
    if (panRange.valid && panRange.max != 0)
        m_currentState.pan = static_cast<double>(m_v4l2.getPanAbsolute()) / panRange.max;
    if (tiltRange.valid && tiltRange.max != 0)
        m_currentState.tilt = static_cast<double>(m_v4l2.getTiltAbsolute()) / tiltRange.max;

    m_currentState.autoFocusEnabled = m_v4l2.getAutoFocus();
    m_currentState.manualFocusValue = m_v4l2.getFocusAbsolute();

    m_currentState.exposureAuto = m_v4l2.getAutoExposure();
    // Like the SDK path: in auto mode the exposure is the camera's own choice, not the manual speed
    const int exposure = m_currentState.exposureAuto ? 0 : m_v4l2.getExposureAbsolute();
    if (exposure > 0) {
        m_currentState.exposure = clampToRange(ShutterSpeed::fromV4l2Exposure(exposure, m_currentState.exposure),
                                               m_exposureRange, ShutterSpeed::kFirstIndex, ShutterSpeed::kLastIndex);
    }
    if (m_isoRange.valid) {
        m_currentState.iso = clampToRange(m_v4l2.getGain(), m_isoRange, 1, 64);
    }

    emit stateChanged(m_currentState);
}

void CameraController::disconnectFromCamera()
{
    if (m_connected) {
        if (m_v4l2Only) {
            m_v4l2.close();
            m_v4l2Only = false;
        } else {
            m_device.reset();
        }
        m_connected = false;
        m_cameraInfo.connected = false;
        resetControlRanges();

        emit cameraDisconnected();
    }
}

QString CameraController::getVideoDevicePath() const
{
    if (m_v4l2Only)
        return m_v4l2DevicePath;
    if (m_connected && m_device)
        return QString::fromStdString(m_device->videoDevPath());
    return QString();
}

QMap<QString, QString> CameraController::getSerialsByDevicePath() const
{
    QMap<QString, QString> result;
    const auto dev_list = Devices::get().getDevList();
    for (const auto &dev : dev_list) {
        const QString path = QString::fromStdString(dev->videoDevPath());
        const QString serial = QString::fromStdString(dev->devSn());
        if (!path.isEmpty() && !serial.isEmpty())
            result.insert(path, serial);
    }
    return result;
}

CameraController::CameraState CameraController::getCurrentState()
{
    if (m_connected && !isSettling()) {
        if (m_v4l2Only)
            updateV4l2State();
        else
            updateState();
    }
    return isSettling() ? m_cachedState : m_currentState;
}

bool CameraController::hasTiny2Capabilities() const
{
    return isTiny2Family();
}

bool CameraController::enableAutoFraming(bool enabled)
{
    if (!m_connected || m_v4l2Only) return false;

    // Invalidate a still-pending delayed "set AutoFraming mode" from an earlier call so it
    // cannot re-enable tracking after this call turned it off (or fire twice).
    const int generation = ++m_autoFramingGeneration;

    if (isTiny2Family()) {
        // MediaMode/AutoFraming are Meet-series commands; Tiny 2 is driven by its AI mode alone.
        if (!enabled) {
            return setAiMode(static_cast<int>(Device::AiWorkModeNone), 0);
        }
        int mode = m_currentState.aiMode;
        if (mode == static_cast<int>(Device::AiWorkModeNone)) {
            mode = static_cast<int>(Device::AiWorkModeHuman);
        }
        const int subMode = (mode == static_cast<int>(Device::AiWorkModeHuman)) ? m_currentState.aiSubMode : 0;
        return setAiMode(mode, subMode);
    }

    if (enabled) {
        // Step 1: Set MediaMode to AutoFrame
        if (!executeCommand("Set MediaMode to AutoFrame", [this]() {
            return m_device->cameraSetMediaModeU(Device::MediaModeAutoFrame);
        })) {
            return false;
        }

        // Step 2: Set auto-framing mode after a brief delay (non-blocking). The camera only
        // starts tracking once this arrives, so skip it if tracking was toggled again meanwhile.
        QTimer::singleShot(500, this, [this, generation]() {
            if (generation != m_autoFramingGeneration || !m_connected || !m_device) return;
            executeCommand("Set AutoFraming mode", [this]() {
                return m_device->cameraSetAutoFramingModeU(Device::AutoFrmSingle, Device::AutoFrmUpperBody);
            });
        });

        expectAiMode(kAnyActiveAiMode);
        m_currentState.autoFramingEnabled = true;
        emit stateChanged(m_currentState);
        return true;  // First command succeeded, second is pending
    } else {
        bool success = executeCommand("Disable AutoFraming", [this]() {
            return m_device->cameraSetMediaModeU(Device::MediaModeNormal);
        });
        if (success) {
            expectAiMode(static_cast<int>(Device::AiWorkModeNone));
            m_currentState.autoFramingEnabled = false;
            m_currentState.aiMode = static_cast<int>(Device::AiWorkModeNone);
            emit stateChanged(m_currentState);
        }
        return success;
    }
}

void CameraController::expectAiMode(int aiMode)
{
    m_pendingAiMode = aiMode;
    m_pendingAiDeadline.setRemainingTime(kAiModeSwitchTimeoutMs);
}

bool CameraController::aiModeMatchesPending(int reportedAiMode) const
{
    if (m_pendingAiMode == kAnyActiveAiMode) {
        return reportedAiMode != Device::AiWorkModeNone;
    }
    return reportedAiMode == m_pendingAiMode;
}

bool CameraController::setAiMode(int mode, int subMode)
{
    if (!m_connected || m_v4l2Only) return false;

    auto workMode = static_cast<Device::AiWorkModeType>(mode);
    bool success = executeCommand("Set AI Mode", [this, workMode, subMode]() {
        return m_device->cameraSetAiModeU(workMode, subMode);
    });

    if (success) {
        expectAiMode(mode);
        m_currentState.aiMode = mode;
        m_currentState.aiSubMode = subMode;
        m_currentState.autoFramingEnabled = (mode != Device::AiWorkModeNone);
        emit stateChanged(m_currentState);
    }

    return success;
}

bool CameraController::setAutoZoom(bool enabled)
{
    if (!m_connected || m_v4l2Only) return false;

    bool success = executeCommand(enabled ? "Enable Auto Zoom" : "Disable Auto Zoom", [this, enabled]() {
        return m_device->aiSetAiAutoZoomR(enabled);
    });

    if (success) {
        m_currentState.autoZoomEnabled = enabled;
        emit stateChanged(m_currentState);
    }

    return success;
}

bool CameraController::setTrackSpeed(int speedMode)
{
    if (!m_connected || m_v4l2Only) return false;

    auto speed = static_cast<Device::AiTrackSpeedType>(speedMode);
    bool success = executeCommand("Set Tracking Speed", [this, speed]() {
        return m_device->aiSetTrackSpeedTypeR(speed);
    });

    if (success) {
        m_currentState.trackSpeedMode = speedMode;
        emit stateChanged(m_currentState);
    }

    return success;
}

bool CameraController::setAudioAutoGain(bool enabled)
{
    if (!m_connected || m_v4l2Only) return false;

    bool success = executeCommand(enabled ? "Enable Audio Auto Gain" : "Disable Audio Auto Gain", [this, enabled]() {
        return m_device->cameraSetAudioAutoGainU(enabled);
    });

    if (success) {
        m_currentState.audioAutoGainEnabled = enabled;
        emit stateChanged(m_currentState);
    }

    return success;
}

bool CameraController::setPanTilt(double pan, double tilt)
{
    if (!m_connected) return false;

    pan = qBound(-1.0, pan, 1.0);
    tilt = qBound(-1.0, tilt, 1.0);

    bool success;
    if (m_v4l2Only) {
        auto panRange = m_v4l2.getPanRange();
        auto tiltRange = m_v4l2.getTiltRange();
        int panVal = static_cast<int>(pan * panRange.max);
        int tiltVal = static_cast<int>(tilt * tiltRange.max);
        success = m_v4l2.setPanAbsolute(panVal) && m_v4l2.setTiltAbsolute(tiltVal);
    } else {
        success = executeCommand("Set Pan/Tilt", [this, pan, tilt]() {
            return m_device->cameraSetPanTiltAbsolute(pan, tilt);
        });
    }

    if (success) {
        m_currentState.pan = pan;
        m_currentState.tilt = tilt;
        emit stateChanged(m_currentState);
    }

    return success;
}

bool CameraController::adjustPan(double delta)
{
    double newPan = m_currentState.pan + delta;
    return setPanTilt(newPan, m_currentState.tilt);
}

bool CameraController::adjustTilt(double delta)
{
    double newTilt = m_currentState.tilt + delta;
    return setPanTilt(m_currentState.pan, newTilt);
}

bool CameraController::setZoom(double zoom)
{
    if (!m_connected) return false;

    zoom = qBound(1.0, zoom, 2.0);

    bool success;
    if (m_v4l2Only) {
        auto zoomRange = m_v4l2.getZoomRange();
        int zoomMax = zoomRange.valid ? zoomRange.max : 100;
        int v4l2Zoom = static_cast<int>((zoom - 1.0) * zoomMax);
        success = m_v4l2.setZoomAbsolute(v4l2Zoom);
    } else {
        uint32_t zoomRatio = static_cast<uint32_t>(zoom * 100);
        success = executeCommand("Set Zoom", [this, zoomRatio]() {
            return m_device->cameraSetZoomWithSpeedAbsoluteR(zoomRatio, 255);
        });
    }

    if (success) {
        m_currentState.zoom = zoom;
        emit stateChanged(m_currentState);
    }

    return success;
}

bool CameraController::centerView()
{
    return setPanTilt(0.0, 0.0);
}

bool CameraController::setHDR(bool enabled)
{
    if (!m_connected || m_v4l2Only) return false;

    return executeCommand(enabled ? "Enable HDR" : "Disable HDR", [this, enabled]() {
        return m_device->cameraSetWdrR(enabled ? Device::DevWdrModeDol2TO1 : Device::DevWdrModeNone);
    });
}

bool CameraController::setFOV(int fovMode)
{
    if (!m_connected || m_v4l2Only) return false;

    Device::FovType fov;
    switch (fovMode) {
        case 0: fov = Device::FovType86; break;
        case 1: fov = Device::FovType78; break;
        case 2: fov = Device::FovType65; break;
        default: return false;
    }

    return executeCommand("Set FOV", [this, fov]() {
        return m_device->cameraSetFovU(fov);
    });
}

bool CameraController::setFaceAE(bool enabled)
{
    if (!m_connected || m_v4l2Only) return false;

    return executeCommand(enabled ? "Enable Face AE" : "Disable Face AE", [this, enabled]() {
        return m_device->cameraSetFaceAER(enabled);
    });
}

bool CameraController::setFaceFocus(bool enabled)
{
    if (!m_connected || m_v4l2Only) return false;

    return executeCommand(enabled ? "Enable Face Focus" : "Disable Face Focus", [this, enabled]() {
        return m_device->cameraSetFaceFocusR(enabled);
    });
}

bool CameraController::setFocusAbsolute(int position, bool autoFocus)
{
    if (!m_connected) return false;
    position = qBound(0, position, 100);

    bool success;
    if (m_v4l2Only) {
        success = m_v4l2.setAutoFocus(autoFocus) &&
                  (autoFocus || m_v4l2.setFocusAbsolute(position));
        if (!success) {
            emit commandFailed("Set Focus", -1);
        }
    } else {
        success = executeCommand("Set Focus", [this, position, autoFocus]() {
            return m_device->cameraSetFocusAbsolute(position, autoFocus);
        });
    }
    if (success) {
        m_currentState.autoFocusEnabled = autoFocus;
        if (!autoFocus) {
            m_currentState.manualFocusValue = position;
        }
        if (isSettling()) {
            m_cachedState.autoFocusEnabled = m_currentState.autoFocusEnabled;
            m_cachedState.manualFocusValue = m_currentState.manualFocusValue;
        }
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setBrightness(int value)
{
    if (!m_connected) return false;
    if (m_currentState.brightnessAuto) return true;

    int clamped = clampToRange(value, m_brightnessRange, 0, 255);
    bool success;
    if (m_v4l2Only) {
        success = m_v4l2.setBrightness(clamped);
    } else {
        success = executeCommand("Set Brightness", [this, clamped]() {
            return m_device->cameraSetImageBrightnessR(clamped);
        });
    }
    if (success) {
        m_currentState.brightness = clamped;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setContrast(int value)
{
    if (!m_connected) return false;
    if (m_currentState.contrastAuto) return true;

    int clamped = clampToRange(value, m_contrastRange, 0, 255);
    bool success;
    if (m_v4l2Only) {
        success = m_v4l2.setContrast(clamped);
    } else {
        success = executeCommand("Set Contrast", [this, clamped]() {
            return m_device->cameraSetImageContrastR(clamped);
        });
    }
    if (success) {
        m_currentState.contrast = clamped;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setSaturation(int value)
{
    if (!m_connected) return false;
    if (m_currentState.saturationAuto) return true;

    int clamped = clampToRange(value, m_saturationRange, 0, 255);
    bool success;
    if (m_v4l2Only) {
        success = m_v4l2.setSaturation(clamped);
    } else {
        success = executeCommand("Set Saturation", [this, clamped]() {
            return m_device->cameraSetImageSaturationR(clamped);
        });
    }
    if (success) {
        m_currentState.saturation = clamped;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setWhiteBalance(int mode)
{
    if (!m_connected) return false;

    if (m_v4l2Only) {
        if (mode == static_cast<int>(Device::DevWhiteBalanceAuto)) {
            m_v4l2.setWhiteBalanceAuto(true);
            m_currentState.whiteBalance = mode;
            emit stateChanged(m_currentState);
            return true;
        }
        if (mode == static_cast<int>(Device::DevWhiteBalanceManual)) {
            m_v4l2.setWhiteBalanceAuto(false);
            m_v4l2.setWhiteBalanceTemperature(m_currentState.whiteBalanceKelvin);
            m_currentState.whiteBalance = mode;
            emit stateChanged(m_currentState);
            return true;
        }
        int kelvin = whiteBalancePresetToKelvin(mode);
        if (kelvin > 0) {
            m_v4l2.setWhiteBalanceAuto(false);
            m_v4l2.setWhiteBalanceTemperature(kelvin);
            m_currentState.whiteBalance = mode;
            m_currentState.whiteBalanceKelvin = kelvin;
            emit stateChanged(m_currentState);
            return true;
        }
        return false;
    }

    m_lastRequestedWhiteBalance = mode;

    if (mode == static_cast<int>(Device::DevWhiteBalanceManual)) {
        m_whiteBalanceFallbackActive = false;
        m_fallbackWhiteBalanceMode = mode;
        return applyManualWhiteBalance(m_currentState.whiteBalanceKelvin, mode);
    }

    if (mode == static_cast<int>(Device::DevWhiteBalanceAuto)) {
        m_whiteBalanceFallbackActive = false;
        m_fallbackWhiteBalanceMode = mode;
        bool success = executeCommand("Set White Balance", [this]() {
            return m_device->cameraSetWhiteBalanceR(Device::DevWhiteBalanceAuto, 0);
        });
        if (success) {
            m_currentState.whiteBalance = mode;
            if (m_whiteBalanceKelvinRange.valid) {
                m_currentState.whiteBalanceKelvin = clampToRange(m_whiteBalanceKelvinRange.defaultValue, m_whiteBalanceKelvinRange, 2000, 10000);
            }
            emit stateChanged(m_currentState);
        }
        return success;
    }

    auto wbType = static_cast<Device::DevWhiteBalanceType>(mode);
    bool attemptDirect = m_supportedWhiteBalanceTypes.empty() ||
        isWhiteBalanceTypeSupported(mode);
    bool success = false;

    if (attemptDirect) {
        success = executeCommand("Set White Balance", [this, wbType]() {
            return m_device->cameraSetWhiteBalanceR(wbType, 0);
        });

        if (success) {
            Device::DevWhiteBalanceType readType;
            int32_t readParam = 0;
            if (m_device->cameraGetWhiteBalanceR(readType, readParam) == 0 && readType == wbType) {
                m_whiteBalanceFallbackActive = false;
                m_fallbackWhiteBalanceMode = mode;
                m_currentState.whiteBalance = mode;
                if (m_whiteBalanceKelvinRange.valid) {
                    m_currentState.whiteBalanceKelvin = clampToRange(readParam, m_whiteBalanceKelvinRange, 2000, 10000);
                }
                emit stateChanged(m_currentState);
                return true;
            }
        }
    }

    int kelvin = whiteBalancePresetToKelvin(mode);
    if (kelvin > 0 && m_whiteBalanceKelvinRange.valid) {
        m_whiteBalanceFallbackActive = true;
        m_fallbackWhiteBalanceMode = mode;
        return applyManualWhiteBalance(kelvin, mode);
    }

    return success;
}
bool CameraController::setWhiteBalanceManual(int kelvin)
{
    if (!m_connected) return false;

    if (m_v4l2Only) {
        int clamped = clampToRange(kelvin, m_whiteBalanceKelvinRange, 2000, 10000);
        m_v4l2.setWhiteBalanceAuto(false);
        bool success = m_v4l2.setWhiteBalanceTemperature(clamped);
        if (success) {
            m_currentState.whiteBalance = static_cast<int>(Device::DevWhiteBalanceManual);
            m_currentState.whiteBalanceKelvin = clamped;
            emit stateChanged(m_currentState);
        }
        return success;
    }

    m_lastRequestedWhiteBalance = static_cast<int>(Device::DevWhiteBalanceManual);
    m_whiteBalanceFallbackActive = false;
    m_fallbackWhiteBalanceMode = static_cast<int>(Device::DevWhiteBalanceManual);
    return applyManualWhiteBalance(kelvin, static_cast<int>(Device::DevWhiteBalanceManual));
}

bool CameraController::setExposureAuto(bool enabled)
{
    if (!m_connected) return false;

    int clamped = clampToRange(m_currentState.exposure, m_exposureRange,
                               ShutterSpeed::kFirstIndex, ShutterSpeed::kLastIndex);
    bool success;
    if (m_v4l2Only) {
        success = m_v4l2.setAutoExposure(enabled);
        if (success && !enabled) {
            // Re-assert the last known shutter value when leaving auto mode
            success = m_v4l2.setExposureAbsolute(ShutterSpeed::toV4l2Exposure(clamped));
        }
    } else {
        success = executeCommand("Set Exposure Mode", [this, clamped, enabled]() {
            return m_device->cameraSetExposureAbsolute(clamped, enabled);
        });
    }
    if (success) {
        m_currentState.exposureAuto = enabled;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setExposure(int value)
{
    if (!m_connected) return false;
    if (m_currentState.exposureAuto) return true; // Manual value is ignored while auto exposure is active

    int clamped = clampToRange(value, m_exposureRange, ShutterSpeed::kFirstIndex, ShutterSpeed::kLastIndex);
    bool success;
    if (m_v4l2Only) {
        success = m_v4l2.setExposureAbsolute(ShutterSpeed::toV4l2Exposure(clamped));
    } else {
        success = executeCommand("Set Exposure", [this, clamped]() {
            return m_device->cameraSetExposureAbsolute(clamped, false);
        });
    }
    if (success) {
        m_currentState.exposure = clamped;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setISO(int value)
{
    // The SDK documents its ISO commands for the Tail Air only, so skip cameras that report no ISO range
    if (!m_connected || !m_isoRange.valid) return false;

    int clamped = clampToRange(value, m_isoRange, 1, 64);
    bool success;
    if (m_v4l2Only) {
        success = m_v4l2.setGain(clamped);
    } else {
        success = executeCommand("Set ISO", [this, clamped]() {
            return m_device->cameraSetMAEIsoR(clamped);
        });
    }
    if (success) {
        m_currentState.iso = clamped;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::executeCommand(const QString &description, std::function<int32_t()> command)
{
    int32_t ret = command();
    if (ret != 0) {
        emit commandFailed(description, ret);
        return false;
    }
    return true;
}

void CameraController::updateState()
{
    if (!m_connected) return;

    // Don't update from camera during settling period
    if (isSettling()) {
        return;
    }

    auto status = m_device->cameraStatus();

    const int reportedAiMode = status.tiny.ai_mode;
    if (!m_pendingAiDeadline.hasExpired() && aiModeMatchesPending(reportedAiMode)) {
        m_pendingAiDeadline.setPreciseRemainingTime(0); // Camera caught up with the request
    }
    if (m_pendingAiDeadline.hasExpired()) {
        m_currentState.aiMode = reportedAiMode;
        m_currentState.aiSubMode = status.tiny.ai_sub_mode;
        m_currentState.autoFramingEnabled = (reportedAiMode != Device::AiWorkModeNone);
    }
    // Otherwise the camera is still switching modes; keep reporting the requested state.
    m_currentState.zoomRatio = status.tiny.zoom_ratio;
    // Derive zoom float from zoom_ratio (100 = 1.0x, 200 = 2.0x)
    if (status.tiny.zoom_ratio >= 100 && status.tiny.zoom_ratio <= 200) {
        m_currentState.zoom = status.tiny.zoom_ratio / 100.0;
    } else {
        qDebug() << "CameraController: unexpected zoom_ratio" << status.tiny.zoom_ratio
                 << "— keeping previous zoom" << m_currentState.zoom;
    }
    m_currentState.hdrEnabled = status.tiny.hdr;
    m_currentState.faceAEEnabled = status.tiny.face_ae;
    m_currentState.faceFocusEnabled = status.tiny.face_auto_focus;
    m_currentState.autoFocusEnabled = status.tiny.auto_focus;
    m_currentState.manualFocusValue = status.tiny.manual_focus_value;
    m_currentState.fovMode = status.tiny.fov;
    m_currentState.devStatus = status.tiny.dev_status;
    m_currentState.trackSpeedMode = status.tiny.ai_tracker_speed;
    m_currentState.audioAutoGainEnabled = status.tiny.audio_auto_gain;

    // Image controls - read current values from camera
    // Note: Preserve auto mode flags - camera doesn't have concept of "auto" for these
    bool preservedBrightnessAuto = m_currentState.brightnessAuto;
    bool preservedContrastAuto = m_currentState.contrastAuto;
    bool preservedSaturationAuto = m_currentState.saturationAuto;

    int32_t brightness, contrast, saturation;
    Device::DevWhiteBalanceType wbType;
    int32_t wbParam;

    if (m_device->cameraGetImageBrightnessR(brightness) == 0) {
        m_currentState.brightness = clampToRange(brightness, m_brightnessRange, 0, 255);
    }
    if (m_device->cameraGetImageContrastR(contrast) == 0) {
        m_currentState.contrast = clampToRange(contrast, m_contrastRange, 0, 255);
    }
    if (m_device->cameraGetImageSaturationR(saturation) == 0) {
        m_currentState.saturation = clampToRange(saturation, m_saturationRange, 0, 255);
    }
    if (m_device->cameraGetWhiteBalanceR(wbType, wbParam) == 0) {
        m_currentState.whiteBalance = static_cast<int>(wbType);
        if (wbType == Device::DevWhiteBalanceManual) {
            m_currentState.whiteBalanceKelvin = clampToRange(wbParam, m_whiteBalanceKelvinRange, 2000, 10000);
        } else if (m_whiteBalanceKelvinRange.valid) {
            m_currentState.whiteBalanceKelvin = clampToRange(m_whiteBalanceKelvinRange.defaultValue, m_whiteBalanceKelvinRange, 2000, 10000);
        }
    }

    // Exposure/ISO have genuine hardware auto/manual state, so read it directly
    int32_t shutterValue;
    bool autoExposure;
    if (m_device->cameraGetExposureAbsolute(shutterValue, autoExposure) == 0) {
        m_currentState.exposureAuto = autoExposure;
        // The SDK ignores the shutter value in auto mode; keep the manual speed to restore when Auto is turned off
        if (!autoExposure) {
            m_currentState.exposure = clampToRange(shutterValue, m_exposureRange,
                                                   ShutterSpeed::kFirstIndex, ShutterSpeed::kLastIndex);
        }
    }
    int32_t isoValue;
    if (m_isoRange.valid && m_device->cameraGetMAEIsoR(isoValue) == 0) {
        m_currentState.iso = clampToRange(isoValue, m_isoRange, 1, 64);
    }

    // Restore auto mode flags (not stored in camera)
    m_currentState.brightnessAuto = preservedBrightnessAuto;
    m_currentState.contrastAuto = preservedContrastAuto;
    m_currentState.saturationAuto = preservedSaturationAuto;

    if (m_whiteBalanceFallbackActive) {
        m_currentState.whiteBalance = m_fallbackWhiteBalanceMode;
    } else {
        m_lastRequestedWhiteBalance = m_currentState.whiteBalance;
    }

    emit stateChanged(m_currentState);
}

void CameraController::beginSettling(int durationMs)
{
    // Cache the current intended state
    m_cachedState = m_currentState;
    m_settlingTimer->start(durationMs);
}

bool CameraController::loadConfig(std::vector<Config::ValidationError> &errors)
{
    return m_config.load(errors);
}

bool CameraController::saveConfig()
{
    // Update config with current camera state before saving. Without a camera there is no state
    // to save (m_currentState holds defaults until one connects), so keep the saved camera settings.
    if (m_connected) {
        saveCurrentStateToConfig();
    }
    return m_config.save();
}

void CameraController::applyConfigToCamera()
{
    if (!m_connected) return;

    auto settings = m_config.getSettings();

    // Initialize auto mode flags from config
    m_currentState.brightnessAuto = settings.brightnessAuto;
    m_currentState.contrastAuto = settings.contrastAuto;
    m_currentState.saturationAuto = settings.saturationAuto;
    m_currentState.exposure = settings.exposure;
    m_currentState.iso = settings.iso;

    // Apply all settings to the camera
    if (isTiny2Family()) {
        // The AI mode is the single source of truth on Tiny 2; faceTracking only gates it
        int mode = settings.faceTracking ? settings.aiMode : static_cast<int>(Device::AiWorkModeNone);
        if (settings.faceTracking && mode == static_cast<int>(Device::AiWorkModeNone)) {
            mode = static_cast<int>(Device::AiWorkModeHuman);
        }
        setAiMode(mode, mode == static_cast<int>(Device::AiWorkModeHuman) ? settings.aiSubMode : 0);
    } else {
        enableAutoFraming(settings.faceTracking);
    }
    setHDR(settings.hdr);
    setFOV(settings.fov);
    setFaceAE(settings.faceAE);
    setFaceFocus(settings.faceFocus);
    setZoom(settings.zoom);
    setPanTilt(settings.pan, settings.tilt);
    if (settings.focus >= 0) {
        setFocusAbsolute(settings.focus, false);
    } else {
        setFocusAbsolute(0, true);
    }

    if (isTiny2Family()) {
        setAutoZoom(settings.autoZoom);
        setTrackSpeed(settings.trackSpeed);
        setAudioAutoGain(settings.audioAutoGain);
    }

    // Image controls
    setBrightness(settings.brightness);
    setContrast(settings.contrast);
    setSaturation(settings.saturation);
    if (settings.whiteBalance == static_cast<int>(Device::DevWhiteBalanceManual)) {
        setWhiteBalanceManual(settings.whiteBalanceKelvin);
    } else {
        setWhiteBalance(settings.whiteBalance);
    }
    setExposureAuto(settings.exposureAuto);
    setISO(settings.iso);

    emit configLoaded();
}

void CameraController::applyCurrentStateToCamera(const CameraState &uiState)
{
    if (!m_connected) return;

    // Update current state with UI state (including auto mode flags)
    m_currentState.brightnessAuto = uiState.brightnessAuto;
    m_currentState.contrastAuto = uiState.contrastAuto;
    m_currentState.saturationAuto = uiState.saturationAuto;
    m_currentState.exposure = uiState.exposure;
    m_currentState.iso = uiState.iso;

    // Cache the intended state
    m_cachedState = uiState;

    // Begin settling period - block status updates for 2 seconds
    beginSettling(2000);

    // Apply the current UI state to camera (respects user changes)
    if (isTiny2Family()) {
        int mode = uiState.autoFramingEnabled ? uiState.aiMode : static_cast<int>(Device::AiWorkModeNone);
        if (uiState.autoFramingEnabled && mode == static_cast<int>(Device::AiWorkModeNone)) {
            mode = static_cast<int>(Device::AiWorkModeHuman);
        }
        setAiMode(mode, mode == static_cast<int>(Device::AiWorkModeHuman) ? uiState.aiSubMode : 0);
        setAutoZoom(uiState.autoZoomEnabled);
        setTrackSpeed(uiState.trackSpeedMode);
        setAudioAutoGain(uiState.audioAutoGainEnabled);
    } else {
        enableAutoFraming(uiState.autoFramingEnabled);
    }
    setHDR(uiState.hdrEnabled);
    setFOV(uiState.fovMode);
    setFaceAE(uiState.faceAEEnabled);
    setFaceFocus(uiState.faceFocusEnabled);
    setZoom(uiState.zoom);
    setPanTilt(uiState.pan, uiState.tilt);
    setFocusAbsolute(uiState.manualFocusValue, uiState.autoFocusEnabled);

    // Image controls
    setBrightness(uiState.brightness);
    setContrast(uiState.contrast);
    setSaturation(uiState.saturation);
    if (uiState.whiteBalance == static_cast<int>(Device::DevWhiteBalanceManual)) {
        setWhiteBalanceManual(uiState.whiteBalanceKelvin);
    } else {
        setWhiteBalance(uiState.whiteBalance);
    }
    setExposureAuto(uiState.exposureAuto);
    setISO(uiState.iso);
}

void CameraController::saveCurrentStateToConfig()
{
    // Get current settings to preserve app settings (like startMinimized)
    Config::CameraSettings settings = m_config.getSettings();

    // Update only camera-related settings from current state
    settings.faceTracking = m_currentState.autoFramingEnabled;
    settings.hdr = m_currentState.hdrEnabled;
    settings.fov = m_currentState.fovMode;
    settings.faceAE = m_currentState.faceAEEnabled;
    settings.faceFocus = m_currentState.faceFocusEnabled;
    settings.zoom = qBound(1.0, m_currentState.zoom, 2.0);
    settings.pan = m_currentState.pan;
    settings.tilt = m_currentState.tilt;
    settings.aiMode = m_currentState.aiMode;
    settings.aiSubMode = m_currentState.aiSubMode;
    settings.autoZoom = m_currentState.autoZoomEnabled;
    settings.trackSpeed = m_currentState.trackSpeedMode;
    settings.audioAutoGain = m_currentState.audioAutoGainEnabled;

    // Image controls
    settings.brightnessAuto = m_currentState.brightnessAuto;
    settings.brightness = m_currentState.brightness;
    settings.contrastAuto = m_currentState.contrastAuto;
    settings.contrast = m_currentState.contrast;
    settings.saturationAuto = m_currentState.saturationAuto;
    settings.saturation = m_currentState.saturation;
    settings.whiteBalance = m_currentState.whiteBalance;
    settings.whiteBalanceKelvin = m_currentState.whiteBalanceKelvin;
    settings.focus = m_currentState.autoFocusEnabled ? -1 : m_currentState.manualFocusValue;
    settings.exposureAuto = m_currentState.exposureAuto;
    settings.exposure = m_currentState.exposure;
    settings.iso = m_currentState.iso;

    m_config.setSettings(settings);
}

bool CameraController::isTiny2Family() const
{
    return m_cameraInfo.productType == ObsbotProdTiny2 ||
           m_cameraInfo.productType == ObsbotProdTiny2Lite ||
           m_cameraInfo.productType == ObsbotProdTinySE;
}

void CameraController::refreshControlRanges()
{
    if (!m_device) {
        resetControlRanges();
        return;
    }

    auto fetchRange = [this](int32_t (Device::*getter)(Device::UvcParamRange &), ParamRange &target) {
        Device::UvcParamRange sdkRange{};
        if ((m_device.get()->*getter)(sdkRange) == 0) {
            target.min = sdkRange.min_;
            target.max = sdkRange.max_;
            target.step = sdkRange.step_ == 0 ? 1 : sdkRange.step_;
            target.defaultValue = sdkRange.default_;
            target.valid = true;
        } else {
            target = {};
        }
    };

    fetchRange(&Device::cameraGetRangeImageBrightnessR, m_brightnessRange);
    fetchRange(&Device::cameraGetRangeImageContrastR, m_contrastRange);
    fetchRange(&Device::cameraGetRangeImageSaturationR, m_saturationRange);
    fetchRange(&Device::cameraGetRangeWhiteBalanceR, m_whiteBalanceKelvinRange);
    fetchRange(&Device::cameraGetRangeExposureAbsolute, m_exposureRange);
    fetchRange(&Device::cameraGetRangeMAEIsoR, m_isoRange);

    m_supportedWhiteBalanceTypes.clear();
    std::vector<int32_t> wbList;
    int32_t wbMin = 0;
    int32_t wbMax = 0;
    if (m_device->cameraGetWhiteBalanceListR(wbList, wbMin, wbMax) == 0) {
        m_supportedWhiteBalanceTypes.assign(wbList.begin(), wbList.end());
    }

    if (m_whiteBalanceKelvinRange.valid) {
        int clampedCurrent = clampToRange(
            m_currentState.whiteBalanceKelvin == 0 ? m_whiteBalanceKelvinRange.defaultValue : m_currentState.whiteBalanceKelvin,
            m_whiteBalanceKelvinRange, 2000, 10000);
        m_currentState.whiteBalanceKelvin = clampedCurrent;
        m_cachedState.whiteBalanceKelvin = clampToRange(
            m_cachedState.whiteBalanceKelvin == 0 ? m_whiteBalanceKelvinRange.defaultValue : m_cachedState.whiteBalanceKelvin,
            m_whiteBalanceKelvinRange, 2000, 10000);
    }
}

void CameraController::resetControlRanges()
{
    m_brightnessRange = {};
    m_contrastRange = {};
    m_saturationRange = {};
    m_whiteBalanceKelvinRange = {};
    m_exposureRange = {};
    m_isoRange = {};
    m_supportedWhiteBalanceTypes.clear();
    m_whiteBalanceFallbackActive = false;
    m_fallbackWhiteBalanceMode = static_cast<int>(Device::DevWhiteBalanceAuto);
}

int CameraController::clampToRange(int value, const ParamRange &range, int fallbackMin, int fallbackMax) const
{
    if (range.valid && range.min <= range.max) {
        return std::clamp(value, range.min, range.max);
    }
    return std::clamp(value, fallbackMin, fallbackMax);
}

int CameraController::whiteBalancePresetToKelvin(int mode) const
{
    switch (mode) {
    case static_cast<int>(Device::DevWhiteBalanceDaylight):
        return 5500;
    case static_cast<int>(Device::DevWhiteBalanceFluorescent):
        return 4200;
    case static_cast<int>(Device::DevWhiteBalanceTungsten):
        return 3200;
    case static_cast<int>(Device::DevWhiteBalanceFlash):
        return 6000;
    case static_cast<int>(Device::DevWhiteBalanceFine):
        return 5000;
    case static_cast<int>(Device::DevWhiteBalanceCloudy):
        return 6500;
    case static_cast<int>(Device::DevWhiteBalanceShade):
        return 7500;
    case static_cast<int>(Device::DevWhiteBalanceDayLightFluorescent):
        return 5000;
    case static_cast<int>(Device::DevWhiteBalanceDayWhiteFluorescent):
        return 4500;
    case static_cast<int>(Device::DevWhiteBalanceCoolWhiteFluorescent):
        return 4000;
    case static_cast<int>(Device::DevWhiteBalanceWhiteFluorescent):
        return 3600;
    case static_cast<int>(Device::DevWhiteBalanceWarmWhiteFluorescent):
        return 3000;
    case static_cast<int>(Device::DevWhiteBalanceStandardLightA):
        return 2850;
    case static_cast<int>(Device::DevWhiteBalanceStandardLightB):
        return 3200;
    case static_cast<int>(Device::DevWhiteBalanceStandardLightC):
        return 6500;
    case static_cast<int>(Device::DevWhiteBalance55):
        return 5500;
    case static_cast<int>(Device::DevWhiteBalance65):
        return 6500;
    case static_cast<int>(Device::DevWhiteBalanceD75):
        return 7500;
    case static_cast<int>(Device::DevWhiteBalanceD50):
        return 5000;
    case static_cast<int>(Device::DevWhiteBalanceIsoStudioTungsten):
        return 3200;
    default:
        return 0;
    }
}

bool CameraController::applyManualWhiteBalance(int kelvin, int displayMode)
{
    int clamped = clampToRange(kelvin, m_whiteBalanceKelvinRange, 2000, 10000);
    bool success = executeCommand("Set White Balance (Manual)", [this, clamped]() {
        return m_device->cameraSetWhiteBalanceR(Device::DevWhiteBalanceManual, clamped);
    });

    if (success) {
        m_lastRequestedWhiteBalance = displayMode;
        m_currentState.whiteBalance = displayMode;
        m_currentState.whiteBalanceKelvin = clamped;
        emit stateChanged(m_currentState);
    }

    return success;
}

bool CameraController::isWhiteBalanceTypeSupported(int mode) const
{
    if (mode == static_cast<int>(Device::DevWhiteBalanceAuto) ||
        mode == static_cast<int>(Device::DevWhiteBalanceManual)) {
        return true;
    }

    if (m_supportedWhiteBalanceTypes.empty()) {
        return false;
    }

    return std::find(m_supportedWhiteBalanceTypes.begin(), m_supportedWhiteBalanceTypes.end(), mode)
        != m_supportedWhiteBalanceTypes.end();
}
