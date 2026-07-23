#include "CameraController.h"
#include <QThread>
#include <QDebug>
#include <algorithm>
#include <cstring>

static constexpr int kDefaultWhiteBalanceKelvin = 4800;

static QString productDisplayName(int productType, const QString &fallback)
{
    switch (productType) {
    case ObsbotProdTiny:      return QStringLiteral("OBSBOT Tiny");
    case ObsbotProdTiny4k:    return QStringLiteral("OBSBOT Tiny 4K");
    case ObsbotProdTiny2:     return QStringLiteral("OBSBOT Tiny 2");
    case ObsbotProdTiny2Lite: return QStringLiteral("OBSBOT Tiny 2 Lite");
    case ObsbotProdTinySE:    return QStringLiteral("OBSBOT Tiny SE");
    case ObsbotProdTailAir:   return QStringLiteral("OBSBOT Tail Air");
    case ObsbotProdTail2:     return QStringLiteral("OBSBOT Tail 2");
    case ObsbotProdTail2S:    return QStringLiteral("OBSBOT Tail 2S");
    case ObsbotProdMeet:      return QStringLiteral("OBSBOT Meet");
    case ObsbotProdMeet4k:    return QStringLiteral("OBSBOT Meet 4K");
    case ObsbotProdMeet2:     return QStringLiteral("OBSBOT Meet 2");
    case ObsbotProdMeetSE:    return QStringLiteral("OBSBOT Meet SE");
    case ObsbotProdMe:        return QStringLiteral("OBSBOT Me");
    case ObsbotProdHDMIBox:   return QStringLiteral("OBSBOT HDMI Box");
    case ObsbotProdNDIBox:    return QStringLiteral("OBSBOT NDI Box");
    default:                  return fallback;
    }
}

CameraController::CameraController(QObject *parent)
    : QObject(parent)
    , m_connected(false)
    , m_settlingTimer(nullptr)
{
    m_currentState = {};
    m_cachedState = {};
    m_currentState.whiteBalanceKelvin = 5000;
    m_cachedState.whiteBalanceKelvin = 5000;
    m_currentState.exposureAuto = m_cachedState.exposureAuto = true;
    m_currentState.exposure = m_cachedState.exposure = 33;
    m_currentState.antiFlicker = m_cachedState.antiFlicker = Device::PowerLineFreq50;
    m_currentState.hue = m_cachedState.hue = 50;
    m_currentState.sharpness = m_cachedState.sharpness = 50;
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
    const quint64 connectionAttempt = ++m_connectionAttempt;
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
        if (connected) {
            auto dev_list = Devices::get().getDevList();
            auto dev = pickDevice(dev_list);
            if (dev) {
                // SDK enumeration may finish after the V4L2 fallback has
                // already connected; drop the fallback so SDK-only controls
                // (tracking, HDR) aren't left hidden by a stale flag.
                if (m_v4l2Only) {
                    m_v4l2.close();
                    m_v4l2Only = false;
                }
                m_device = dev;
                m_connected = true;
                m_cameraInfo.name = productDisplayName(
                    m_device->productType(), QString::fromStdString(m_device->devName()));
                m_cameraInfo.serialNumber = QString::fromStdString(m_device->devSn());
                m_cameraInfo.version = QString::fromStdString(m_device->devVersion());
                m_cameraInfo.productType = m_device->productType();
                m_cameraInfo.connected = true;
                refreshControlRanges();
                emit cameraConnected(m_cameraInfo);
                updateState();
            }
        } else {
            m_connected = false;
            m_cameraInfo.connected = false;
            resetControlRanges();
            emit cameraDisconnected();
        }
    };

    Devices::get().setDevChangedCallback(onDevChanged, nullptr);
    Devices::get().setEnableMdnsScan(false);

    auto dev_list = Devices::get().getDevList();
    if (!dev_list.empty() && !m_connected) {
        auto dev = pickDevice(dev_list);
        if (dev) {
            m_device = dev;
            m_connected = true;
            m_cameraInfo.name = productDisplayName(
                m_device->productType(), QString::fromStdString(m_device->devName()));
            m_cameraInfo.serialNumber = QString::fromStdString(m_device->devSn());
            m_cameraInfo.version = QString::fromStdString(m_device->devVersion());
            m_cameraInfo.productType = m_device->productType();
            m_cameraInfo.connected = true;
            refreshControlRanges();
            emit cameraConnected(m_cameraInfo);
            updateState();
        }
    } else {
        // The SDK enumerates devices on a background thread and its connect
        // handshake takes several seconds, so the list is almost always
        // still empty here. An immediate fallback would win that race every
        // launch and lock the UI into V4L2-only mode; give the SDK a grace
        // period before settling for plain V4L2.
        QTimer::singleShot(8000, this, [this, connectionAttempt]() {
            if (connectionAttempt == m_connectionAttempt && !m_connected)
                tryV4l2Fallback();
        });
    }
}

