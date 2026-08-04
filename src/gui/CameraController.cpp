#include "CameraController.h"
#include <QThread>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>

static constexpr int kDefaultWhiteBalanceKelvin = 4800;
static constexpr int kSdkDiscoveryGraceMs = 5000;
static constexpr int kV4l2RescanIntervalMs = 3000;

struct CameraController::DeviceCallbackGate
    : std::enable_shared_from_this<CameraController::DeviceCallbackGate>
{
    struct Lease {
        Lease() = default;
        Lease(std::shared_ptr<DeviceCallbackGate> gate,
              CameraController *controller)
            : gate(std::move(gate)), controller(controller)
        {
        }
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        Lease(Lease &&other) noexcept
            : gate(std::move(other.gate)), controller(other.controller)
        {
            other.controller = nullptr;
        }
        Lease &operator=(Lease &&) = delete;
        ~Lease()
        {
            if (gate) {
                gate->release();
            }
        }

        CameraController *get() const { return controller; }

        std::shared_ptr<DeviceCallbackGate> gate;
        CameraController *controller = nullptr;
    };

    explicit DeviceCallbackGate(CameraController *controller)
        : controller(controller)
    {
    }

    Lease acquire()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping || !controller) {
            return {};
        }
        ++activeCallbacks;
        return Lease(shared_from_this(), controller);
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        controller = nullptr;
    }

    void waitForCallbacks()
    {
        std::unique_lock<std::mutex> lock(mutex);
        callbacksDrained.wait(lock, [this]() { return activeCallbacks == 0; });
    }

private:
    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        --activeCallbacks;
        if (activeCallbacks == 0) {
            callbacksDrained.notify_all();
        }
    }

    std::mutex mutex;
    std::condition_variable callbacksDrained;
    CameraController *controller = nullptr;
    size_t activeCallbacks = 0;
    bool stopping = false;
};

CameraController::CameraController(QObject *parent)
    : QObject(parent)
    , m_deviceCallbackGate(std::make_shared<DeviceCallbackGate>(this))
    , m_connected(false)
    , m_settlingTimer(nullptr)
    , m_aiConfirmationTimer(nullptr)
    , m_autoFramingModeTimer(nullptr)
{
    m_currentState = {};
    m_cachedState = {};
    m_currentState.whiteBalanceKelvin = 5000;
    m_cachedState.whiteBalanceKelvin = 5000;
    m_lastRequestedWhiteBalance = static_cast<int>(Device::DevWhiteBalanceAuto);
    m_whiteBalanceFallbackActive = false;
    m_fallbackWhiteBalanceMode = static_cast<int>(Device::DevWhiteBalanceAuto);

    // Create settling timer
    m_settlingTimer = new QTimer(this);
    m_settlingTimer->setSingleShot(true);

    m_aiConfirmationTimer = new QTimer(this);
    m_aiConfirmationTimer->setInterval(300);
    connect(m_aiConfirmationTimer, &QTimer::timeout, this, [this]() {
        if (!m_connected || m_v4l2Only || !m_pendingAiIntent) {
            m_aiConfirmationTimer->stop();
            return;
        }
        updateState();
        if (!m_pendingAiIntent) {
            m_aiConfirmationTimer->stop();
        } else {
            m_aiConfirmationTimer->setInterval(
                m_pendingAiIntent->failureReported ? 1000 : 300);
        }
    });
    connect(m_settlingTimer, &QTimer::timeout, this, [this]() {
        if (!m_pendingAiIntent || !m_connected || m_v4l2Only) {
            return;
        }
        updateState();
        if (m_pendingAiIntent) {
            m_aiConfirmationTimer->setInterval(
                m_pendingAiIntent->failureReported ? 1000 : 300);
            m_aiConfirmationTimer->start();
        }
    });

    // Meet-series auto-framing has a delayed second step. Keep the timer owned
    // so a newer manual/off intent can cancel that step deterministically.
    m_autoFramingModeTimer = new QTimer(this);
    m_autoFramingModeTimer->setSingleShot(true);
    connect(m_autoFramingModeTimer, &QTimer::timeout, this, [this]() {
        if (!m_autoFramingRequested || !m_connected || m_v4l2Only || !m_device) {
            return;
        }
        const bool success = executeCommand("Set AutoFraming mode", [this]() {
            return m_device->cameraSetAutoFramingModeU(
                Device::AutoFrmSingle, Device::AutoFrmUpperBody);
        });
        m_autoFramingRequested = false;
        if (success) {
            emit trackingStateConfirmed(
                true, m_autoFramingIntentGeneration);
        } else {
            emit trackingStateConfirmationFailed(
                true, m_autoFramingIntentGeneration);
        }
    });

    resetControlRanges();
}

CameraController::~CameraController()
{
    // The SDK may invoke its callback from another thread. Stop new leases
    // before unregistering, then drain callbacks that already acquired one so
    // none can race this object's destruction while queuing a Qt continuation.
    const auto callbackGate = m_deviceCallbackGate;
    callbackGate->stop();
    ++m_connectionGeneration;
    if (m_deviceCallbackRegistered) {
        Devices::get().setDevChangedCallback(Devices::devChangedCallback{}, nullptr);
        m_deviceCallbackRegistered = false;
    }
    callbackGate->waitForCallbacks();
    m_autoFramingRequested = false;
    if (m_autoFramingModeTimer) {
        m_autoFramingModeTimer->stop();
    }
    if (m_aiConfirmationTimer) {
        m_aiConfirmationTimer->stop();
    }
}

