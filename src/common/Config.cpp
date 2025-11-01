#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <unordered_set>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>

Config::Config()
    : m_savingEnabled(true)
{
    setDefaults();
}

Config::~Config()
{
}

void Config::setDefaults()
{
    m_settings.faceTracking = false;  // Default to off for safety
    m_settings.hdr = false;
    m_settings.fov = 0;               // Wide
    m_settings.faceAE = false;
    m_settings.faceFocus = false;
    m_settings.zoom = 1.0;            // No zoom
   m_settings.pan = 0.0;             // Centered
   m_settings.tilt = 0.0;            // Centered

    // AI / Tracking defaults
    m_settings.aiMode = 0;            // AiWorkModeNone
    m_settings.aiSubMode = 0;         // AiSubModeNormal
    m_settings.autoZoom = false;
    m_settings.trackSpeed = 2;        // AiTrackSpeedStandard

    // Image controls - use auto mode by default
    m_settings.brightnessAuto = true;
    m_settings.brightness = 128;
    m_settings.contrastAuto = true;
    m_settings.contrast = 128;
    m_settings.saturationAuto = true;
    m_settings.saturation = 128;
    m_settings.whiteBalance = 0;      // Auto
    m_settings.whiteBalanceKelvin = 5000;

    // Audio defaults
    m_settings.audioAutoGain = true;

    // Video / preview
    m_settings.previewFormat = "auto";

    for (auto &preset : m_settings.presets) {
        preset.defined = false;
        preset.pan = 0.0;
        preset.tilt = 0.0;
        preset.zoom = 1.0;
    }

    // Application settings
    m_settings.startMinimized = false;
    m_settings.virtualCameraEnabled = false;
    m_settings.virtualCameraDevice = "/dev/video42";
    m_settings.virtualCameraResolution = "match";
}

std::string Config::getXdgConfigHome() const
{
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg);
    }

    const char *home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.config";
    }

    return ".config";  // Fallback
}

std::string Config::getConfigPath() const
{
    return getXdgConfigHome() + "/obsbot-control/settings.conf";
}

bool Config::configExists() const
{
    std::ifstream file(getConfigPath());
    return file.good();
}

bool Config::load(std::vector<ValidationError> &errors)
{
    errors.clear();
    std::string configPath = getConfigPath();

    std::ifstream file(configPath);
    if (!file.is_open()) {
        // No config file is not an error - we'll use defaults
        return true;
    }

    // Temporary storage for parsed values
    std::map<std::string, std::string> values;
    std::vector<std::string> foundKeys;

    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        lineNumber++;

        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;  // Empty line
        line = line.substr(start);

        // Skip comments
        if (line[0] == '#') continue;

        // Parse key=value
        size_t equals = line.find('=');
        if (equals == std::string::npos) {
            ValidationError err;
            err.type = MalformedLine;
            err.message = "Expected format: key=value";
            err.lineNumber = lineNumber;
            errors.push_back(err);
            continue;
        }

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);

        // Trim key and value
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);

        // Remove inline comments from value
        size_t comment = value.find('#');
        if (comment != std::string::npos) {
            value = value.substr(0, comment);
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
        }

        values[key] = value;
        foundKeys.push_back(key);

        if (!parseLine(key + "=" + value, lineNumber, errors)) {
            // Error already added to errors vector
        }
    }

    file.close();

    // Check for missing required properties
    std::vector<std::string> requiredKeys = {
        "face_tracking", "hdr", "fov", "face_ae",
        "face_focus", "zoom", "pan", "tilt",
        "brightness_auto", "brightness",
        "contrast_auto", "contrast",
        "saturation_auto", "saturation",
        "white_balance", "start_minimized"
    };

    for (const auto &key : requiredKeys) {
        if (values.find(key) == values.end()) {
            ValidationError err;
            err.type = MissingProperty;
            err.message = "Required property '" + key + "' not found";
            err.lineNumber = 0;
            errors.push_back(err);
        }
    }

    const std::unordered_set<std::string> optionalKeys = {
        "ai_mode",
        "ai_sub_mode",
        "auto_zoom",
        "track_speed",
        "audio_auto_gain",
        "preview_format",
        "virtual_camera_enabled",
        "virtual_camera_device",
        "virtual_camera_resolution",
        "white_balance_kelvin"
    };

    auto isPresetKey = [](const std::string &key) -> bool {
        if (key.rfind("preset", 0) != 0 || key.size() < 10) {
            return false;
        }

        char presetIndex = key[6];
        if (presetIndex < '1' || presetIndex > '3') {
            return false;
        }

        if (key[7] != '_') {
            return false;
        }

        const std::string suffix = key.substr(8);
        static const std::unordered_set<std::string> presetSuffixes = {
            "defined",
            "pan",
            "tilt",
            "zoom"
        };
        return presetSuffixes.count(suffix) > 0;
    };

    // Check for unknown properties
    std::unordered_set<std::string> knownKeys(requiredKeys.begin(), requiredKeys.end());
    knownKeys.insert(optionalKeys.begin(), optionalKeys.end());

    for (const auto &key : foundKeys) {
        if (knownKeys.count(key) > 0 || isPresetKey(key)) {
            continue;
        }

        ValidationError err;
        err.type = UnknownProperty;
        err.message = "Unknown property '" + key + "'";
        err.lineNumber = 0;
        errors.push_back(err);
    }

    return errors.empty();
}

