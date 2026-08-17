#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QMap>
#include <memory>
#include <functional>
#include <optional>
#include <vector>
#include <dev/devs.hpp>
#include "Config.h"
#include "V4l2Backend.h"

struct CameraControllerTestAccess;

/**
 * @brief Handles all camera communication and state management
 *
 * This class encapsulates the OBSBOT SDK and provides a clean Qt-friendly
 * interface for controlling the camera.
 */
class CameraController : public QObject
{
    Q_OBJECT

public:
    struct CameraInfo {
        QString name;
        QString serialNumber;
        QString version;
        int productType;
        bool connected;
    };

    struct CameraState {
        // Tracking
        bool autoFramingEnabled;
        int aiMode;
        int aiSubMode;
        bool autoZoomEnabled;
        int trackSpeedMode;
        bool audioAutoGainEnabled;

        // PTZ
        double pan;
        double tilt;
        double zoom;
        bool panTiltKnown;
        bool zoomKnown;
        bool imageSettingsKnown;

        // Image settings
        bool hdrEnabled;
        int fovMode;
        bool faceAEEnabled;
        bool faceFocusEnabled;
        bool autoFocusEnabled;
        int manualFocusValue;  // 0-100, motor position when in manual focus

        // Image controls
        bool brightnessAuto; // Auto mode for brightness
        int brightness;      // 0-255
        bool contrastAuto;   // Auto mode for contrast
        int contrast;        // 0-255
        bool saturationAuto; // Auto mode for saturation
        int saturation;      // 0-255
        int whiteBalance;    // 0=Auto, 1=Daylight, etc.
        int whiteBalanceKelvin; // Manual Kelvin value when white balance is manual

        // Status
        int zoomRatio;
        int devStatus;
    };

    struct ParamRange {
        int min = 0;
        int max = 0;
        int step = 1;
        int defaultValue = 0;
        bool valid = false;
    };

    explicit CameraController(QObject *parent = nullptr);
    ~CameraController();

    // Connection
    bool isConnected() const { return m_connected; }
    bool isV4l2Only() const { return m_v4l2Only; }
    CameraInfo getCameraInfo() const { return m_cameraInfo; }
    void selectCameraTarget(
        const QString &devicePath, const QString &serialNumber = QString());
    void connectToCamera();
    void connectToCamera(
        const QString &devicePath,
        const QString &serialNumber = QString());
    void disconnectFromCamera();
    QString getVideoDevicePath() const;
    QString selectedDevicePath() const { return m_selectedDevicePath; }
    QString selectedDeviceSerial() const { return m_selectedDeviceSerial; }
    QMap<QString, QString> getSerialsByDevicePath() const;

    // State
    CameraState getCurrentState();
    bool hasTiny2Capabilities() const;

    // Tracking controls
    struct TrackingTransitionResult {
        bool trackingModeApplied = false;
        bool autoZoomApplied = true;
        bool confirmationPending = false;
        quint64 intentGeneration = 0;
        bool complete() const { return trackingModeApplied && autoZoomApplied; }
    };

    bool enableAutoFraming(bool enabled);
    bool enterAutoFramingMediaMode();
    bool setAiMode(int mode, int subMode);
    TrackingTransitionResult setTrackingState(
        bool enabled, int aiMode, int aiSubMode,
        const TrackingModeProfile &profile);
    bool setAutoZoom(bool enabled);
    bool setTrackSpeed(int speedMode);
    bool setAudioAutoGain(bool enabled);

    // PTZ controls
    bool setPanTilt(double pan, double tilt);
    bool adjustPan(double delta);
    bool adjustTilt(double delta);
    bool setZoom(double zoom);
    bool centerView();

    // Camera settings
    bool setHDR(bool enabled);
    bool setFOV(int fovMode);  // 0=Wide, 1=Medium, 2=Narrow
    bool setFaceAE(bool enabled);
    bool setFaceFocus(bool enabled);
    bool setFocusAbsolute(int position, bool autoFocus);

    // Image controls
    void setBrightnessAuto(bool enabled) { m_currentState.brightnessAuto = enabled; }
    bool setBrightness(int value);  // 0-255
    void setContrastAuto(bool enabled) { m_currentState.contrastAuto = enabled; }
    bool setContrast(int value);    // 0-255
    void setSaturationAuto(bool enabled) { m_currentState.saturationAuto = enabled; }
    bool setSaturation(int value);  // 0-255
    bool setWhiteBalance(int mode); // 0=Auto, 1=Daylight, etc.
    bool setWhiteBalanceManual(int kelvin);

    // Configuration
    bool loadConfig(std::vector<Config::ValidationError> &errors);
    bool saveConfig();
    void applyConfigToCamera();  // Apply loaded config settings to camera
    void applyCurrentStateToCamera(const CameraState &uiState);  // Apply UI state to camera
    Config& getConfig() { return m_config; }

    // Settling state
    bool isSettling() const { return m_settlingTimer && m_settlingTimer->isActive(); }
    void beginSettling(int durationMs = 2000);  // Start settling period