void CameraController::connectToCamera()
{
    // Reconnect to the same physical target. The serial remains authoritative
    // if /dev/video numbering changes across a disconnect.
    connectToCamera(m_selectedDevicePath, m_selectedDeviceSerial);
}

void CameraController::selectCameraTarget(
    const QString &devicePath, const QString &serialNumber)
{
    m_selectedDevicePath = devicePath;
    m_selectedDeviceSerial = serialNumber;
}

void CameraController::connectToCamera(
    const QString &devicePath, const QString &serialNumber)
{
    if (m_connected) {
        qDebug() << "CameraController: ignoring connect request while already connected to"
                 << getVideoDevicePath();
        return;
    }
    if (m_v4l2ScanTimer) {
        m_v4l2ScanTimer->stop();
    }

    selectCameraTarget(devicePath, serialNumber);
    m_pendingAiIntent.reset();
    m_pendingTrackingProfile.reset();
    m_autoFramingRequested = false;
    ++m_trackingIntentGeneration;
    m_pendingManualPosition.reset();
    m_aiStateConfirmed = false;
    m_manualMovementAuthorized = false;
    m_autoFramingModeTimer->stop();
    m_aiConfirmationTimer->stop();
    const quint64 generation = ++m_connectionGeneration;

    const auto callbackGate = m_deviceCallbackGate;
    auto onDevChanged = [callbackGate, generation](
                            std::string devSn, bool connected, void * /*param*/) {
        // The SDK does not guarantee callback thread affinity. A lease keeps
        // the QObject alive through invokeMethod(); the QObject context then
        // owns cancellation of the queued continuation during destruction.
        auto lease = callbackGate->acquire();
        CameraController *controller = lease.get();
        if (!controller) {
            return;
        }
        const QString changedSerial = QString::fromStdString(devSn);
        QMetaObject::invokeMethod(controller, [controller, generation, changedSerial, connected]() {
            if (controller->m_connectionGeneration != generation) {
                return;
            }
            if (!connected && !changedSerial.isEmpty()
                && !controller->m_cameraInfo.serialNumber.isEmpty()
                && changedSerial != controller->m_cameraInfo.serialNumber) {
                return;
            }

            if (connected) {
                if (!controller->m_connected) {
                    controller->tryConnectSdkDevice();
                }
            } else if (controller->m_connected && !controller->m_v4l2Only) {
                controller->m_device.reset();
                controller->m_connected = false;
                controller->m_cameraInfo.connected = false;
                controller->m_pendingAiIntent.reset();
                controller->m_pendingTrackingProfile.reset();
                ++controller->m_trackingIntentGeneration;
                controller->m_pendingManualPosition.reset();
                controller->m_aiStateConfirmed = false;
                controller->m_manualMovementAuthorized = false;
                controller->m_autoFramingRequested = false;
                controller->m_autoFramingModeTimer->stop();
                controller->m_aiConfirmationTimer->stop();
                controller->m_settlingTimer->stop();
                controller->resetControlRanges();
                emit controller->cameraDisconnected();
                // Give the SDK a fresh chance to report a transient reconnect
                // before considering the reduced-capability V4L2 backend.
                controller->tryV4l2Fallback();
            }
        }, Qt::QueuedConnection);
    };

    Devices::get().setDevChangedCallback(onDevChanged, nullptr);
    m_deviceCallbackRegistered = true;
    Devices::get().setEnableMdnsScan(false);

    if (!tryConnectSdkDevice()) {
        tryV4l2Fallback();
    }
}

std::shared_ptr<Device> CameraController::selectedSdkDevice() const
{
    const auto devices = Devices::get().getDevList();
    if (devices.empty()) {
        return nullptr;
    }
    if (!m_selectedDeviceSerial.isEmpty()) {
        for (const auto &device : devices) {
            if (QString::fromStdString(device->devSn())
                == m_selectedDeviceSerial) {
                return device;
            }
        }
        return nullptr;
    }
    if (m_selectedDevicePath.isEmpty()) {
        return devices.front();
    }

    for (const auto &device : devices) {
        if (QString::fromStdString(device->videoDevPath())
            == m_selectedDevicePath) {
            return device;
        }
    }
    // Exact selection is a safety boundary: never direct PTZ commands to an
    // arbitrary first camera while the requested camera is still discovering.
    return nullptr;
}

bool CameraController::connectSdkDevice(
    const std::shared_ptr<Device> &device)
{
    if (m_connected) {
        return !m_v4l2Only;
    }
    if (!device) {
        return false;
    }

    if (m_v4l2ScanTimer) {
        m_v4l2ScanTimer->stop();
    }
    m_device = device;
    m_v4l2Only = false;
    m_v4l2DevicePath.clear();
    m_connected = true;
    m_manualMovementAuthorized = false;
    m_currentState.panTiltKnown = false;
    m_currentState.zoomKnown = false;
    m_currentState.imageSettingsKnown = false;
    m_cameraInfo.name = QString::fromStdString(device->devName());
    m_cameraInfo.serialNumber = QString::fromStdString(device->devSn());
    m_selectedDevicePath = QString::fromStdString(device->videoDevPath());
    m_selectedDeviceSerial = m_cameraInfo.serialNumber;
    m_cameraInfo.version = QString::fromStdString(device->devVersion());
    m_cameraInfo.productType = device->productType();
    m_cameraInfo.connected = true;
    refreshControlRanges();
    qInfo() << "CameraController: connected through SDK"
            << QString::fromStdString(device->videoDevPath())
            << m_cameraInfo.serialNumber;
    emit cameraConnected(m_cameraInfo);
    updateState();
    return true;
}

bool CameraController::tryConnectSdkDevice()
{
    return connectSdkDevice(selectedSdkDevice());
}