bool Config::parseLine(const std::string &line, int lineNumber, std::vector<ValidationError> &errors)
{
    size_t equals = line.find('=');
    if (equals == std::string::npos) return false;

    std::string key = line.substr(0, equals);
    std::string value = line.substr(equals + 1);

    auto addError = [&](ValidationResult type, const std::string &msg) {
        ValidationError err;
        err.type = type;
        err.message = msg;
        err.lineNumber = lineNumber;
        errors.push_back(err);
    };

    // Parse boolean values
    auto parseBool = [&](const std::string &val, bool &out) -> bool {
        if (val == "true" || val == "enabled" || val == "yes" || val == "1") {
            out = true;
            return true;
        } else if (val == "false" || val == "disabled" || val == "no" || val == "0") {
            out = false;
            return true;
        }
        return false;
    };

    for (int i = 0; i < 3; ++i) {
        std::string base = "preset" + std::to_string(i + 1) + "_";
        if (key.rfind(base, 0) == 0) {
            auto &preset = m_settings.presets[static_cast<size_t>(i)];
            std::string suffix = key.substr(base.size());

            if (suffix == "defined") {
                if (!parseBool(value, preset.defined)) {
                    addError(InvalidValue, base + "defined must be true/false or enabled/disabled");
                    return false;
                }
                return true;
            } else if (suffix == "pan") {
                try {
                    double pan = std::stod(value);
                    if (pan < -1.0 || pan > 1.0) {
                        addError(InvalidValue, base + "pan must be between -1.0 and 1.0");
                        return false;
                    }
                    preset.pan = pan;
                } catch (...) {
                    addError(InvalidValue, base + "pan must be a number between -1.0 and 1.0");
                    return false;
                }
                return true;
            } else if (suffix == "tilt") {
                try {
                    double tilt = std::stod(value);
                    if (tilt < -1.0 || tilt > 1.0) {
                        addError(InvalidValue, base + "tilt must be between -1.0 and 1.0");
                        return false;
                    }
                    preset.tilt = tilt;
                } catch (...) {
                    addError(InvalidValue, base + "tilt must be a number between -1.0 and 1.0");
                    return false;
                }
                return true;
            } else if (suffix == "zoom") {
                try {
                    double zoom = std::stod(value);
                    if (zoom < 1.0 || zoom > 2.0) {
                        addError(InvalidValue, base + "zoom must be between 1.0 and 2.0");
                        return false;
                    }
                    preset.zoom = zoom;
                } catch (...) {
                    addError(InvalidValue, base + "zoom must be a number between 1.0 and 2.0");
                    return false;
                }
                return true;
            }
        }
    }

    if (key == "face_tracking") {
        if (!parseBool(value, m_settings.faceTracking)) {
            addError(InvalidValue, "face_tracking must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "hdr") {
        if (!parseBool(value, m_settings.hdr)) {
            addError(InvalidValue, "hdr must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "face_ae") {
        if (!parseBool(value, m_settings.faceAE)) {
            addError(InvalidValue, "face_ae must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "face_focus") {
        if (!parseBool(value, m_settings.faceFocus)) {
            addError(InvalidValue, "face_focus must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "fov") {
        if (value == "wide" || value == "0") {
            m_settings.fov = 0;
        } else if (value == "medium" || value == "1") {
            m_settings.fov = 1;
        } else if (value == "narrow" || value == "2") {
            m_settings.fov = 2;
        } else {
            addError(InvalidValue, "fov must be wide/medium/narrow or 0/1/2");
            return false;
        }
    } else if (key == "zoom") {
        try {
            double zoom = std::stod(value);
            if (zoom < 1.0 || zoom > 2.0) {
                addError(InvalidValue, "zoom must be between 1.0 and 2.0");
                return false;
            }
            m_settings.zoom = zoom;
        } catch (...) {
            addError(InvalidValue, "zoom must be a number between 1.0 and 2.0");
            return false;
        }
    } else if (key == "pan") {
        try {
            double pan = std::stod(value);
            if (pan < -1.0 || pan > 1.0) {
                addError(InvalidValue, "pan must be between -1.0 and 1.0");
                return false;
            }
            m_settings.pan = pan;
        } catch (...) {
            addError(InvalidValue, "pan must be a number between -1.0 and 1.0");
            return false;
        }
    } else if (key == "tilt") {
        try {
            double tilt = std::stod(value);
            if (tilt < -1.0 || tilt > 1.0) {
                addError(InvalidValue, "tilt must be between -1.0 and 1.0");
                return false;
            }
            m_settings.tilt = tilt;
        } catch (...) {
            addError(InvalidValue, "tilt must be a number between -1.0 and 1.0");
            return false;
        }
    } else if (key == "ai_mode") {
        try {
            int mode = std::stoi(value);
            if (mode < 0 || mode > 6) {
                addError(InvalidValue, "ai_mode must be between 0 and 6");
                return false;
            }
            m_settings.aiMode = mode;
        } catch (...) {
            addError(InvalidValue, "ai_mode must be an integer between 0 and 6");
            return false;
        }
    } else if (key == "ai_sub_mode") {
        try {
            int subMode = std::stoi(value);
            if (subMode < 0 || subMode > 5) {
                addError(InvalidValue, "ai_sub_mode must be between 0 and 5");
                return false;
            }
            m_settings.aiSubMode = subMode;
        } catch (...) {
            addError(InvalidValue, "ai_sub_mode must be an integer between 0 and 5");
            return false;
        }
    } else if (key == "auto_zoom") {
        if (!parseBool(value, m_settings.autoZoom)) {
            addError(InvalidValue, "auto_zoom must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "track_speed") {
        try {
            int trackSpeed = std::stoi(value);
            if (trackSpeed < 0 || trackSpeed > 5) {
                addError(InvalidValue, "track_speed must be between 0 and 5");
                return false;
            }
            m_settings.trackSpeed = trackSpeed;
        } catch (...) {
            addError(InvalidValue, "track_speed must be an integer between 0 and 5");
            return false;
        }
    } else if (key == "brightness_auto") {
        if (!parseBool(value, m_settings.brightnessAuto)) {
            addError(InvalidValue, "brightness_auto must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "brightness") {
        try {
            int brightness = std::stoi(value);
            if (brightness < 0 || brightness > 255) {
                addError(InvalidValue, "brightness must be between 0 and 255");
                return false;
            }
            m_settings.brightness = brightness;
        } catch (...) {
            addError(InvalidValue, "brightness must be an integer between 0 and 255");
            return false;
        }
    } else if (key == "contrast_auto") {
        if (!parseBool(value, m_settings.contrastAuto)) {
            addError(InvalidValue, "contrast_auto must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "contrast") {
        try {
            int contrast = std::stoi(value);
            if (contrast < 0 || contrast > 255) {
                addError(InvalidValue, "contrast must be between 0 and 255");
                return false;
            }
            m_settings.contrast = contrast;
        } catch (...) {
            addError(InvalidValue, "contrast must be an integer between 0 and 255");
            return false;
        }
    } else if (key == "saturation_auto") {
        if (!parseBool(value, m_settings.saturationAuto)) {
            addError(InvalidValue, "saturation_auto must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "saturation") {
        try {
            int saturation = std::stoi(value);
            if (saturation < 0 || saturation > 255) {
                addError(InvalidValue, "saturation must be between 0 and 255");
                return false;
            }
            m_settings.saturation = saturation;
        } catch (...) {
            addError(InvalidValue, "saturation must be an integer between 0 and 255");
            return false;
        }
    } else if (key == "white_balance") {
        if (value == "auto" || value == "0") {
            m_settings.whiteBalance = 0;
        } else if (value == "daylight" || value == "1") {
            m_settings.whiteBalance = 1;
        } else if (value == "fluorescent" || value == "2") {
            m_settings.whiteBalance = 2;
        } else if (value == "tungsten" || value == "3") {
            m_settings.whiteBalance = 3;
        } else if (value == "flash" || value == "4") {
            m_settings.whiteBalance = 4;
        } else if (value == "fine" || value == "9") {
            m_settings.whiteBalance = 9;
        } else if (value == "cloudy" || value == "10") {
            m_settings.whiteBalance = 10;
        } else if (value == "shade" || value == "11") {
            m_settings.whiteBalance = 11;
        } else if (value == "manual" || value == "255") {
            m_settings.whiteBalance = 255;
        } else {
            addError(InvalidValue, "white_balance must be auto/daylight/fluorescent/tungsten/flash/fine/cloudy/shade/manual or numeric");
            return false;
        }
    } else if (key == "white_balance_kelvin") {
        try {
            int kelvin = std::stoi(value);
            if (kelvin < 2000 || kelvin > 10000) {
                addError(InvalidValue, "white_balance_kelvin must be between 2000 and 10000");
                return false;
            }
            m_settings.whiteBalanceKelvin = kelvin;
        } catch (...) {
            addError(InvalidValue, "white_balance_kelvin must be an integer between 2000 and 10000");
            return false;
        }
    } else if (key == "audio_auto_gain") {
        if (!parseBool(value, m_settings.audioAutoGain)) {
            addError(InvalidValue, "audio_auto_gain must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "preview_format") {
        m_settings.previewFormat = value;
    } else if (key == "start_minimized") {
        if (!parseBool(value, m_settings.startMinimized)) {
            addError(InvalidValue, "start_minimized must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "virtual_camera_enabled") {
        if (!parseBool(value, m_settings.virtualCameraEnabled)) {
            addError(InvalidValue, "virtual_camera_enabled must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "virtual_camera_device") {
        if (value.empty()) {
            addError(InvalidValue, "virtual_camera_device cannot be empty");
            return false;
        }
        m_settings.virtualCameraDevice = value;
    } else if (key == "virtual_camera_resolution") {
        if (value.empty()) {
            m_settings.virtualCameraResolution = "match";
            return true;
        }

        std::string normalized = value;
        std::replace(normalized.begin(), normalized.end(), 'X', 'x');

        if (normalized == "match") {
            m_settings.virtualCameraResolution = normalized;
            return true;
        }

        size_t sep = normalized.find('x');
        if (sep == std::string::npos) {
            addError(InvalidValue, "virtual_camera_resolution must be 'match' or WIDTHxHEIGHT (e.g. 1280x720)");
            return false;
        }

        try {
            const int width = std::stoi(normalized.substr(0, sep));
            const int height = std::stoi(normalized.substr(sep + 1));
            if (width <= 0 || height <= 0) {
                addError(InvalidValue, "virtual_camera_resolution width and height must be greater than zero");
                return false;
            }
            m_settings.virtualCameraResolution = std::to_string(width) + "x" + std::to_string(height);
        } catch (...) {
            addError(InvalidValue, "virtual_camera_resolution must be 'match' or WIDTHxHEIGHT (e.g. 1280x720)");
            return false;
        }
    }

    return true;
}

bool Config::validateSettings(std::vector<ValidationError> &errors)
{
    errors.clear();

    auto addError = [&](const std::string &msg) {
        ValidationError err;
        err.type = InvalidValue;
        err.message = msg;
        err.lineNumber = 0;
        errors.push_back(err);
    };

    if (m_settings.fov < 0 || m_settings.fov > 2) {
        addError("fov out of range (must be 0-2)");
    }

    if (m_settings.zoom < 1.0 || m_settings.zoom > 2.0) {
        addError("zoom out of range (must be 1.0-2.0)");
    }

    if (m_settings.pan < -1.0 || m_settings.pan > 1.0) {
        addError("pan out of range (must be -1.0 to 1.0)");
    }

    if (m_settings.tilt < -1.0 || m_settings.tilt > 1.0) {
        addError("tilt out of range (must be -1.0 to 1.0)");
    }

    if (m_settings.aiMode < 0 || m_settings.aiMode > 6) {
        addError("ai_mode out of range (must be 0-6)");
    }

    if (m_settings.aiSubMode < 0 || m_settings.aiSubMode > 5) {
        addError("ai_sub_mode out of range (must be 0-5)");
    }

    if (m_settings.trackSpeed < 0 || m_settings.trackSpeed > 5) {
        addError("track_speed out of range (must be 0-5)");
    }

    if (m_settings.whiteBalance == 255) {
        if (m_settings.whiteBalanceKelvin < 2000 || m_settings.whiteBalanceKelvin > 10000) {
            addError("white_balance_kelvin out of range (must be 2000-10000)");
        }
    }

    for (size_t i = 0; i < m_settings.presets.size(); ++i) {
        const auto &preset = m_settings.presets[i];
        if (!preset.defined) {
            continue;
        }
        if (preset.pan < -1.0 || preset.pan > 1.0) {
            addError("preset" + std::to_string(i + 1) + "_pan out of range (must be -1.0 to 1.0)");
        }
        if (preset.tilt < -1.0 || preset.tilt > 1.0) {
            addError("preset" + std::to_string(i + 1) + "_tilt out of range (must be -1.0 to 1.0)");
        }
        if (preset.zoom < 1.0 || preset.zoom > 2.0) {
            addError("preset" + std::to_string(i + 1) + "_zoom out of range (must be 1.0 to 2.0)");
        }
    }

    if (m_settings.virtualCameraDevice.empty()) {
        addError("virtual_camera_device cannot be empty");
    }

    if (m_settings.virtualCameraResolution.empty()) {
        addError("virtual_camera_resolution cannot be empty");
    } else if (m_settings.virtualCameraResolution != "match") {
        const std::string &res = m_settings.virtualCameraResolution;
        size_t sep = res.find('x');
        if (sep == std::string::npos) {
            addError("virtual_camera_resolution must be 'match' or WIDTHxHEIGHT (e.g. 1280x720)");
        } else {
            try {
                const int width = std::stoi(res.substr(0, sep));
                const int height = std::stoi(res.substr(sep + 1));
                if (width <= 0 || height <= 0) {
                    addError("virtual_camera_resolution width and height must be greater than zero");
                }
            } catch (...) {
                addError("virtual_camera_resolution must be 'match' or WIDTHxHEIGHT (e.g. 1280x720)");
            }
        }
    }

    return errors.empty();
}

bool Config::save()
{
    std::cout << "[Config] save() called, savingEnabled=" << m_savingEnabled << ", startMinimized=" << m_settings.startMinimized << std::endl;

    if (!m_savingEnabled) {
        std::cout << "[Config] save() aborted - saving disabled" << std::endl;
        return false;
    }

    std::string configPath = getConfigPath();
    std::string configDir = configPath.substr(0, configPath.find_last_of('/'));

    // Create config directory if it doesn't exist
    struct stat st;
    if (stat(configDir.c_str(), &st) != 0) {
        // Directory doesn't exist, create it
        if (mkdir(configDir.c_str(), 0755) != 0) {
            std::cerr << "Failed to create config directory: " << configDir << std::endl;
            return false;
        }
    }

    std::ofstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file for writing: " << configPath << std::endl;
        return false;
    }

    file << "# OBSBOT Control Configuration\n";
    file << "# Auto-generated settings file\n";
    file << "#\n";
    file << "# Boolean values: true/false or enabled/disabled\n";
    file << "# FOV values: wide/medium/narrow or 0/1/2\n";
    file << "# Numeric ranges: zoom (1.0-2.0), pan/tilt (-1.0 to 1.0)\n";
    file << "\n";

    file << "# Enable automatic face tracking\n";
    file << "face_tracking=" << (m_settings.faceTracking ? "enabled" : "disabled") << "\n\n";

    file << "# High Dynamic Range\n";
    file << "hdr=" << (m_settings.hdr ? "enabled" : "disabled") << "\n\n";

    file << "# Field of View (wide/medium/narrow)\n";
    std::string fovStr = m_settings.fov == 0 ? "wide" : (m_settings.fov == 1 ? "medium" : "narrow");
    file << "fov=" << fovStr << "\n\n";

    file << "# Face-based Auto Exposure\n";
    file << "face_ae=" << (m_settings.faceAE ? "enabled" : "disabled") << "\n\n";

    file << "# Face-based Auto Focus\n";
    file << "face_focus=" << (m_settings.faceFocus ? "enabled" : "disabled") << "\n\n";

    file << "# Zoom level (1.0 to 2.0)\n";
    file << "zoom=" << m_settings.zoom << "\n\n";

    file << "# Pan position (-1.0 to 1.0, 0 is center)\n";
    file << "pan=" << m_settings.pan << "\n\n";

    file << "# Tilt position (-1.0 to 1.0, 0 is center)\n";
    file << "tilt=" << m_settings.tilt << "\n\n";

    file << "# AI Tracking Mode (0=None,1=Group,2=Human,3=Hand,4=Whiteboard,5=Desk)\n";
    file << "ai_mode=" << m_settings.aiMode << "\n\n";

    file << "# AI Human Sub-Mode (0=Normal,1=UpperBody,2=CloseUp,3=Headless,4=LowerBody)\n";
    file << "ai_sub_mode=" << m_settings.aiSubMode << "\n\n";

    file << "# Enable AI Auto Zoom\n";
    file << "auto_zoom=" << (m_settings.autoZoom ? "enabled" : "disabled") << "\n\n";

    file << "# Tracking Speed (0=Lazy,1=Slow,2=Standard,3=Fast,4=Crazy,5=Auto)\n";
    file << "track_speed=" << m_settings.trackSpeed << "\n\n";

    // Image controls
    file << "# Brightness Auto Mode (when enabled, brightness slider is read-only)\n";
    file << "brightness_auto=" << (m_settings.brightnessAuto ? "enabled" : "disabled") << "\n";
    file << "# Brightness (0-255, default 128)\n";
    file << "brightness=" << m_settings.brightness << "\n\n";

    file << "# Contrast Auto Mode (when enabled, contrast slider is read-only)\n";
    file << "contrast_auto=" << (m_settings.contrastAuto ? "enabled" : "disabled") << "\n";
    file << "# Contrast (0-255, default 128)\n";
    file << "contrast=" << m_settings.contrast << "\n\n";

    file << "# Saturation Auto Mode (when enabled, saturation slider is read-only)\n";
    file << "saturation_auto=" << (m_settings.saturationAuto ? "enabled" : "disabled") << "\n";
    file << "# Saturation (0-255, default 128)\n";
    file << "saturation=" << m_settings.saturation << "\n\n";

    file << "# White Balance (auto/daylight/fluorescent/tungsten/flash/fine/cloudy/shade)\n";
    std::string wbStr;
    switch (m_settings.whiteBalance) {
        case 0: wbStr = "auto"; break;
        case 1: wbStr = "daylight"; break;
        case 2: wbStr = "fluorescent"; break;
        case 3: wbStr = "tungsten"; break;
        case 4: wbStr = "flash"; break;
        case 9: wbStr = "fine"; break;
        case 10: wbStr = "cloudy"; break;
        case 11: wbStr = "shade"; break;
        case 255: wbStr = "manual"; break;
        default: wbStr = "auto";
    }
    file << "white_balance=" << wbStr << "\n";
    file << "# Manual white balance temperature (Kelvin, only used when white_balance=manual)\n";
    file << "white_balance_kelvin=" << m_settings.whiteBalanceKelvin << "\n\n";

    for (size_t i = 0; i < m_settings.presets.size(); ++i) {
        const auto &preset = m_settings.presets[i];
        file << "# PTZ Preset " << (i + 1) << "\n";
        file << "preset" << (i + 1) << "_defined=" << (preset.defined ? "enabled" : "disabled") << "\n";
        file << "preset" << (i + 1) << "_pan=" << preset.pan << "\n";
        file << "preset" << (i + 1) << "_tilt=" << preset.tilt << "\n";
        file << "preset" << (i + 1) << "_zoom=" << preset.zoom << "\n\n";
    }

    file << "# Audio auto gain control\n";
    file << "audio_auto_gain=" << (m_settings.audioAutoGain ? "enabled" : "disabled") << "\n\n";

    file << "# Preferred preview format (auto or WIDTHxHEIGHT@FPS)\n";
    file << "preview_format=" << (m_settings.previewFormat.empty() ? "auto" : m_settings.previewFormat) << "\n\n";

    file << "# Application Settings\n";
    file << "# Start application minimized to system tray\n";
    file << "start_minimized=" << (m_settings.startMinimized ? "enabled" : "disabled") << "\n";

    file << "\n# Virtual camera output\n";
    file << "virtual_camera_enabled=" << (m_settings.virtualCameraEnabled ? "enabled" : "disabled") << "\n";
    file << "virtual_camera_device=" << (m_settings.virtualCameraDevice.empty() ? "/dev/video42" : m_settings.virtualCameraDevice) << "\n";
    file << "# Set 'match' to follow the preview output, or WIDTHxHEIGHT (e.g. 1280x720)\n";
    file << "virtual_camera_resolution=" << (m_settings.virtualCameraResolution.empty() ? "match" : m_settings.virtualCameraResolution) << "\n";

    file.close();
    std::cout << "[Config] Configuration saved successfully to " << configPath << std::endl;
    return true;
}

bool Config::resetToDefaults(bool saveToFile)
{
    setDefaults();
    if (saveToFile) {
        return save();
    }
    return true;
}