void CameraController::tryV4l2Fallback()
{
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
    if (!m_v4l2.open(devicePath))
        return;

    Devices::get().close();

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

    m_brightnessRange = convert(m_v4l2.getBrightnessRange());
    m_contrastRange = convert(m_v4l2.getContrastRange());
    m_saturationRange = convert(m_v4l2.getSaturationRange());
    m_whiteBalanceKelvinRange = convert(m_v4l2.getWhiteBalanceTemperatureRange());

    m_supportedWhiteBalanceTypes.clear();
    m_supportedWhiteBalanceTypes.push_back(static_cast<int>(Device::DevWhiteBalanceAuto));
    m_supportedWhiteBalanceTypes.push_back(static_cast<int>(Device::DevWhiteBalanceManual));
}

void CameraController::updateV4l2State(bool includeImageControls)
{
    if (!m_v4l2.isOpen())
        return;

    if (includeImageControls) {
        m_currentState.brightness = m_v4l2.getBrightness();
        m_currentState.contrast = m_v4l2.getContrast();
        m_currentState.saturation = m_v4l2.getSaturation();

        bool autoWb = m_v4l2.getWhiteBalanceAuto();
        m_currentState.whiteBalance = autoWb
            ? static_cast<int>(Device::DevWhiteBalanceAuto)
            : static_cast<int>(Device::DevWhiteBalanceManual);
        m_currentState.whiteBalanceKelvin = m_v4l2.getWhiteBalanceTemperature();
    }

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

    emit stateChanged(m_currentState);
}

void CameraController::disconnectFromCamera()
{
    // Invalidate any delayed fallback belonging to the connection being
    // closed, even when SDK discovery has not completed yet.
    ++m_connectionAttempt;
    if (m_v4l2ScanTimer)
        m_v4l2ScanTimer->stop();

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

    // Tiny and Tiny 4K predate the AiWorkMode/MediaMode APIs.  The SDK sample
    // and API documentation require target selection for these two cameras.
    if (isOriginalTinyFamily()) {
        bool success = executeCommand(enabled ? "Enable AI tracking" : "Disable AI tracking",
                                      [this, enabled]() {
            return m_device->aiSetTargetSelectR(enabled);
        });
        if (success) {
            m_currentState.autoFramingEnabled = enabled;
            emit stateChanged(m_currentState);
        }
        return success;
    }

    if (enabled) {
        // Step 1: Set MediaMode to AutoFrame
        if (!executeCommand("Set MediaMode to AutoFrame", [this]() {
            return m_device->cameraSetMediaModeU(Device::MediaModeAutoFrame);
        })) {
            return false;
        }

        // Step 2: Set auto-framing mode after a brief delay (non-blocking)
        QTimer::singleShot(500, [this]() {
            executeCommand("Set AutoFraming mode", [this]() {
                return m_device->cameraSetAutoFramingModeU(Device::AutoFrmSingle, Device::AutoFrmUpperBody);
            });
        });

        // Restore auto focus when auto-framing is enabled
        setFocusAbsolute(0, true);

        m_currentState.autoFramingEnabled = true;
        emit stateChanged(m_currentState);
        return true;  // First command succeeded, second is pending
    } else {
        bool success = executeCommand("Disable AutoFraming", [this]() {
            return m_device->cameraSetMediaModeU(Device::MediaModeNormal);
        });
        if (success) {
            // Switch to manual focus when auto-framing is disabled
            setFocusAbsolute(m_currentState.manualFocusValue, false);

            m_currentState.autoFramingEnabled = false;
            emit stateChanged(m_currentState);
        }
        return success;
    }
}