void CameraController::tryV4l2Fallback()
{
    if (m_connected) {
        return;
    }

    m_v4l2FallbackGeneration = m_connectionGeneration;
    if (!m_v4l2ScanTimer) {
        m_v4l2ScanTimer = new QTimer(this);
        m_v4l2ScanTimer->setSingleShot(false);
        connect(m_v4l2ScanTimer, &QTimer::timeout, this, [this]() {
            if (m_v4l2FallbackGeneration != m_connectionGeneration
                || m_connected) {
                m_v4l2ScanTimer->stop();
                return;
            }

            // A device callback can be queued at the same time as this timer.
            // Re-read the SDK list before probing fallback so an available
            // exact SDK target wins this arbitration point.
            if (tryConnectSdkDevice()) {
                return;
            }

            // A serial-bound target must never fall back by pathname: Linux
            // may reuse /dev/videoN for another physical camera after hotplug.
            std::string path;
            if (m_selectedDeviceSerial.isEmpty()) {
                path = m_selectedDevicePath.isEmpty()
                    ? V4l2Backend::findObsbotDevice()
                    : m_selectedDevicePath.toStdString();
            }
            if (!path.empty()) {
                connectV4l2(path);
            }
            if (!m_connected) {
                m_v4l2ScanTimer->setInterval(kV4l2RescanIntervalMs);
            }
        });
    }
    // Do not synchronously claim /dev/video* while the SDK discovery callback
    // is queued on the Qt event loop. V4L2 remains a bounded fallback only.
    m_v4l2ScanTimer->setInterval(kSdkDiscoveryGraceMs);
    m_v4l2ScanTimer->start();
}

void CameraController::connectV4l2(const std::string &devicePath)
{
    if (!m_v4l2.open(devicePath))
        return;

    // Close the final probe and prefer an SDK device that appeared while the
    // V4L2 path was being opened. Keeping the shared Device candidate removes
    // a second list lookup from this last arbitration point.
    if (const auto sdkDevice = selectedSdkDevice()) {
        m_v4l2.close();
        connectSdkDevice(sdkDevice);
        return;
    }

    if (m_v4l2ScanTimer) {
        m_v4l2ScanTimer->stop();
    }
    if (m_deviceCallbackRegistered) {
        Devices::get().setDevChangedCallback(Devices::devChangedCallback{}, nullptr);
        m_deviceCallbackRegistered = false;
    }
    ++m_connectionGeneration;
    Devices::get().close();

    m_connected = true;
    m_v4l2Only = true;
    m_manualMovementAuthorized = false;
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
    qWarning() << "CameraController: SDK discovery grace expired; using fail-closed V4L2 fallback"
               << m_v4l2DevicePath;

    // Do not write discovery-time defaults. onCameraConnected() applies the
    // explicit Config intent; until then this backend remains observational.
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
    m_currentState.imageSettingsKnown = true;

    auto zoomRange = m_v4l2.getZoomRange();
    int zoomMax = zoomRange.valid ? zoomRange.max : 100;
    int zoomRaw = m_v4l2.getZoomAbsolute();
    m_currentState.zoom = (zoomMax > 0) ? (static_cast<double>(zoomRaw) / zoomMax + 1.0) : 1.0;
    m_currentState.zoomKnown = zoomRange.valid && zoomMax > 0;

    auto panRange = m_v4l2.getPanRange();
    auto tiltRange = m_v4l2.getTiltRange();
    if (panRange.valid && panRange.max != 0)
        m_currentState.pan = static_cast<double>(m_v4l2.getPanAbsolute()) / panRange.max;
    if (tiltRange.valid && tiltRange.max != 0)
        m_currentState.tilt = static_cast<double>(m_v4l2.getTiltAbsolute()) / tiltRange.max;
    m_currentState.panTiltKnown =
        panRange.valid && panRange.max != 0
        && tiltRange.valid && tiltRange.max != 0;

    m_currentState.autoFocusEnabled = m_v4l2.getAutoFocus();
    m_currentState.manualFocusValue = m_v4l2.getFocusAbsolute();

    emit stateChanged(m_currentState);
}

