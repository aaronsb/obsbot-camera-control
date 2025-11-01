#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <map>
#include <vector>
#include <array>

/**
 * @brief Configuration manager for OBSBOT camera settings
 *
 * Reads and writes XDG-compliant config files with validation.
 * Format: Simple key=value pairs with # comments
 * Location: $XDG_CONFIG_HOME/obsbot-control/settings.conf
 */
class Config
{
public:
    enum ValidationResult {
        Valid,
        MissingProperty,
        UnknownProperty,
        InvalidValue,
        MalformedLine
    };

    struct ValidationError {
        ValidationResult type;
        std::string message;
        int lineNumber;
    };

    struct CameraSettings {
        struct PresetSlot {
            bool defined;
            double pan;
            double tilt;
            double zoom;
        };

        bool faceTracking;
        bool hdr;
        int fov;              // 0=Wide, 1=Medium, 2=Narrow
        bool faceAE;
        bool faceFocus;
        double zoom;          // 1.0 - 2.0
        double pan;           // -1.0 to 1.0
        double tilt;          // -1.0 to 1.0

        // AI / Tracking
        int aiMode;           // Device::AiWorkModeType
        int aiSubMode;        // Device::AiSubModeType
        bool autoZoom;        // Enable adaptive auto zoom
        int trackSpeed;       // Device::AiTrackSpeedType

        // Image controls
        bool brightnessAuto;  // Auto mode for brightness
        int brightness;       // Typically 0-255 or similar range
        bool contrastAuto;    // Auto mode for contrast
        int contrast;         // Typically 0-255 or similar range
        bool saturationAuto;  // Auto mode for saturation
        int saturation;       // Typically 0-255 or similar range
        int whiteBalance;     // 0=Auto, 1=Daylight, 2=Fluorescent, etc.
        int whiteBalanceKelvin; // Manual Kelvin value (when whiteBalance==255)

        // Audio
        bool audioAutoGain;   // Enable auto gain control for microphones

        // Preview / video
        std::string previewFormat; // Encoded as "widthxheight@fps" or "auto"

        std::array<PresetSlot, 3> presets;

        // Application settings
        bool startMinimized;  // Start application minimized to tray
        bool virtualCameraEnabled;
        std::string virtualCameraDevice;
        std::string virtualCameraResolution;
    };

    Config();
    ~Config();

    /**
     * @brief Load configuration from disk
     * @param errors Output parameter for validation errors
     * @return true if loaded successfully, false if validation failed
     */
    bool load(std::vector<ValidationError> &errors);

    /**
     * @brief Save current settings to disk
     * @return true if saved successfully
     */
    bool save();

    /**
     * @brief Reset to default settings and optionally save
     * @param saveToFile If true, writes defaults to disk
     * @return true if successful
     */
    bool resetToDefaults(bool saveToFile = false);

    /**
     * @brief Get current camera settings
     */
    CameraSettings getSettings() const { return m_settings; }

    /**
     * @brief Set camera settings
     */
    void setSettings(const CameraSettings &settings) { m_settings = settings; }

    /**
     * @brief Get config file path
     */
    std::string getConfigPath() const;

    /**
     * @brief Check if config file exists
     */
    bool configExists() const;

    /**
     * @brief Check if saving is enabled (false if user declined to fix invalid config)
     */
    bool isSavingEnabled() const { return m_savingEnabled; }

    /**
     * @brief Disable saving (called when user declines to fix config)
     */
    void disableSaving() { m_savingEnabled = false; }

private:
    CameraSettings m_settings;
    bool m_savingEnabled;

    void setDefaults();
    bool parseLine(const std::string &line, int lineNumber, std::vector<ValidationError> &errors);
    bool validateSettings(std::vector<ValidationError> &errors);
    std::string getXdgConfigHome() const;
};

#endif // CONFIG_H