bool CameraController::setAiMode(int mode, int subMode)
{
    if (!m_connected || m_v4l2Only) return false;

    auto workMode = static_cast<Device::AiWorkModeType>(mode);
    bool success = executeCommand("Set AI Mode", [this, workMode, subMode]() {
        return m_device->cameraSetAiModeU(workMode, subMode);
    });

    if (success) {
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
        if (isOriginalTinyFamily()) {
            // The original Tiny SDK uses gimbal angles in degrees. The
            // cameraSetPanTiltAbsolute API is documented for Meet cameras.
            success = executeCommand("Set Pan/Tilt", [this, pan, tilt]() {
                return m_device->gimbalSetSpeedPositionR(
                    0.0f, static_cast<float>(tilt * 90.0),
                    static_cast<float>(pan * 120.0), 0.0f, 90.0f, 90.0f);
            });
        } else {
            success = executeCommand("Set Pan/Tilt", [this, pan, tilt]() {
                return m_device->cameraSetPanTiltAbsolute(pan, tilt);
            });
        }
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
        if (isOriginalTinyFamily()) {
            success = executeCommand("Set Zoom", [this, zoom]() {
                return m_device->cameraSetZoomAbsoluteR(static_cast<float>(zoom));
            });
        } else {
            uint32_t zoomRatio = static_cast<uint32_t>(zoom * 100);
            success = executeCommand("Set Zoom", [this, zoomRatio]() {
                return m_device->cameraSetZoomWithSpeedAbsoluteR(zoomRatio, 255);
            });
        }
    }

    if (success) {
        m_currentState.zoom = zoom;
        if (!m_v4l2Only && isOriginalTinyFamily())
            m_currentState.fovMode = Device::FovTypeNull;
        emit stateChanged(m_currentState);
    }

    return success;
}

bool CameraController::centerView()
{
    if (m_connected && !m_v4l2Only && isOriginalTinyFamily()) {
        bool success = executeCommand("Center gimbal", [this]() {
            return m_device->gimbalRstPosR();
        });
        if (success) {
            m_currentState.pan = 0.0;
            m_currentState.tilt = 0.0;
            emit stateChanged(m_currentState);
        }
        return success;
    }
    return setPanTilt(0.0, 0.0);
}

CameraController::CameraState CameraController::pollCurrentState(bool includeImageControls)
{
    if (m_connected && !isSettling()) {
        if (m_v4l2Only)
            updateV4l2State(includeImageControls);
        else
            updateState(includeImageControls);
    }
    return isSettling() ? m_cachedState : m_currentState;
}