void CameraController::disconnectFromCamera()
{
    ++m_connectionGeneration;
    if (m_deviceCallbackRegistered) {
        Devices::get().setDevChangedCallback(Devices::devChangedCallback{}, nullptr);
        m_deviceCallbackRegistered = false;
    }
    m_pendingAiIntent.reset();
    m_autoFramingRequested = false;
    m_pendingTrackingProfile.reset();
    ++m_trackingIntentGeneration;
    m_pendingManualPosition.reset();
    m_aiStateConfirmed = false;
    m_manualMovementAuthorized = false;
    m_autoFramingModeTimer->stop();
    m_aiConfirmationTimer->stop();
    m_settlingTimer->stop();
    if (m_v4l2ScanTimer) {
        m_v4l2ScanTimer->stop();
    }

    if (m_connected) {
        if (m_v4l2Only) {
            m_v4l2.close();
            m_v4l2Only = false;
            m_v4l2DevicePath.clear();
        }
        m_device.reset();
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

bool CameraController::enterAutoFramingMediaMode()
{
    // cameraSetMediaModeU is a Meet-series API. Tiny tracking families use
    // their dedicated AI protocols.
    if (!m_connected || m_v4l2Only || !isMeetFamily()) return false;

    const bool success = executeCommand("Set MediaMode to AutoFrame", [this]() {
        return m_device->cameraSetMediaModeU(Device::MediaModeAutoFrame);
    });
    if (success) {
        setFocusAbsoluteUnchecked(0, true);
        m_currentState.autoFramingEnabled = true;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::enableAutoFraming(bool enabled)
{
    if (!m_connected || m_v4l2Only || !isMeetFamily()) return false;

    // Any ownership transition revokes raw movement immediately. Only the
    // complete tracking-off transaction may grant it again.
    m_manualMovementAuthorized = false;
    m_autoFramingRequested = enabled;
    m_autoFramingModeTimer->stop();

    if (enabled) {
        // Meet-series enable is a cancellable two-step transition.
        if (!enterAutoFramingMediaMode()) {
            m_autoFramingRequested = false;
            return false;
        }
        m_autoFramingModeTimer->start(500);
        return true;
    }

    const bool success = executeCommand("Disable AutoFraming", [this]() {
        return m_device->cameraSetMediaModeU(Device::MediaModeNormal);
    });
    if (success) {
        // Preserve the legacy immediate focus restoration. setTrackingState()
        // follows with the caller's exact retained snapshot before authorizing
        // movement.
        setFocusAbsoluteUnchecked(m_currentState.manualFocusValue, false);
        m_currentState.autoFramingEnabled = false;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setAiMode(int mode, int subMode)
{
    if (!m_connected || m_v4l2Only) return false;

    const auto previousIntent = m_pendingAiIntent;
    const int previousMode = m_currentState.aiMode;
    const int previousSubMode = m_currentState.aiSubMode;
    const bool previousTracking = m_currentState.autoFramingEnabled;
    const bool previousAiStateConfirmed = m_aiStateConfirmed;
    const bool confirmationWasActive = m_aiConfirmationTimer->isActive();
    m_aiConfirmationTimer->stop();

    const bool previousModeTracks = previousMode > Device::AiWorkModeNone
        && previousMode <= Device::AiWorkModeDesk;
    const bool requestedModeTracks = mode > Device::AiWorkModeNone
        && mode <= Device::AiWorkModeDesk;
    const int failSafeMode = previousModeTracks
        ? previousMode
        : (requestedModeTracks ? mode : Device::AiWorkModeHuman);
    const int failSafeSubMode = failSafeMode == Device::AiWorkModeHuman
        ? (previousMode == Device::AiWorkModeHuman ? previousSubMode
           : (mode == Device::AiWorkModeHuman ? subMode : Device::AiSubModeNormal))
        : 0;

    m_pendingAiIntent = PendingAiIntent{
        mode, subMode, failSafeMode, failSafeSubMode,
        m_trackingIntentGeneration};
    m_aiStateConfirmed = false;
    m_manualMovementAuthorized = false;
    const auto workMode = static_cast<Device::AiWorkModeType>(mode);
    const bool success = executeCommand("Set AI Mode", [this, workMode, subMode]() {
        return m_device->cameraSetAiModeU(workMode, subMode);
    });

    if (!success) {
        m_pendingAiIntent = previousIntent;
        if (m_pendingAiIntent) {
            // A superseding controller intent already invalidated the older
            // token. Resume confirmation under the current generation so an
            // older completion cannot become authoritative again.
            m_pendingAiIntent->intentGeneration = m_trackingIntentGeneration;
        }
        m_currentState.aiMode = previousMode;
        m_currentState.aiSubMode = previousSubMode;
        m_currentState.autoFramingEnabled = previousTracking;
        m_aiStateConfirmed = previousAiStateConfirmed;
        if (previousIntent && confirmationWasActive) {
            m_aiConfirmationTimer->start();
        }
        return false;
    }

    // Publish the requested state immediately, but keep the pending intent
    // until the SDK's lagging cameraStatus() cache confirms it.
    m_currentState.aiMode = mode;
    m_currentState.aiSubMode = subMode;
    m_currentState.autoFramingEnabled = (mode != Device::AiWorkModeNone);
    emit stateChanged(m_currentState);
    beginSettling(3000);
    return true;
}

CameraController::TrackingTransitionResult CameraController::setTrackingState(
    bool enabled, int aiMode, int aiSubMode,
    const TrackingModeProfile &profile)
{
    if (!m_connected || m_v4l2Only) {
        return {false, false, false, m_trackingIntentGeneration};
    }
    if (!isValidTrackingModeProfile(profile)
        || (!enabled
            && (profile.focusPolicy != TrackingFocusPolicy::Manual
                || profile.autoZoom))) {
        emit commandFailed("Invalid tracking profile", -1);
        return {false, false, false, m_trackingIntentGeneration};
    }

    const quint64 intentGeneration = ++m_trackingIntentGeneration;
    m_pendingManualPosition.reset();
    m_pendingTrackingProfile.reset();
    m_manualMovementAuthorized = false;
    emit trackingIntentStarted(intentGeneration);
    const auto reportSynchronousFailure = [this, intentGeneration]() {
        const bool observedTracking = isTiny2Family()
            ? m_currentState.aiMode != Device::AiWorkModeNone
            : m_currentState.autoFramingEnabled;
        emit trackingStateConfirmationFailed(
            observedTracking, intentGeneration);
    };

    if (isOriginalTinyFamily()) {
        m_pendingAiIntent.reset();
        const bool modeApplied = executeCommand(
            enabled ? "Enable AI Tracking" : "Disable AI Tracking",
            [this, enabled]() { return m_device->aiSetTargetSelectR(enabled); });
        const bool profileApplied = modeApplied
            && (enabled || applyTrackingProfile(profile, false));
        if (modeApplied) {
            m_currentState.autoFramingEnabled = enabled;
            m_currentState.aiMode = enabled
                ? Device::AiWorkModeHuman : Device::AiWorkModeNone;
            emit stateChanged(m_currentState);
        }
        if (profileApplied) {
            m_manualMovementAuthorized = !enabled;
            emit trackingStateConfirmed(enabled, intentGeneration);
        } else {
            reportSynchronousFailure();
        }
        return {
            modeApplied, profileApplied, false, intentGeneration
        };
    }

    if (isMeetFamily()) {
        m_pendingAiIntent.reset();
        m_autoFramingIntentGeneration = intentGeneration;
        const bool modeApplied = enableAutoFraming(enabled);
        const bool profileApplied = modeApplied
            && (enabled || applyTrackingProfile(profile, false));
        const bool confirmationPending = enabled && modeApplied;
        if (confirmationPending) {
            emit trackingStateConfirmationPending(
                enabled, intentGeneration);
        } else if (profileApplied) {
            m_manualMovementAuthorized = true;
            emit trackingStateConfirmed(false, intentGeneration);
        } else {
            reportSynchronousFailure();
        }
        return {
            modeApplied, profileApplied, confirmationPending,
            intentGeneration
        };
    }

    if (!isTiny2Family()) {
        emit commandFailed("Tracking is unsupported for this camera", -1);
        reportSynchronousFailure();
        return {false, false, false, intentGeneration};
    }

    if (m_pendingAiIntent) {
        m_pendingAiIntent->intentGeneration = intentGeneration;
    }
    m_pendingTrackingProfile = PendingTrackingProfile{
        profile, intentGeneration};

    int targetMode = enabled ? aiMode : Device::AiWorkModeNone;
    if (enabled && targetMode == Device::AiWorkModeNone) {
        targetMode = Device::AiWorkModeHuman;
    }
    const int targetSubMode = targetMode == Device::AiWorkModeHuman ? aiSubMode : 0;

    // Auto zoom is the pre-mode part of the same profile transaction. Speed
    // and focus wait for a fresh exact mode confirmation below.
    const bool previousAutoZoom = m_currentState.autoZoomEnabled;
    const bool targetAutoZoom = enabled && profile.autoZoom;
    const bool autoZoomApplied = setAutoZoom(targetAutoZoom);
    if (!autoZoomApplied) {
        m_pendingTrackingProfile.reset();
        const bool confirmationPending = m_pendingAiIntent.has_value();
        if (!confirmationPending) {
            reportSynchronousFailure();
        }
        return {false, false, confirmationPending, intentGeneration};
    }

    const bool modeApplied = setAiMode(targetMode, targetSubMode);
    if (!modeApplied) {
        m_pendingTrackingProfile.reset();
        const bool rollbackApplied = setAutoZoom(previousAutoZoom);
        const bool confirmationPending = m_pendingAiIntent.has_value();
        if (!rollbackApplied) {
            emit trackingStateConfirmationUncertain(intentGeneration);
        }
        if (!confirmationPending) {
            reportSynchronousFailure();
        }
        return {
            false, rollbackApplied, confirmationPending, intentGeneration
        };
    }

    emit trackingStateConfirmationPending(enabled, intentGeneration);
    return {true, true, true, intentGeneration};
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

bool CameraController::applyTrackingProfile(
    const TrackingModeProfile &profile, bool trackingEnabled)
{
    if (!isValidTrackingModeProfile(profile)) {
        emit commandFailed("Invalid tracking profile", -1);
        return false;
    }

    bool speedApplied = true;
    if (trackingEnabled) {
        speedApplied = setTrackSpeed(profile.trackSpeed);
    }

    bool firstFocusCommand = false;
    bool secondFocusCommand = false;
    switch (profile.focusPolicy) {
    case TrackingFocusPolicy::Face:
        // General autofocus supplies the focus motor behavior; face focus then
        // selects the face as the preferred target.
        firstFocusCommand = setFocusAbsoluteUnchecked(
            profile.manualFocusPosition, true);
        secondFocusCommand = setFaceFocus(true);
        break;
    case TrackingFocusPolicy::Continuous:
        firstFocusCommand = setFaceFocus(false);
        secondFocusCommand = setFocusAbsoluteUnchecked(
            profile.manualFocusPosition, true);
        break;
    case TrackingFocusPolicy::Manual:
        firstFocusCommand = setFaceFocus(false);
        secondFocusCommand = setFocusAbsoluteUnchecked(
            profile.manualFocusPosition, false);
        break;
    }

    return speedApplied && firstFocusCommand && secondFocusCommand;
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

bool CameraController::canApplyManualMovement() const
{
    return m_connected && !m_v4l2Only && m_device
        && m_manualMovementAuthorized;
}

bool CameraController::setPanTilt(double pan, double tilt)
{
    // V4L2 cannot confirm that autonomous tracking released gimbal ownership.
    // Enforce the fail-closed invariant at the transport boundary as well as
    // in the widget so no future caller can bypass it.
    if (!canApplyManualMovement()) return false;

    pan = qBound(-1.0, pan, 1.0);
    tilt = qBound(-1.0, tilt, 1.0);

    const bool success = executeCommand("Set Pan/Tilt", [this, pan, tilt]() {
        return m_device->cameraSetPanTiltAbsolute(pan, tilt);
    });

    if (success) {
        m_currentState.pan = pan;
        m_currentState.tilt = tilt;
        m_currentState.panTiltKnown = true;
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
    if (!canApplyManualMovement()) return false;

    zoom = qBound(1.0, zoom, 2.0);

    const float normalizedZoom = static_cast<float>(zoom);
    const bool success = executeCommand("Set Zoom", [this, normalizedZoom]() {
        // This SDK API accepts the same normalized 1.0-2.0 contract as Config
        // and the UI. The speed API is device-specific and was observed to
        // acknowledge Tiny 2 commands without changing zoom.
        return m_device->cameraSetZoomAbsoluteR(normalizedZoom);
    });

    if (success) {
        m_currentState.zoom = zoom;
        m_currentState.zoomKnown = true;
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

    const bool success = executeCommand(
        enabled ? "Enable Face Focus" : "Disable Face Focus",
        [this, enabled]() { return m_device->cameraSetFaceFocusR(enabled); });
    if (success) {
        m_currentState.faceFocusEnabled = enabled;
        emit stateChanged(m_currentState);
    }
    return success;
}

bool CameraController::setFocusAbsolute(int position, bool autoFocus)
{
    if (!canApplyManualMovement()) return false;
    return setFocusAbsoluteUnchecked(position, autoFocus);
}

bool CameraController::setFocusAbsoluteUnchecked(
    int position, bool autoFocus)
{
    if (!m_connected || m_v4l2Only || !m_device) return false;
    position = qBound(0, position, 100);

    const bool success = executeCommand("Set Focus", [this, position, autoFocus]() {
        return m_device->cameraSetFocusAbsolute(position, autoFocus);
    });
    if (success) {
        m_currentState.autoFocusEnabled = autoFocus;
        m_currentState.manualFocusValue = position;
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
    bool freshStatusRead = false;
    if (isTiny2Family()) {
        Device::CameraStatus freshStatus{};
        if (m_device->cameraGetCameraStatusU(freshStatus) == 0) {
            status = freshStatus;
            freshStatusRead = true;
        }
    }

    const int reportedAiMode = status.tiny.ai_mode;
    const int reportedAiSubMode = status.tiny.ai_sub_mode;
    const bool reportedModeIsStable = reportedAiMode >= Device::AiWorkModeNone
        && reportedAiMode <= Device::AiWorkModeDesk;
    if (m_pendingAiIntent) {
        const bool modeMatches = reportedAiMode == m_pendingAiIntent->mode;
        const bool subModeMatches = m_pendingAiIntent->mode != Device::AiWorkModeHuman
            || reportedAiSubMode == m_pendingAiIntent->subMode;
        if (freshStatusRead && modeMatches && subModeMatches) {
            const quint64 intentGeneration =
                m_pendingAiIntent->intentGeneration;
            const bool trackingEnabled =
                reportedAiMode != Device::AiWorkModeNone;
            const bool hasMatchingProfile = m_pendingTrackingProfile
                && m_pendingTrackingProfile->intentGeneration
                    == intentGeneration;
            const TrackingModeProfile profile = hasMatchingProfile
                ? m_pendingTrackingProfile->profile
                : TrackingModeProfile{};

            m_currentState.aiMode = reportedAiMode;
            m_currentState.aiSubMode = reportedAiSubMode;
            m_pendingAiIntent.reset();
            m_pendingTrackingProfile.reset();
            m_aiStateConfirmed = true;

            // Focus and speed are allowed only after this fresh exact mode
            // confirmation. The terminal tracking signal now means the whole
            // profile transaction—not merely the AI mode—has completed.
            const bool profileApplied = hasMatchingProfile
                && applyTrackingProfile(profile, trackingEnabled);
            if (profileApplied) {
                // The off confirmation is not terminal until face focus is
                // disabled and the retained manual snapshot has succeeded.
                m_manualMovementAuthorized = !trackingEnabled;
                emit trackingStateConfirmed(
                    trackingEnabled, intentGeneration);
                if (!trackingEnabled) {
                    schedulePendingManualPosition();
                }
            } else {
                m_manualMovementAuthorized = false;
                m_pendingManualPosition.reset();
                emit trackingStateConfirmationFailed(
                    trackingEnabled, intentGeneration);
                if (!hasMatchingProfile) {
                    emit commandFailed("Apply Tracking Profile", -1);
                }
            }
        } else {
            PendingAiIntent &intent = *m_pendingAiIntent;
            ++intent.confirmationAttempts;
            if (freshStatusRead && reportedModeIsStable
                && (intent.confirmationAttempts >= 3 || intent.failureReported)) {
                const bool failureAlreadyReported = intent.failureReported;
                const quint64 intentGeneration = intent.intentGeneration;
                m_currentState.aiMode = reportedAiMode;
                m_currentState.aiSubMode = reportedAiSubMode;
                m_pendingAiIntent.reset();
                m_aiStateConfirmed = true;
                m_manualMovementAuthorized = false;
                m_pendingManualPosition.reset();
                m_pendingTrackingProfile.reset();
                emit trackingStateConfirmationFailed(
                    reportedAiMode != Device::AiWorkModeNone,
                    intentGeneration);
                if (!failureAlreadyReported) {
                    emit commandFailed("Confirm AI Mode", -1);
                }
            } else if (intent.confirmationAttempts >= 3) {
                // A failed fresh read or a transitional/sentinel mode is not
                // evidence that AI released the gimbal. Keep retrying fresh
                // reads, but fail closed until a stable mode is observed.
                m_currentState.aiMode = intent.failSafeMode;
                m_currentState.aiSubMode = intent.failSafeSubMode;
                m_aiStateConfirmed = false;
                m_manualMovementAuthorized = false;
                if (!intent.failureReported) {
                    intent.failureReported = true;
                    emit trackingStateConfirmationUncertain(
                        intent.intentGeneration);
                    emit commandFailed("Confirm AI Mode", -1);
                }
            }
        }
        // Until a successful fresh read confirms or rejects the intent, retain
        // explicit operator intent (or the conservative tracking-on fallback).
    } else if (!isTiny2Family() || freshStatusRead) {
        if (reportedModeIsStable) {
            m_currentState.aiMode = reportedAiMode;
            m_currentState.aiSubMode = reportedAiSubMode;
            if (isTiny2Family()) {
                // A fresh unsolicited observation may update displayed state,
                // but it is not completion evidence for an already-resolved
                // controller intent and therefore carries no generation token.
                const bool trackingEnabled =
                    reportedAiMode != Device::AiWorkModeNone;
                m_aiStateConfirmed = true;
                bool ownershipReady = trackingEnabled;
                if (trackingEnabled) {
                    m_manualMovementAuthorized = false;
                } else if (m_manualMovementAuthorized) {
                    ownershipReady = true;
                } else {
                    TrackingModeProfile manualProfile =
                        m_config.getSettings().activeTrackingProfile;
                    manualProfile.focusPolicy = TrackingFocusPolicy::Manual;
                    manualProfile.autoZoom = false;
                    ownershipReady = applyTrackingProfile(
                        manualProfile, false);
                    m_manualMovementAuthorized = ownershipReady;
                }
                if (ownershipReady) {
                    emit trackingOwnershipObserved(trackingEnabled);
                }
            }
        } else {
            qDebug() << "CameraController: ignoring transitional AI mode"
                     << reportedAiMode;
        }
    }
    // A failed Tiny 2 fresh-status read never falls back to the SDK's lagging
    // cameraStatus() cache for AI ownership.
    m_currentState.zoomRatio = status.tiny.zoom_ratio;
    // Derive zoom float from zoom_ratio (100 = 1.0x, 200 = 2.0x)
    if (status.tiny.zoom_ratio >= 100 && status.tiny.zoom_ratio <= 200) {
        m_currentState.zoom = status.tiny.zoom_ratio / 100.0;
        m_currentState.zoomKnown = !isTiny2Family() || freshStatusRead;
    } else {
        m_currentState.zoomKnown = false;
        qDebug() << "CameraController: unexpected zoom_ratio" << status.tiny.zoom_ratio
                 << "— keeping previous zoom" << m_currentState.zoom;
    }

    if (isTiny2Family()) {
        Device::AiGimbalStateInfo gimbal{};
        if (m_device->aiGetGimbalStateR(&gimbal) == 0
            && std::isfinite(gimbal.yaw_motor)
            && std::isfinite(gimbal.pitch_motor)) {
            m_currentState.pan = qBound(
                -1.0, static_cast<double>(gimbal.yaw_motor) / 180.0, 1.0);
            m_currentState.tilt = qBound(
                -1.0, static_cast<double>(gimbal.pitch_motor) / 90.0, 1.0);
            m_currentState.panTiltKnown = true;
        } else {
            m_currentState.panTiltKnown = false;
        }
    }
    m_currentState.hdrEnabled = status.tiny.hdr;
    m_currentState.faceAEEnabled = status.tiny.face_ae;
    m_currentState.faceFocusEnabled = status.tiny.face_auto_focus;
    m_currentState.autoFocusEnabled = status.tiny.auto_focus;
    m_currentState.manualFocusValue = status.tiny.manual_focus_value;
    m_currentState.fovMode = status.tiny.fov;
    m_currentState.devStatus = status.tiny.dev_status;
    m_currentState.autoFramingEnabled = (m_currentState.aiMode != Device::AiWorkModeNone);
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

    const bool brightnessRead =
        m_device->cameraGetImageBrightnessR(brightness) == 0;
    const bool contrastRead =
        m_device->cameraGetImageContrastR(contrast) == 0;
    const bool saturationRead =
        m_device->cameraGetImageSaturationR(saturation) == 0;
    const bool whiteBalanceRead =
        m_device->cameraGetWhiteBalanceR(wbType, wbParam) == 0;
    if (brightnessRead) {
        m_currentState.brightness = clampToRange(brightness, m_brightnessRange, 0, 255);
    }
    if (contrastRead) {
        m_currentState.contrast = clampToRange(contrast, m_contrastRange, 0, 255);
    }
    if (saturationRead) {
        m_currentState.saturation = clampToRange(saturation, m_saturationRange, 0, 255);
    }
    if (whiteBalanceRead) {
        m_currentState.whiteBalance = static_cast<int>(wbType);
        if (wbType == Device::DevWhiteBalanceManual) {
            m_currentState.whiteBalanceKelvin = clampToRange(wbParam, m_whiteBalanceKelvinRange, 2000, 10000);
        } else if (m_whiteBalanceKelvinRange.valid) {
            m_currentState.whiteBalanceKelvin = clampToRange(m_whiteBalanceKelvinRange.defaultValue, m_whiteBalanceKelvinRange, 2000, 10000);
        }
    }

    // Restore auto mode flags (not stored in camera)
    m_currentState.imageSettingsKnown =
        brightnessRead && contrastRead && saturationRead && whiteBalanceRead
        && (!isTiny2Family() || freshStatusRead);

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

void CameraController::schedulePendingManualPosition()
{
    if (!m_pendingManualPosition || !m_connected || m_v4l2Only
        || !m_aiStateConfirmed
        || m_currentState.aiMode != Device::AiWorkModeNone) {
        return;
    }

    const PendingManualPosition pending = *m_pendingManualPosition;
    QTimer::singleShot(0, this, [this, pending]() {
        if (!m_pendingManualPosition || !m_connected || m_v4l2Only
            || pending.trackingIntentGeneration != m_trackingIntentGeneration
            || pending.connectionGeneration != m_connectionGeneration
            || !m_aiStateConfirmed
            || m_currentState.aiMode != Device::AiWorkModeNone) {
            return;
        }
        m_pendingManualPosition.reset();
        if (pending.applyZoom && !setZoom(pending.zoom)) {
            return;
        }
        if (pending.applyPanTilt
            && !setPanTilt(pending.pan, pending.tilt)) {
            return;
        }
        if (pending.focus) {
            if (*pending.focus >= 0) {
                setFocusAbsolute(*pending.focus, false);
            } else {
                setFocusAbsolute(0, true);
            }
        }
    });
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
    // Config is the explicit persisted-intent model. Live SDK telemetry is
    // deliberately never promoted here.
    return m_config.save();
}

void CameraController::applyConfigToCamera()
{
    if (!m_connected) return;

    auto settings = m_config.getSettings();

    if (settings.imageIntentDefined) {
        m_currentState.brightnessAuto = settings.brightnessAuto;
        m_currentState.contrastAuto = settings.contrastAuto;
        m_currentState.saturationAuto = settings.saturationAuto;
    }

    // Apply all settings to the camera
    const auto trackingResult = setTrackingState(
        settings.faceTracking, settings.aiMode,
        settings.aiSubMode, settings.activeTrackingProfile);
    if (settings.imageIntentDefined) {
        setHDR(settings.hdr);
        setFOV(settings.fov);
        setFaceAE(settings.faceAE);
        if (!isTiny2Family()) {
            setFaceFocus(settings.faceFocus);
        }
    }
    if (!settings.faceTracking && trackingResult.trackingModeApplied) {
        if (trackingResult.confirmationPending) {
            m_pendingManualPosition = PendingManualPosition{
                settings.pan,
                settings.tilt,
                settings.zoom,
                settings.panTiltIntentDefined,
                settings.zoomIntentDefined,
                std::nullopt,
                m_trackingIntentGeneration,
                m_connectionGeneration
            };
        } else {
            bool positionApplied = true;
            if (settings.zoomIntentDefined) {
                positionApplied = setZoom(settings.zoom);
            }
            if (positionApplied && settings.panTiltIntentDefined) {
                positionApplied = setPanTilt(settings.pan, settings.tilt);
            }
            if (positionApplied) {
                if (settings.focus >= 0) {
                    setFocusAbsolute(settings.focus, false);
                } else {
                    setFocusAbsolute(0, true);
                }
            }
        }
    }

    if (isTiny2Family() && settings.imageIntentDefined) {
        // Tiny 2 speed and focus are applied only after exact AI-mode
        // confirmation as part of activeTrackingProfile.
        setAudioAutoGain(settings.audioAutoGain);
    }

    if (settings.imageIntentDefined) {
        setBrightness(settings.brightness);
        setContrast(settings.contrast);
        setSaturation(settings.saturation);
        if (settings.whiteBalance == static_cast<int>(Device::DevWhiteBalanceManual)) {
            setWhiteBalanceManual(settings.whiteBalanceKelvin);
        } else {
            setWhiteBalance(settings.whiteBalance);
        }
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

    // Apply the current UI state to camera (respects user changes). A remembered
    // nonzero AI mode must never override an explicit manual/off state.
    TrackingModeProfile profile = legacyTrackingModeProfile(
        uiState.faceFocusEnabled,
        uiState.autoFocusEnabled ? -1 : uiState.manualFocusValue,
        uiState.autoZoomEnabled,
        uiState.trackSpeedMode);
    if (!uiState.autoFramingEnabled) {
        profile.focusPolicy = TrackingFocusPolicy::Manual;
        profile.manualFocusPosition = uiState.manualFocusValue;
        profile.autoZoom = false;
    }
    const auto trackingResult = setTrackingState(
        uiState.autoFramingEnabled, uiState.aiMode,
        uiState.aiSubMode, profile);
    if (isTiny2Family()) {
        setAudioAutoGain(uiState.audioAutoGainEnabled);
    }
    setHDR(uiState.hdrEnabled);
    setFOV(uiState.fovMode);
    setFaceAE(uiState.faceAEEnabled);
    if (!isTiny2Family()) {
        setFaceFocus(uiState.faceFocusEnabled);
    }
    if (!uiState.autoFramingEnabled && trackingResult.trackingModeApplied) {
        if (trackingResult.confirmationPending) {
            m_pendingManualPosition = PendingManualPosition{
                uiState.pan,
                uiState.tilt,
                uiState.zoom,
                true,
                true,
                std::nullopt,
                m_trackingIntentGeneration,
                m_connectionGeneration
            };
        } else if (setZoom(uiState.zoom)) {
            setPanTilt(uiState.pan, uiState.tilt);
        }
    }

    // Image controls
    setBrightness(uiState.brightness);
    setContrast(uiState.contrast);
    setSaturation(uiState.saturation);
    if (uiState.whiteBalance == static_cast<int>(Device::DevWhiteBalanceManual)) {
        setWhiteBalanceManual(uiState.whiteBalanceKelvin);
    } else {
        setWhiteBalance(uiState.whiteBalance);
    }
}

bool CameraController::isTiny2Family() const
{
    return m_cameraInfo.productType == ObsbotProdTiny2 ||
           m_cameraInfo.productType == ObsbotProdTiny2Lite ||
           m_cameraInfo.productType == ObsbotProdTinySE;
}

bool CameraController::isOriginalTinyFamily() const
{
    return m_cameraInfo.productType == ObsbotProdTiny
        || m_cameraInfo.productType == ObsbotProdTiny4k;
}

bool CameraController::isMeetFamily() const
{
    return m_cameraInfo.productType == ObsbotProdMeet
        || m_cameraInfo.productType == ObsbotProdMeet4k
        || m_cameraInfo.productType == ObsbotProdMeet2
        || m_cameraInfo.productType == ObsbotProdMeetSE;
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
