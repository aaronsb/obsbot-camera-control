#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QMap>
#include <memory>
#include <functional>
#include <vector>
#include <dev/devs.hpp>
#include "Config.h"
#include "V4l2Backend.h"

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

        // Image settings
        bool hdrEnabled;
        int fovMode;
        bool faceAEEnabled;
        bool faceFocusEnabled;
        bool autoFocusEnabled;
        int manualFocusValue;  // 0-100, motor position when in manual focus
        int trackingStyle;
        bool exposureAuto;
        int exposure;
        int antiFlicker;
        int uvcExposure;
        int gain;
        int backlightCompensation;

        // Image controls
        bool brightnessAuto; // Auto mode for brightness
        int brightness;      // 0-255
        bool contrastAuto;   // Auto mode for contrast
        int contrast;        // 0-255
        bool saturationAuto; // Auto mode for saturation
        int saturation;      // 0-255
        int hue;
        int sharpness;
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
    void connectToCamera();
    void connectToCamera(const QString &devicePath);
    void disconnectFromCamera();
    QString getVideoDevicePath() const;
    QMap<QString, QString> getSerialsByDevicePath() const;

    // State
    CameraState getCurrentState();
    bool hasTiny2Capabilities() const;
    bool hasOriginalTinyCapabilities() const { return isOriginalTinyFamily(); }
    bool hasTiny4kCapabilities() const { return isTiny4k(); }

    // Tracking controls
    bool enableAutoFraming(bool enabled);
    bool setAiMode(int mode, int subMode);
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
    bool setTrackingStyle(int style);
    bool setExposure(int shutterTime, bool automatic);
    bool setAntiFlicker(int frequency);
    bool setGestureControl(int gesture, bool enabled);
    bool setHardwareMirror(bool enabled);
    bool setMicrophoneDuringSleep(bool enabled);
    bool setSleepTimeout(int seconds);
    bool setDeviceAwake(bool awake);
    bool setAiEnabled(bool enabled);
    bool setVerticalMode(bool enabled);
    bool restoreFactorySettings();
    bool setCurrentViewAsBootPosition();
    bool saveHardwarePreset(int id);
    bool recallHardwarePreset(int id);
    bool setGimbalSpeed(double pitch, double pan);
    bool setTiny4kExposure(int value);
    bool setTiny4kGain(int value);
    bool setTiny4kBacklightCompensation(int value);

    // Image controls
    void setBrightnessAuto(bool enabled) { m_currentState.brightnessAuto = enabled; }
    bool setBrightness(int value);  // 0-255
    void setContrastAuto(bool enabled) { m_currentState.contrastAuto = enabled; }
    bool setContrast(int value);    // 0-255
    void setSaturationAuto(bool enabled) { m_currentState.saturationAuto = enabled; }
    bool setSaturation(int value);  // 0-255
    bool setHue(int value);
    bool setSharpness(int value);
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
    ParamRange getHueRange() const { return m_hueRange; }
    ParamRange getSharpnessRange() const { return m_sharpnessRange; }
    ParamRange getExposureRange() const { return m_exposureRange; }
    ParamRange getAntiFlickerRange() const { return m_antiFlickerRange; }
    ParamRange getTiny4kExposureRange() const { return m_uvcExposureRange; }
    ParamRange getGainRange() const { return m_gainRange; }
    ParamRange getBacklightCompensationRange() const { return m_backlightRange; }
    ParamRange getWhiteBalanceKelvinRange() const { return m_whiteBalanceKelvinRange; }
    const std::vector<int>& getSupportedWhiteBalanceTypes() const { return m_supportedWhiteBalanceTypes; }

signals:
    void cameraConnected(const CameraInfo &info);
    void cameraDisconnected();
    void stateChanged(const CameraState &state);
    void commandFailed(const QString &description, int errorCode);
    void configLoaded();  // Emitted after config is successfully loaded

private:
    std::shared_ptr<Device> m_device;
    bool m_connected;
    QString m_selectedDevicePath;
    quint64 m_connectionAttempt = 0;
    bool m_v4l2Only = false;
    V4l2Backend m_v4l2;
    QTimer *m_v4l2ScanTimer = nullptr;
    QString m_v4l2DevicePath;
    CameraInfo m_cameraInfo;
    CameraState m_currentState;
    CameraState m_cachedState;  // Cache intended state during settling
    Config m_config;
    QTimer *m_settlingTimer;  // Timer for settling period after config apply
    ParamRange m_brightnessRange;
    ParamRange m_contrastRange;
    ParamRange m_saturationRange;
    ParamRange m_hueRange;
    ParamRange m_sharpnessRange;
    ParamRange m_exposureRange;
    ParamRange m_antiFlickerRange;
    ParamRange m_uvcExposureRange;
    ParamRange m_gainRange;
    ParamRange m_backlightRange;
    ParamRange m_whiteBalanceKelvinRange;
    std::vector<int> m_supportedWhiteBalanceTypes;
    int m_lastRequestedWhiteBalance;
    bool m_whiteBalanceFallbackActive;
    int m_fallbackWhiteBalanceMode;
    bool isTiny2Family() const;
    bool isOriginalTinyFamily() const;
    bool isTiny4k() const;
    void tryV4l2Fallback();
    void connectV4l2(const std::string &devicePath);
    void refreshV4l2ControlRanges();
    void updateV4l2State();

    // Helper
    bool executeCommand(const QString &description, std::function<int32_t()> command);
    void updateState();
    void saveCurrentStateToConfig();  // Update config with current camera state
    void refreshControlRanges();
    void resetControlRanges();
    int clampToRange(int value, const ParamRange &range, int fallbackMin, int fallbackMax) const;
    int whiteBalancePresetToKelvin(int mode) const;
    bool applyManualWhiteBalance(int kelvin, int displayMode);
    bool isWhiteBalanceTypeSupported(int mode) const;
};

#endif // CAMERACONTROLLER_H