    // Ranges
    ParamRange getBrightnessRange() const { return m_brightnessRange; }
    ParamRange getContrastRange() const { return m_contrastRange; }
    ParamRange getSaturationRange() const { return m_saturationRange; }
    ParamRange getWhiteBalanceKelvinRange() const { return m_whiteBalanceKelvinRange; }
    const std::vector<int>& getSupportedWhiteBalanceTypes() const { return m_supportedWhiteBalanceTypes; }

signals:
    void cameraConnected(const CameraInfo &info);
    void cameraDisconnected();
    void stateChanged(const CameraState &state);
    void commandFailed(const QString &description, int errorCode);
    void trackingIntentStarted(quint64 intentGeneration);
    void trackingStateConfirmationPending(
        bool trackingEnabled, quint64 intentGeneration);
    void trackingStateConfirmationFailed(
        bool trackingEnabled, quint64 intentGeneration);
    void trackingStateConfirmationUncertain(quint64 intentGeneration);
    void trackingStateConfirmed(
        bool trackingEnabled, quint64 intentGeneration);
    void trackingOwnershipObserved(bool trackingEnabled);
    void configLoaded();  // Emitted after config is successfully loaded

private:
    struct DeviceCallbackGate;
    friend struct CameraControllerTestAccess;
    std::shared_ptr<DeviceCallbackGate> m_deviceCallbackGate;
    std::shared_ptr<Device> m_device;
    bool m_connected;
    bool m_deviceCallbackRegistered = false;
    quint64 m_connectionGeneration = 0;
    QString m_selectedDevicePath;
    QString m_selectedDeviceSerial;
    bool m_v4l2Only = false;
    V4l2Backend m_v4l2;
    QTimer *m_v4l2ScanTimer = nullptr;
    quint64 m_v4l2FallbackGeneration = 0;
    QString m_v4l2DevicePath;
    CameraInfo m_cameraInfo;
    CameraState m_currentState;
    CameraState m_cachedState;  // Cache intended state during settling
    Config m_config;
    QTimer *m_settlingTimer;  // Timer for settling period after config apply
    QTimer *m_aiConfirmationTimer;  // Fresh Tiny 2 status retries after settling
    QTimer *m_autoFramingModeTimer;  // Owned Meet-series delayed enable step
    bool m_autoFramingRequested = false;
    quint64 m_autoFramingIntentGeneration = 0;
    struct PendingAiIntent {
        int mode;
        int subMode;
        int failSafeMode;
        int failSafeSubMode;
        quint64 intentGeneration;
        int confirmationAttempts = 0;
        bool failureReported = false;
    };
    std::optional<PendingAiIntent> m_pendingAiIntent;
    struct PendingTrackingProfile {
        TrackingModeProfile profile;
        quint64 intentGeneration;
    };
    std::optional<PendingTrackingProfile> m_pendingTrackingProfile;
    bool m_aiStateConfirmed = false;
    // Granted only after this exact SDK connection has positively confirmed
    // tracking off and restored the retained manual-focus state.
    bool m_manualMovementAuthorized = false;
    quint64 m_trackingIntentGeneration = 0;
    struct PendingManualPosition {
        double pan;
        double tilt;
        double zoom;
        bool applyPanTilt;
        bool applyZoom;
        std::optional<int> focus;
        quint64 trackingIntentGeneration;
        quint64 connectionGeneration;
    };
    std::optional<PendingManualPosition> m_pendingManualPosition;
    ParamRange m_brightnessRange;
    ParamRange m_contrastRange;
    ParamRange m_saturationRange;
    ParamRange m_whiteBalanceKelvinRange;
    std::vector<int> m_supportedWhiteBalanceTypes;
    int m_lastRequestedWhiteBalance;
    bool m_whiteBalanceFallbackActive;
    int m_fallbackWhiteBalanceMode;
    bool isTiny2Family() const;
    bool isOriginalTinyFamily() const;
    bool isMeetFamily() const;
    std::shared_ptr<Device> selectedSdkDevice() const;
    bool connectSdkDevice(const std::shared_ptr<Device> &device);
    bool tryConnectSdkDevice();
    void tryV4l2Fallback();
    void connectV4l2(const std::string &devicePath);
    void refreshV4l2ControlRanges();
    void updateV4l2State();

    // Helper
    bool executeCommand(const QString &description, std::function<int32_t()> command);
    void updateState();
    bool applyTrackingProfile(
        const TrackingModeProfile &profile, bool trackingEnabled);
    bool canApplyManualMovement() const;
    bool setFocusAbsoluteUnchecked(int position, bool autoFocus);
    void schedulePendingManualPosition();
    void refreshControlRanges();
    void resetControlRanges();
    int clampToRange(int value, const ParamRange &range, int fallbackMin, int fallbackMax) const;
    int whiteBalancePresetToKelvin(int mode) const;
    bool applyManualWhiteBalance(int kelvin, int displayMode);
    bool isWhiteBalanceTypeSupported(int mode) const;
};

#endif // CAMERACONTROLLER_H