bool CameraController::setHDR(bool enabled)
{
    if (!m_connected || m_v4l2Only) return false;

    bool success = executeCommand(enabled ? "Enable HDR" : "Disable HDR", [this, enabled]() {
        return m_device->cameraSetWdrR(enabled ? Device::DevWdrModeDol2TO1 : Device::DevWdrModeNone);
    });
    if (success) {
        m_currentState.hdrEnabled = enabled;
        emit stateChanged(m_currentState);
    }
    return success;
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

    bool success = executeCommand("Set FOV", [this, fov]() {
        return m_device->cameraSetFovU(fov);
    });
    if (success) {
        m_currentState.fovMode = fovMode;
        static constexpr double presetZoom[] = {1.00, 1.05, 1.15};
        m_currentState.zoom = presetZoom[fovMode];
        m_currentState.zoomRatio = qRound(m_currentState.zoom * 100.0);
        // Tiny/Tiny 4K can briefly report the previous zoom after changing
        // FOV. Preserve the intended preset until the camera has caught up.
        m_zoomPollingPause.start();
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setFaceAE(bool enabled)
{
    if (!m_connected || m_v4l2Only) return false;

    bool success = executeCommand(enabled ? "Enable Face AE" : "Disable Face AE", [this, enabled]() {
        return m_device->cameraSetFaceAER(enabled);
    });
    if (success) {
        m_currentState.faceAEEnabled = enabled;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setFaceFocus(bool enabled)
{
    if (!m_connected || m_v4l2Only) return false;

    // Face focus selects the subject to prioritize; it does not itself leave
    // manual lens mode. Ensure the lens can actually follow that subject.
    if (enabled && !m_currentState.autoFocusEnabled
            && !setFocusAbsolute(m_currentState.manualFocusValue, true)) {
        return false;
    }

    bool success = executeCommand(enabled ? "Enable Face Focus" : "Disable Face Focus", [this, enabled]() {
        return m_device->cameraSetFaceFocusR(enabled);
    });
    if (success) {
        m_currentState.faceFocusEnabled = enabled;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setFocusAbsolute(int position, bool autoFocus)
{
    if (!m_connected) return false;
    position = qBound(0, position, 100);

    if (!autoFocus && m_currentState.faceFocusEnabled && !m_v4l2Only) {
        if (!executeCommand("Disable Face Focus", [this]() {
                return m_device->cameraSetFaceFocusR(false);
            })) {
            return false;
        }
        m_currentState.faceFocusEnabled = false;
    }

    bool success;
    if (m_v4l2Only) {
        m_v4l2.setAutoFocus(autoFocus);
        success = autoFocus || m_v4l2.setFocusAbsolute(position);
    } else {
        success = executeCommand("Set Focus", [this, position, autoFocus]() {
            return m_device->cameraSetFocusAbsolute(position, autoFocus);
        });
    }
    if (success) {
        m_currentState.autoFocusEnabled = autoFocus;
        m_currentState.manualFocusValue = position;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setTrackingStyle(int style)
{
    if (!m_connected || m_v4l2Only || !isOriginalTinyFamily()) return false;
    style = qBound(static_cast<int>(Device::AiVTrackStandard), style,
                   static_cast<int>(Device::AiVTrackMotion));
    bool success = executeCommand("Set tracking style", [this, style]() {
        return m_device->aiSetTrackingModeR(static_cast<Device::AiVerticalTrackType>(style));
    });
    if (success) {
        m_currentState.trackingStyle = style;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setExposure(int shutterTime, bool automatic)
{
    if (!m_connected || m_v4l2Only) return false;
    if (isTiny4k()) return false;
    int clamped = clampToRange(shutterTime, m_exposureRange, 9, 42);

    bool success = executeCommand(automatic ? "Enable auto exposure" : "Set exposure",
                                  [this, clamped, automatic]() {
        return m_device->cameraSetExposureAbsolute(clamped, automatic);
    });
    if (success) {
        m_currentState.exposureAuto = automatic;
        m_currentState.exposure = clamped;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setAntiFlicker(int frequency)
{
    if (!m_connected || m_v4l2Only) return false;
    int clamped = clampToRange(frequency, m_antiFlickerRange, 0, 3);
    bool success = executeCommand("Set anti-flicker", [this, clamped]() {
        return m_device->cameraSetAntiFlickR(clamped);
    });
    if (success) {
        m_currentState.antiFlicker = clamped;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setGestureControl(int gesture, bool enabled)
{
    if (!m_connected || m_v4l2Only || !isOriginalTinyFamily()) return false;
    return executeCommand("Set gesture control", [this, gesture, enabled]() {
        return m_device->aiSetGestureCtrlIndividualR(gesture, enabled);
    });
}

bool CameraController::setHardwareMirror(bool enabled)
{
    if (!m_connected || m_v4l2Only || !isTiny4k()) return false;
    return executeCommand("Set hardware mirror", [this, enabled]() {
        return m_device->cameraSetImageFlipHorizonU(enabled ? 1 : 0);
    });
}

bool CameraController::setMicrophoneDuringSleep(bool enabled)
{
    if (!m_connected || m_v4l2Only || !isTiny4k()) return false;
    return executeCommand("Set sleep microphone", [this, enabled]() {
        return m_device->cameraSetMicrophoneDuringSleepU(enabled ? 1 : 0);
    });
}

bool CameraController::setSleepTimeout(int seconds)
{
    if (!m_connected || m_v4l2Only || !isOriginalTinyFamily()) return false;
    return executeCommand("Set sleep timeout", [this, seconds]() {
        return m_device->cameraSetSuspendTimeU(seconds);
    });
}

bool CameraController::setDeviceAwake(bool awake)
{
    if (!m_connected || m_v4l2Only) return false;
    return executeCommand(awake ? "Wake camera" : "Sleep camera", [this, awake]() {
        return m_device->cameraSetDevRunStatusR(
            awake ? Device::DevStatusRun : Device::DevStatusSleep);
    });
}

bool CameraController::setAiEnabled(bool enabled)
{
    if (!m_connected || m_v4l2Only || !isOriginalTinyFamily()) return false;
    return executeCommand("Set AI enabled", [this, enabled]() {
        return m_device->aiSetEnabledR(enabled);
    });
}

bool CameraController::setVerticalMode(bool enabled)
{
    if (!m_connected || m_v4l2Only || !isTiny4k()) return false;
    return executeCommand("Set vertical mode", [this, enabled]() {
        return m_device->cameraSetVerticalModeU(enabled ? 1 : 0);
    });
}

bool CameraController::restoreFactorySettings()
{
    if (!m_connected || m_v4l2Only) return false;
    return executeCommand("Restore factory settings", [this]() {
        return m_device->cameraSetRestoreFactorySettingsR();
    });
}

bool CameraController::setCurrentViewAsBootPosition()
{
    if (!m_connected || m_v4l2Only || !isOriginalTinyFamily()) return false;
    float attitude[3] = {};
    if (m_device->gimbalGetAttitudeInfoR(attitude) != 0) return false;
    float zoom = 1.0f;
    if (m_device->cameraGetZoomAbsoluteR(zoom) != 0) return false;
    Device::PresetPosInfo preset{};
    preset.roll = attitude[0];
    preset.pitch = attitude[1];
    preset.yaw = attitude[2];
    preset.zoom = zoom;
    return executeCommand("Set boot position", [this, preset]() {
        return m_device->aiSetGimbalBootPosR(preset);
    });
}

bool CameraController::saveHardwarePreset(int id)
{
    if (!m_connected || m_v4l2Only || !isOriginalTinyFamily()) return false;
    float attitude[3] = {};
    if (m_device->gimbalGetAttitudeInfoR(attitude) != 0) return false;
    float zoom = 1.0f;
    if (m_device->cameraGetZoomAbsoluteR(zoom) != 0) return false;
    Device::PresetPosInfo preset{};
    preset.id = id;
    preset.roll = attitude[0];
    preset.pitch = attitude[1];
    preset.yaw = attitude[2];
    preset.zoom = zoom;
    QByteArray name = QString("Preset %1").arg(id + 1).toUtf8();
    preset.name_len = qMin(name.size(), 63);
    std::memcpy(preset.name, name.constData(), preset.name_len);
    return executeCommand("Save hardware preset", [this, preset]() mutable {
        return m_device->aiAddGimbalPresetR(&preset);
    });
}

bool CameraController::recallHardwarePreset(int id)
{
    if (!m_connected || m_v4l2Only || !isOriginalTinyFamily()) return false;
    return executeCommand("Recall hardware preset", [this, id]() {
        return m_device->aiTrgGimbalPresetR(id);
    });
}

bool CameraController::setGimbalSpeed(double pitch, double pan)
{
    if (!m_connected || m_v4l2Only || !isOriginalTinyFamily()) return false;
    return executeCommand("Set gimbal speed", [this, pitch, pan]() {
        return m_device->aiSetGimbalSpeedCtrlR(pitch, pan, 0.0);
    });
}

bool CameraController::setTiny4kExposure(int value)
{
    if (!m_connected || !isTiny4k()) return false;
    V4l2Backend backend;
    if (!backend.open(m_device->videoDevPath())) return false;
    int clamped = clampToRange(value, m_uvcExposureRange, 1, 2500);
    bool success = backend.setAutoExposure(false) && backend.setExposureAbsolute(clamped);
    if (success) m_currentState.uvcExposure = clamped;
    return success;
}

bool CameraController::setTiny4kGain(int value)
{
    if (!m_connected || !isTiny4k()) return false;
    V4l2Backend backend;
    if (!backend.open(m_device->videoDevPath())) return false;
    int clamped = clampToRange(value, m_gainRange, 1, 48);
    bool success = backend.setGain(clamped);
    if (success) m_currentState.gain = clamped;
    return success;
}

bool CameraController::setTiny4kBacklightCompensation(int value)
{
    if (!m_connected || !isTiny4k()) return false;
    V4l2Backend backend;
    if (!backend.open(m_device->videoDevPath())) return false;
    int clamped = clampToRange(value, m_backlightRange, 0, 18);
    bool success = backend.setBacklightCompensation(clamped);
    if (success) m_currentState.backlightCompensation = clamped;
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

bool CameraController::setHue(int value)
{
    if (!m_connected || m_v4l2Only) return false;
    int clamped = clampToRange(value, m_hueRange, 0, 100);
    bool success = executeCommand("Set Hue", [this, clamped]() {
        return m_device->cameraSetImageHueR(clamped);
    });
    if (success) {
        m_currentState.hue = clamped;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setSharpness(int value)
{
    if (!m_connected || m_v4l2Only) return false;
    int clamped = clampToRange(value, m_sharpnessRange, 0, 100);
    bool success = executeCommand("Set Sharpness", [this, clamped]() {
        return m_device->cameraSetImageSharpR(clamped);
    });
    if (success) {
        m_currentState.sharpness = clamped;
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

bool CameraController::executeCommand(const QString &description, std::function<int32_t()> command)
{
    int32_t ret = command();
    if (ret != 0) {
        emit commandFailed(description, ret);
        return false;
    }
    return true;
}

void CameraController::updateState(bool includeImageControls)
{
    if (!m_connected) return;

    // Don't update from camera during settling period
    if (isSettling()) {
        return;
    }

    auto status = m_device->cameraStatus();

    m_currentState.aiMode = status.tiny.ai_mode;
    m_currentState.aiSubMode = status.tiny.ai_sub_mode;
    // Tiny/Tiny 4K firmware 1.2.6.2 returns 2.0 from the normalized getter
    // regardless of the actual zoom. Its status field reliably reports a
    // 0..100 offset from 1.0x instead.
    const bool zoomPollingPaused =
        m_zoomPollingPause.isValid() && m_zoomPollingPause.elapsed() < 2000;
    if (!zoomPollingPaused) {
        if (isOriginalTinyFamily() && status.tiny.zoom_ratio <= 100) {
            m_currentState.zoom = 1.0
                + static_cast<double>(status.tiny.zoom_ratio) / 100.0;
            m_currentState.zoomRatio = qRound(m_currentState.zoom * 100.0);
        } else {
            float zoom = 1.0f;
            if (m_device->cameraGetZoomAbsoluteR(zoom) == 0
                    && zoom >= 1.0f && zoom <= 2.0f) {
                m_currentState.zoom = zoom;
                m_currentState.zoomRatio = qRound(zoom * 100.0f);
            }
        }
    }
    m_currentState.hdrEnabled = status.tiny.hdr;
    m_currentState.faceAEEnabled = status.tiny.face_ae;
    m_currentState.faceFocusEnabled = status.tiny.face_auto_focus;
    m_currentState.autoFocusEnabled = status.tiny.auto_focus;
    m_currentState.manualFocusValue = status.tiny.manual_focus_value;
    if (status.tiny.fov >= Device::FovType86
            && status.tiny.fov <= Device::FovTypeNull) {
        m_currentState.fovMode = status.tiny.fov;
    }
    m_currentState.devStatus = status.tiny.dev_status;
    m_currentState.autoFramingEnabled = isOriginalTinyFamily()
        ? status.tiny.ai_target != 0
        : m_currentState.aiMode != Device::AiWorkModeNone;
    m_currentState.trackSpeedMode = status.tiny.ai_tracker_speed;
    m_currentState.audioAutoGainEnabled = status.tiny.audio_auto_gain;

    if (includeImageControls) {
    // Image controls - read current values from camera
    // Note: Preserve auto mode flags - camera doesn't have concept of "auto" for these
    bool preservedBrightnessAuto = m_currentState.brightnessAuto;
    bool preservedContrastAuto = m_currentState.contrastAuto;
    bool preservedSaturationAuto = m_currentState.saturationAuto;

    int32_t brightness, contrast, saturation, hue, sharpness;
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
    if (m_device->cameraGetImageHueR(hue) == 0)
        m_currentState.hue = clampToRange(hue, m_hueRange, 0, 100);
    if (m_device->cameraGetImageSharpR(sharpness) == 0)
        m_currentState.sharpness = clampToRange(sharpness, m_sharpnessRange, 0, 100);

    if (!isTiny4k()) {
        int32_t exposure = m_currentState.exposure;
        bool exposureAuto = m_currentState.exposureAuto;
        if (m_device->cameraGetExposureAbsolute(exposure, exposureAuto) == 0) {
            m_currentState.exposure = clampToRange(exposure, m_exposureRange, 9, 42);
            m_currentState.exposureAuto = exposureAuto;
        }
    }
    if (isTiny4k()) {
        V4l2Backend backend;
        if (backend.open(m_device->videoDevPath())) {
            int value = backend.getExposureAbsolute();
            if (value >= 0) m_currentState.uvcExposure = value;
            value = backend.getGain();
            if (value >= 0) m_currentState.gain = value;
            value = backend.getBacklightCompensation();
            if (value >= 0) m_currentState.backlightCompensation = value;
        }
    }
    int32_t antiFlicker = m_currentState.antiFlicker;
    if (m_device->cameraGetAntiFlickR(antiFlicker) == 0)
        m_currentState.antiFlicker = antiFlicker;
    if (m_device->cameraGetWhiteBalanceR(wbType, wbParam) == 0) {
        m_currentState.whiteBalance = static_cast<int>(wbType);
        if (wbType == Device::DevWhiteBalanceManual) {
            m_currentState.whiteBalanceKelvin = clampToRange(wbParam, m_whiteBalanceKelvinRange, 2000, 10000);
        } else if (m_whiteBalanceKelvinRange.valid) {
            m_currentState.whiteBalanceKelvin = clampToRange(m_whiteBalanceKelvinRange.defaultValue, m_whiteBalanceKelvinRange, 2000, 10000);
        }
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
    // Update config with current camera state before saving
    saveCurrentStateToConfig();
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

    // Apply all settings to the camera
    enableAutoFraming(settings.faceTracking);
    setHDR(settings.hdr);
    setFOV(settings.fov);
    setFaceAE(settings.faceAE);
    setFaceFocus(settings.faceFocus);
    setZoom(settings.zoom);
    setPanTilt(settings.pan, settings.tilt);
    if (settings.focus >= 0 && !settings.faceFocus) {
        setFocusAbsolute(settings.focus, false);
    } else {
        setFocusAbsolute(0, true);
    }

    if (isTiny2Family()) {
        setAiMode(settings.aiMode, settings.aiSubMode);
        setAutoZoom(settings.autoZoom);
        setTrackSpeed(settings.trackSpeed);
        setAudioAutoGain(settings.audioAutoGain);
    }
    if (isOriginalTinyFamily())
        setTrackingStyle(settings.trackingStyle);

    // Image controls
    setBrightness(settings.brightness);
    setContrast(settings.contrast);
    setSaturation(settings.saturation);
    setHue(settings.hue);
    setSharpness(settings.sharpness);
    setAntiFlicker(settings.antiFlicker);
    if (settings.whiteBalance == static_cast<int>(Device::DevWhiteBalanceManual)) {
        setWhiteBalanceManual(settings.whiteBalanceKelvin);
    } else {
        setWhiteBalance(settings.whiteBalance);
    }

    emit configLoaded();
}

void CameraController::applyCurrentStateToCamera(const CameraState &uiState)
{
    if (!m_connected) return;

    // Update current state with UI state (including auto mode flags)
    m_currentState.brightnessAuto = uiState.brightnessAuto;
    m_currentState.contrastAuto = uiState.contrastAuto;
    m_currentState.saturationAuto = uiState.saturationAuto;

    // Cache the intended state
    m_cachedState = uiState;

    // Begin settling period - block status updates for 2 seconds
    beginSettling(2000);

    // Apply the current UI state to camera (respects user changes)
    enableAutoFraming(uiState.autoFramingEnabled);
    if (isTiny2Family()) {
        setAiMode(uiState.aiMode, uiState.aiSubMode);
        setAutoZoom(uiState.autoZoomEnabled);
        setTrackSpeed(uiState.trackSpeedMode);
        setAudioAutoGain(uiState.audioAutoGainEnabled);
    }
    if (isOriginalTinyFamily())
        setTrackingStyle(uiState.trackingStyle);
    setHDR(uiState.hdrEnabled);
    setFOV(uiState.fovMode);
    setFaceAE(uiState.faceAEEnabled);
    setFaceFocus(uiState.faceFocusEnabled);
    setFocusAbsolute(uiState.manualFocusValue,
                     uiState.autoFocusEnabled || uiState.faceFocusEnabled);
    setZoom(uiState.zoom);
    setPanTilt(uiState.pan, uiState.tilt);

    // Image controls
    setBrightness(uiState.brightness);
    setContrast(uiState.contrast);
    setSaturation(uiState.saturation);
    setHue(uiState.hue);
    setSharpness(uiState.sharpness);
    setAntiFlicker(uiState.antiFlicker);
    if (uiState.whiteBalance == static_cast<int>(Device::DevWhiteBalanceManual)) {
        setWhiteBalanceManual(uiState.whiteBalanceKelvin);
    } else {
        setWhiteBalance(uiState.whiteBalance);
    }
}

void CameraController::saveCurrentStateToConfig()
{
    // Get current settings to preserve app settings (like startMinimized)
    Config::CameraSettings settings = m_config.getSettings();

    // Update only camera-related settings from current state
    settings.faceTracking = m_currentState.autoFramingEnabled;
    settings.hdr = m_currentState.hdrEnabled;
    if (m_currentState.fovMode >= Device::FovType86
            && m_currentState.fovMode <= Device::FovType65) {
        settings.fov = m_currentState.fovMode;
    }
    settings.faceAE = m_currentState.faceAEEnabled;
    settings.faceFocus = m_currentState.faceFocusEnabled;
    settings.zoom = qBound(1.0, m_currentState.zoom, 2.0);
    settings.pan = m_currentState.pan;
    settings.tilt = m_currentState.tilt;
    settings.aiMode = m_currentState.aiMode;
    settings.aiSubMode = m_currentState.aiSubMode;
    settings.autoZoom = m_currentState.autoZoomEnabled;
    settings.trackSpeed = m_currentState.trackSpeedMode;
    settings.trackingStyle = m_currentState.trackingStyle;
    settings.audioAutoGain = m_currentState.audioAutoGainEnabled;

    // Image controls
    settings.brightnessAuto = m_currentState.brightnessAuto;
    settings.brightness = m_currentState.brightness;
    settings.contrastAuto = m_currentState.contrastAuto;
    settings.contrast = m_currentState.contrast;
    settings.saturationAuto = m_currentState.saturationAuto;
    settings.saturation = m_currentState.saturation;
    settings.hue = m_currentState.hue;
    settings.sharpness = m_currentState.sharpness;
    settings.antiFlicker = m_currentState.antiFlicker;
    settings.whiteBalance = m_currentState.whiteBalance;
    settings.whiteBalanceKelvin = m_currentState.whiteBalanceKelvin;
    settings.focus = m_currentState.autoFocusEnabled ? -1 : m_currentState.manualFocusValue;

    m_config.setSettings(settings);
}

bool CameraController::isTiny2Family() const
{
    return m_cameraInfo.productType == ObsbotProdTiny2 ||
           m_cameraInfo.productType == ObsbotProdTiny2Lite ||
           m_cameraInfo.productType == ObsbotProdTinySE;
}

bool CameraController::isOriginalTinyFamily() const
{
    return m_cameraInfo.productType == ObsbotProdTiny ||
           m_cameraInfo.productType == ObsbotProdTiny4k;
}

bool CameraController::isTiny4k() const
{
    return m_cameraInfo.productType == ObsbotProdTiny4k;
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
    fetchRange(&Device::cameraGetRangeImageHueR, m_hueRange);
    fetchRange(&Device::cameraGetRangeImageSharpR, m_sharpnessRange);
    fetchRange(&Device::cameraGetRangeExposureAbsolute, m_exposureRange);
    fetchRange(&Device::cameraGetRangeAntiFlickR, m_antiFlickerRange);
    fetchRange(&Device::cameraGetRangeWhiteBalanceR, m_whiteBalanceKelvinRange);

    if (isTiny4k()) {
        V4l2Backend backend;
        if (backend.open(m_device->videoDevPath())) {
            auto convert = [](V4l2Backend::ControlRange range) {
                return ParamRange{range.min, range.max, range.step,
                                  range.defaultValue, range.valid};
            };
            m_uvcExposureRange = convert(backend.getExposureRange());
            m_gainRange = convert(backend.getGainRange());
            m_backlightRange = convert(backend.getBacklightCompensationRange());
        }
    }

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
    m_hueRange = {};
    m_sharpnessRange = {};
    m_exposureRange = {};
    m_antiFlickerRange = {};
    m_uvcExposureRange = {};
    m_gainRange = {};
    m_backlightRange = {};
    m_whiteBalanceKelvinRange = {};
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
