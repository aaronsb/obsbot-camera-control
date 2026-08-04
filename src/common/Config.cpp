#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace {

bool writeConfigAtomically(const std::string &configPath,
                           const std::string &configDir,
                           const std::string &contents)
{
    std::string temporaryTemplate = configPath + ".tmp.XXXXXX";
    std::vector<char> temporaryPath(
        temporaryTemplate.begin(), temporaryTemplate.end());
    temporaryPath.push_back('\0');

    const int descriptor = ::mkstemp(temporaryPath.data());
    if (descriptor < 0) {
        std::cerr << "Failed to create temporary config file for "
                  << configPath << ": " << std::strerror(errno)
                  << std::endl;
        return false;
    }

    const std::string temporaryName(temporaryPath.data());
    const auto discardTemporary = [&]() {
        ::unlink(temporaryName.c_str());
    };

    std::size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t written = ::write(
            descriptor, contents.data() + offset,
            contents.size() - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            const int writeError = errno;
            ::close(descriptor);
            discardTemporary();
            std::cerr << "Failed to write temporary config file for "
                      << configPath << ": "
                      << std::strerror(writeError) << std::endl;
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }

    if (::fsync(descriptor) != 0) {
        const int syncError = errno;
        ::close(descriptor);
        discardTemporary();
        std::cerr << "Failed to sync temporary config file for "
                  << configPath << ": " << std::strerror(syncError)
                  << std::endl;
        return false;
    }
    if (::close(descriptor) != 0) {
        const int closeError = errno;
        discardTemporary();
        std::cerr << "Failed to close temporary config file for "
                  << configPath << ": " << std::strerror(closeError)
                  << std::endl;
        return false;
    }

    if (::rename(temporaryName.c_str(), configPath.c_str()) != 0) {
        const int renameError = errno;
        discardTemporary();
        std::cerr << "Failed to atomically replace config file "
                  << configPath << ": " << std::strerror(renameError)
                  << std::endl;
        return false;
    }

    // The rename is already atomic. Sync the containing directory as a
    // best-effort durability step without reporting a false rollback after
    // the new file has become visible.
    const int directoryDescriptor =
        ::open(configDir.c_str(), O_RDONLY | O_DIRECTORY);
    if (directoryDescriptor >= 0) {
        if (::fsync(directoryDescriptor) != 0) {
            std::cerr << "Warning: failed to sync config directory "
                      << configDir << ": " << std::strerror(errno)
                      << std::endl;
        }
        ::close(directoryDescriptor);
    }
    return true;
}

bool isTrackingProfileMetadataKey(const std::string &key)
{
    static const std::unordered_set<std::string> activeKeys = {
        "tiny2_active_focus_policy",
        "tiny2_active_manual_focus",
        "tiny2_active_auto_zoom",
        "tiny2_active_track_speed"
        , "camera_pan_tilt_intent_defined"
        , "camera_zoom_intent_defined"
        , "camera_image_intent_defined"
    };
    if (activeKeys.count(key) > 0) {
        return true;
    }

    for (std::size_t i = 0; i < 5; ++i) {
        const std::string base =
            "tiny2_" + std::string(tiny2TrackingModeName(i)) + "_";
        if (key == base + "focus_policy"
            || key == base + "manual_focus"
            || key == base + "auto_zoom"
            || key == base + "track_speed") {
            return true;
        }
    }

    if (key.rfind("preset", 0) == 0 && key.size() > 8
        && key[6] >= '1' && key[6] <= '3' && key[7] == '_') {
        const std::string suffix = key.substr(8);
        return suffix == "focus_policy"
            || suffix == "manual_focus"
            || suffix == "track_speed";
    }
    return false;
}

} // namespace

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
    m_settings.panTiltIntentDefined = true;
    m_settings.zoomIntentDefined = true;
    m_settings.imageIntentDefined = true;

    // AI / Tracking defaults
    m_settings.aiMode = 0;            // AiWorkModeNone
    m_settings.aiSubMode = 0;         // AiSubModeNormal
    m_settings.autoZoom = false;
    m_settings.trackSpeed = 2;        // AiTrackSpeedStandard
    m_settings.activeTrackingProfile = {
        TrackingFocusPolicy::Manual, 50, false, 2};
    m_settings.trackingModeProfiles = defaultTiny2TrackingModeProfiles();

    // Image controls - use auto mode by default
    m_settings.brightnessAuto = true;
    m_settings.brightness = 128;
    m_settings.contrastAuto = true;
    m_settings.contrast = 128;
    m_settings.saturationAuto = true;
    m_settings.saturation = 128;
    m_settings.whiteBalance = 0;      // Auto
    m_settings.whiteBalanceKelvin = 5000;
    m_settings.focus = -1;            // Auto focus

    // Audio defaults
    m_settings.audioAutoGain = true;

    // Video / preview
    m_settings.previewFormat = "auto";
    m_settings.paperCropMode = 0;
    m_settings.paperCropLeft = 0.0;
    m_settings.paperCropTop = 0.0;
    m_settings.paperCropRight = 0.0;
    m_settings.paperCropBottom = 0.0;

    for (auto &preset : m_settings.presets) {
        preset.defined = false;
        preset.pan = 0.0;
        preset.tilt = 0.0;
        preset.zoom = 1.0;
        preset.sceneDefined = false;
        preset.trackingEnabled = false;
        preset.aiMode = 0;
        preset.aiSubMode = 0;
        preset.autoZoom = false;
        preset.focusPolicy = TrackingFocusPolicy::Manual;
        preset.manualFocusPosition = 50;
        preset.trackSpeed = 2;
        preset.paperCropMode = 0;
        preset.paperCropLeft = 0.0;
        preset.paperCropTop = 0.0;
        preset.paperCropRight = 0.0;
        preset.paperCropBottom = 0.0;
    }

    // Application settings
    m_settings.startMinimized = false;
    m_settings.virtualCameraEnabled = false;
    m_settings.virtualCameraDevice = "/dev/video42";
    m_settings.virtualCameraResolution = "match";
    m_settings.snapshotDirectory = "";  // Empty = XDG Pictures default
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

    // Parse transactionally. A malformed reload must not replace the last
    // valid in-memory intent with a partially parsed configuration.
    const CameraSettings previousSettings = m_settings;
    setDefaults();

    std::map<std::string, std::string> values;
    std::vector<std::string> foundKeys;

    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        lineNumber++;

        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        // New Tiny 2 profile fields are written as #@ metadata. Older
        // binaries skip these lines as comments, preserving package rollback
        // compatibility while the current loader validates them strictly.
        const bool profileMetadata = line.rfind("#@", 0) == 0;
        if (profileMetadata) {
            line = line.substr(2);
        } else if (line[0] == '#') {
            continue;
        }

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

        const size_t keyEnd = key.find_last_not_of(" \t\r\n");
        if (keyEnd == std::string::npos) {
            key.clear();
        } else {
            key.erase(keyEnd + 1);
        }
        const size_t valueStart = value.find_first_not_of(" \t\r\n");
        value = valueStart == std::string::npos
            ? std::string() : value.substr(valueStart);
        const size_t valueEnd = value.find_last_not_of(" \t\r\n");
        if (valueEnd != std::string::npos) {
            value.erase(valueEnd + 1);
        }

        size_t comment = value.find('#');
        if (comment != std::string::npos) {
            value = value.substr(0, comment);
            const size_t trimmedEnd = value.find_last_not_of(" \t\r\n");
            value = trimmedEnd == std::string::npos
                ? std::string() : value.substr(0, trimmedEnd + 1);
        }

        if (profileMetadata && !isTrackingProfileMetadataKey(key)) {
            ValidationError err;
            err.type = UnknownProperty;
            err.message = "Unknown tracking profile metadata '" + key + "'";
            err.lineNumber = lineNumber;
            errors.push_back(err);
            continue;
        }

        values[key] = value;
        if (!profileMetadata) {
            foundKeys.push_back(key);
        }
        parseLine(key + "=" + value, lineNumber, errors);
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
        "paper_crop_mode",
        "paper_crop_left",
        "paper_crop_top",
        "paper_crop_right",
        "paper_crop_bottom",
        "virtual_camera_enabled",
        "virtual_camera_device",
        "virtual_camera_resolution",
        "white_balance_kelvin",
        "focus",
        "snapshot_directory"
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
            "zoom",
            "scene_defined",
            "tracking_enabled",
            "ai_mode",
            "ai_sub_mode",
            "auto_zoom",
            "paper_crop_mode",
            "paper_crop_left",
            "paper_crop_top",
            "paper_crop_right",
            "paper_crop_bottom"
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

    if (!errors.empty()) {
        m_settings = previousSettings;
        return false;
    }

    migrateTrackingProfiles(values);
    if (!validateSettings(errors)) {
        m_settings = previousSettings;
        return false;
    }
    return true;
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

    auto parseFiniteDouble = [](const std::string &val, double &out) -> bool {
        try {
            size_t consumed = 0;
            out = std::stod(val, &consumed);
            return consumed == val.size() && std::isfinite(out);
        } catch (...) {
            return false;
        }
    };

    auto parseInteger = [](const std::string &val, int &out) -> bool {
        try {
            size_t consumed = 0;
            out = std::stoi(val, &consumed);
            return consumed == val.size();
        } catch (...) {
            return false;
        }
    };

    auto parseCropMode = [&](const std::string &val, int &out) -> bool {
        if (val == "off") out = 0;
        else if (val == "manual") out = 1;
        else if (val == "automatic" || val == "auto") out = 2;
        else if (!parseInteger(val, out)) return false;
        return out >= 0 && out <= 2;
    };

    auto parseCropMargin = [&](const std::string &val, double &out) -> bool {
        return parseFiniteDouble(val, out) && out >= 0.0 && out <= 0.45;
    };

    auto parseProfileField = [&](const std::string &base,
                                 TrackingModeProfile &profile) -> int {
        if (key == base + "focus_policy") {
            if (!parseTrackingFocusPolicy(value, profile.focusPolicy)) {
                addError(InvalidValue,
                         base + "focus_policy must be face, continuous, or manual");
                return -1;
            }
            return 1;
        }
        if (key == base + "manual_focus") {
            int focus = 0;
            if (!parseInteger(value, focus) || focus < 0 || focus > 100) {
                addError(InvalidValue,
                         base + "manual_focus must be an integer between 0 and 100");
                return -1;
            }
            profile.manualFocusPosition = focus;
            return 1;
        }
        if (key == base + "auto_zoom") {
            if (!parseBool(value, profile.autoZoom)) {
                addError(InvalidValue,
                         base + "auto_zoom must be true/false or enabled/disabled");
                return -1;
            }
            return 1;
        }
        if (key == base + "track_speed") {
            int speed = 0;
            if (!parseInteger(value, speed) || speed < 0 || speed > 5) {
                addError(InvalidValue,
                         base + "track_speed must be an integer between 0 and 5");
                return -1;
            }
            profile.trackSpeed = speed;
            return 1;
        }
        return 0;
    };

    int profileFieldResult = parseProfileField(
        "tiny2_active_", m_settings.activeTrackingProfile);
    if (profileFieldResult != 0) {
        return profileFieldResult > 0;
    }
    for (std::size_t i = 0; i < m_settings.trackingModeProfiles.size(); ++i) {
        profileFieldResult = parseProfileField(
            "tiny2_" + std::string(tiny2TrackingModeName(i)) + "_",
            m_settings.trackingModeProfiles[i]);
        if (profileFieldResult != 0) {
            return profileFieldResult > 0;
        }
    }

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
                double pan = 0.0;
                if (!parseFiniteDouble(value, pan)) {
                    addError(InvalidValue, base + "pan must be a number between -1.0 and 1.0");
                    return false;
                }
                if (pan < -1.0 || pan > 1.0) {
                    addError(InvalidValue, base + "pan must be between -1.0 and 1.0");
                    return false;
                }
                preset.pan = pan;
                return true;
            } else if (suffix == "tilt") {
                double tilt = 0.0;
                if (!parseFiniteDouble(value, tilt)) {
                    addError(InvalidValue, base + "tilt must be a number between -1.0 and 1.0");
                    return false;
                }
                if (tilt < -1.0 || tilt > 1.0) {
                    addError(InvalidValue, base + "tilt must be between -1.0 and 1.0");
                    return false;
                }
                preset.tilt = tilt;
                return true;
            } else if (suffix == "zoom") {
                double zoom = 0.0;
                if (!parseFiniteDouble(value, zoom)) {
                    addError(InvalidValue, base + "zoom must be a number between 1.0 and 2.0");
                    return false;
                }
                if (zoom < 1.0 || zoom > 2.0) {
                    addError(InvalidValue, base + "zoom must be between 1.0 and 2.0");
                    return false;
                }
                preset.zoom = zoom;
                return true;
            } else if (suffix == "scene_defined") {
                if (!parseBool(value, preset.sceneDefined)) {
                    addError(InvalidValue, base + "scene_defined must be true/false or enabled/disabled");
                    return false;
                }
                return true;
            } else if (suffix == "tracking_enabled") {
                if (!parseBool(value, preset.trackingEnabled)) {
                    addError(InvalidValue, base + "tracking_enabled must be true/false or enabled/disabled");
                    return false;
                }
                return true;
            } else if (suffix == "ai_mode") {
                if (!parseInteger(value, preset.aiMode) || preset.aiMode < 0 || preset.aiMode > 5) {
                    addError(InvalidValue, base + "ai_mode must be between 0 and 5");
                    return false;
                }
                return true;
            } else if (suffix == "ai_sub_mode") {
                if (!parseInteger(value, preset.aiSubMode) || preset.aiSubMode < 0 || preset.aiSubMode > 4) {
                    addError(InvalidValue, base + "ai_sub_mode must be between 0 and 4");
                    return false;
                }
                return true;
            } else if (suffix == "auto_zoom") {
                if (!parseBool(value, preset.autoZoom)) {
                    addError(InvalidValue, base + "auto_zoom must be true/false or enabled/disabled");
                    return false;
                }
                return true;
            } else if (suffix == "focus_policy") {
                if (!parseTrackingFocusPolicy(value, preset.focusPolicy)) {
                    addError(InvalidValue,
                             base + "focus_policy must be face, continuous, or manual");
                    return false;
                }
                return true;
            } else if (suffix == "manual_focus") {
                if (!parseInteger(value, preset.manualFocusPosition)
                    || preset.manualFocusPosition < 0
                    || preset.manualFocusPosition > 100) {
                    addError(InvalidValue,
                             base + "manual_focus must be an integer between 0 and 100");
                    return false;
                }
                return true;
            } else if (suffix == "track_speed") {
                if (!parseInteger(value, preset.trackSpeed)
                    || preset.trackSpeed < 0 || preset.trackSpeed > 5) {
                    addError(InvalidValue,
                             base + "track_speed must be an integer between 0 and 5");
                    return false;
                }
                return true;
            } else if (suffix == "paper_crop_mode") {
                if (!parseCropMode(value, preset.paperCropMode)) {
                    addError(InvalidValue, base + "paper_crop_mode must be off/manual/automatic or 0/1/2");
                    return false;
                }
                return true;
            } else if (suffix == "paper_crop_left") {
                if (!parseCropMargin(value, preset.paperCropLeft)) {
                    addError(InvalidValue, base + "paper_crop_left must be between 0.0 and 0.45");
                    return false;
                }
                return true;
            } else if (suffix == "paper_crop_top") {
                if (!parseCropMargin(value, preset.paperCropTop)) {
                    addError(InvalidValue, base + "paper_crop_top must be between 0.0 and 0.45");
                    return false;
                }
                return true;
            } else if (suffix == "paper_crop_right") {
                if (!parseCropMargin(value, preset.paperCropRight)) {
                    addError(InvalidValue, base + "paper_crop_right must be between 0.0 and 0.45");
                    return false;
                }
                return true;
            } else if (suffix == "paper_crop_bottom") {
                if (!parseCropMargin(value, preset.paperCropBottom)) {
                    addError(InvalidValue, base + "paper_crop_bottom must be between 0.0 and 0.45");
                    return false;
                }
                return true;
            }
        }
    }

    if (key == "camera_pan_tilt_intent_defined") {
        if (!parseBool(value, m_settings.panTiltIntentDefined)) {
            addError(InvalidValue,
                     "camera_pan_tilt_intent_defined must be boolean");
            return false;
        }
    } else if (key == "camera_zoom_intent_defined") {
        if (!parseBool(value, m_settings.zoomIntentDefined)) {
            addError(InvalidValue,
                     "camera_zoom_intent_defined must be boolean");
            return false;
        }
    } else if (key == "camera_image_intent_defined") {
        if (!parseBool(value, m_settings.imageIntentDefined)) {
            addError(InvalidValue,
                     "camera_image_intent_defined must be boolean");
            return false;
        }
    } else if (key == "face_tracking") {
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
        double zoom = 0.0;
        if (!parseFiniteDouble(value, zoom)
            || (zoom != 0.0 && (zoom < 1.0 || zoom > 2.0))) {
            addError(InvalidValue, "zoom must be 0 or a number between 1.0 and 2.0");
            return false;
        }
        m_settings.zoom = zoom;
    } else if (key == "pan") {
        double pan = 0.0;
        if (!parseFiniteDouble(value, pan) || pan < -1.0 || pan > 1.0) {
            addError(InvalidValue, "pan must be a number between -1.0 and 1.0");
            return false;
        }
        m_settings.pan = pan;
    } else if (key == "tilt") {
        double tilt = 0.0;
        if (!parseFiniteDouble(value, tilt) || tilt < -1.0 || tilt > 1.0) {
            addError(InvalidValue, "tilt must be a number between -1.0 and 1.0");
            return false;
        }
        m_settings.tilt = tilt;
    } else if (key == "ai_mode") {
        int mode = 0;
        if (!parseInteger(value, mode) || mode < 0 || mode > 5) {
            addError(InvalidValue, "ai_mode must be an integer between 0 and 5");
            return false;
        }
        m_settings.aiMode = mode;
    } else if (key == "ai_sub_mode") {
        int subMode = 0;
        if (!parseInteger(value, subMode) || subMode < 0 || subMode > 4) {
            addError(InvalidValue, "ai_sub_mode must be an integer between 0 and 4");
            return false;
        }
        m_settings.aiSubMode = subMode;
    } else if (key == "auto_zoom") {
        if (!parseBool(value, m_settings.autoZoom)) {
            addError(InvalidValue, "auto_zoom must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "track_speed") {
        int trackSpeed = 0;
        if (!parseInteger(value, trackSpeed)
            || trackSpeed < 0 || trackSpeed > 5) {
            addError(InvalidValue, "track_speed must be an integer between 0 and 5");
            return false;
        }
        m_settings.trackSpeed = trackSpeed;
    } else if (key == "brightness_auto") {
        if (!parseBool(value, m_settings.brightnessAuto)) {
            addError(InvalidValue, "brightness_auto must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "brightness") {
        try {
            int brightness = std::stoi(value);
            if (brightness == -1) {
                m_settings.brightness = -1;
            } else if (brightness < 0 || brightness > 255) {
                addError(InvalidValue, "brightness must be between 0 and 255");
                return false;
            } else {
                m_settings.brightness = brightness;
            }
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
            if (contrast == -1) {
                m_settings.contrast = -1;
            } else if (contrast < 0 || contrast > 255) {
                addError(InvalidValue, "contrast must be between 0 and 255");
                return false;
            } else {
                m_settings.contrast = contrast;
            }
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
            if (saturation == -1) {
                m_settings.saturation = -1;
            } else if (saturation < 0 || saturation > 255) {
                addError(InvalidValue, "saturation must be between 0 and 255");
                return false;
            } else {
                m_settings.saturation = saturation;
            }
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
            if (kelvin == -1) {
                m_settings.whiteBalanceKelvin = -1;
            } else if (kelvin < 2000 || kelvin > 10000) {
                addError(InvalidValue, "white_balance_kelvin must be between 2000 and 10000");
                return false;
            } else {
                m_settings.whiteBalanceKelvin = kelvin;
            }
        } catch (...) {
            addError(InvalidValue, "white_balance_kelvin must be an integer between 2000 and 10000");
            return false;
        }
    } else if (key == "focus") {
        try {
            int focus = std::stoi(value);
            if (focus == -1) {
                m_settings.focus = -1;
            } else if (focus < 0 || focus > 100) {
                addError(InvalidValue, "focus must be between 0 and 100");
                return false;
            } else {
                m_settings.focus = focus;
            }
        } catch (...) {
            addError(InvalidValue, "focus must be an integer between 0 and 100");
            return false;
        }
    } else if (key == "audio_auto_gain") {
        if (!parseBool(value, m_settings.audioAutoGain)) {
            addError(InvalidValue, "audio_auto_gain must be true/false or enabled/disabled");
            return false;
        }
    } else if (key == "preview_format") {
        m_settings.previewFormat = value;
    } else if (key == "paper_crop_mode") {
        if (!parseCropMode(value, m_settings.paperCropMode)) {
            addError(InvalidValue, "paper_crop_mode must be off/manual/automatic or 0/1/2");
            return false;
        }
    } else if (key == "paper_crop_left") {
        if (!parseCropMargin(value, m_settings.paperCropLeft)) {
            addError(InvalidValue, "paper_crop_left must be between 0.0 and 0.45");
            return false;
        }
    } else if (key == "paper_crop_top") {
        if (!parseCropMargin(value, m_settings.paperCropTop)) {
            addError(InvalidValue, "paper_crop_top must be between 0.0 and 0.45");
            return false;
        }
    } else if (key == "paper_crop_right") {
        if (!parseCropMargin(value, m_settings.paperCropRight)) {
            addError(InvalidValue, "paper_crop_right must be between 0.0 and 0.45");
            return false;
        }
    } else if (key == "paper_crop_bottom") {
        if (!parseCropMargin(value, m_settings.paperCropBottom)) {
            addError(InvalidValue, "paper_crop_bottom must be between 0.0 and 0.45");
            return false;
        }
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
    } else if (key == "snapshot_directory") {
        m_settings.snapshotDirectory = value;
    }

    return true;
}

void Config::migrateTrackingProfiles(
    const std::map<std::string, std::string> &parsedValues)
{
    const auto has = [&](const std::string &key) {
        return parsedValues.find(key) != parsedValues.end();
    };
    const TrackingModeProfile legacy = legacyTrackingModeProfile(
        m_settings.faceFocus, m_settings.focus,
        m_settings.autoZoom, m_settings.trackSpeed);

    if (!has("tiny2_active_focus_policy")) {
        m_settings.activeTrackingProfile.focusPolicy = legacy.focusPolicy;
    }
    if (!has("tiny2_active_manual_focus")) {
        m_settings.activeTrackingProfile.manualFocusPosition =
            legacy.manualFocusPosition;
    }
    if (!has("tiny2_active_auto_zoom")) {
        m_settings.activeTrackingProfile.autoZoom = legacy.autoZoom;
    }
    if (!has("tiny2_active_track_speed")) {
        m_settings.activeTrackingProfile.trackSpeed = legacy.trackSpeed;
    }

    const bool hasExplicitActiveProfile =
        has("tiny2_active_focus_policy")
        || has("tiny2_active_manual_focus")
        || has("tiny2_active_auto_zoom")
        || has("tiny2_active_track_speed");
    if (!m_settings.faceTracking && !hasExplicitActiveProfile) {
        m_settings.activeTrackingProfile.focusPolicy =
            TrackingFocusPolicy::Manual;
        m_settings.activeTrackingProfile.autoZoom = false;
    }

    for (std::size_t i = 0; i < m_settings.trackingModeProfiles.size(); ++i) {
        auto &profile = m_settings.trackingModeProfiles[i];
        const std::string base =
            "tiny2_" + std::string(tiny2TrackingModeName(i)) + "_";

        // Legacy face-focus and auto-zoom were Human-mode controls. Preserve
        // that intent only for Human; the approved Group/Hand/Whiteboard/Desk
        // defaults remain independent.
        if (i == 1 && !has(base + "focus_policy")) {
            profile.focusPolicy = legacy.focusPolicy;
        }
        if (!has(base + "manual_focus")) {
            profile.manualFocusPosition = legacy.manualFocusPosition;
        }
        if (i == 1 && !has(base + "auto_zoom")) {
            profile.autoZoom = legacy.autoZoom;
        }
        if (!has(base + "track_speed")) {
            profile.trackSpeed = legacy.trackSpeed;
        }
    }

    for (std::size_t i = 0; i < m_settings.presets.size(); ++i) {
        auto &preset = m_settings.presets[i];
        const std::string base =
            "preset" + std::to_string(i + 1) + "_";
        if (!has(base + "focus_policy")) {
            preset.focusPolicy = preset.trackingEnabled
                ? m_settings.activeTrackingProfile.focusPolicy
                : TrackingFocusPolicy::Manual;
        }
        if (!has(base + "manual_focus")) {
            preset.manualFocusPosition =
                m_settings.activeTrackingProfile.manualFocusPosition;
        }
        if (!has(base + "track_speed")) {
            preset.trackSpeed = m_settings.activeTrackingProfile.trackSpeed;
        }
        if (preset.sceneDefined && !preset.trackingEnabled) {
            // Older packages could persist auto zoom while a scene was
            // tracking-off even though it could never be active. Normalize
            // that legacy combination to the new safe off invariant.
            preset.autoZoom = false;
        }
    }
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

    const auto validCropMargin = [](double margin) {
        return std::isfinite(margin) && margin >= 0.0 && margin <= 0.45;
    };

    if (m_settings.fov < 0 || m_settings.fov > 2) {
        addError("fov out of range (must be 0-2)");
    }

    if (!std::isfinite(m_settings.zoom)
        || (m_settings.zoom != 0.0
            && (m_settings.zoom < 1.0 || m_settings.zoom > 2.0))) {
        addError("zoom out of range (must be 0 or 1.0-2.0)");
    }

    if (!std::isfinite(m_settings.pan)
        || m_settings.pan < -1.0 || m_settings.pan > 1.0) {
        addError("pan out of range (must be -1.0 to 1.0)");
    }

    if (!std::isfinite(m_settings.tilt)
        || m_settings.tilt < -1.0 || m_settings.tilt > 1.0) {
        addError("tilt out of range (must be -1.0 to 1.0)");
    }

    if (m_settings.aiMode < 0 || m_settings.aiMode > 5) {
        addError("ai_mode out of range (must be 0-5)");
    }

    if (m_settings.aiSubMode < 0 || m_settings.aiSubMode > 4) {
        addError("ai_sub_mode out of range (must be 0-4)");
    }

    if (m_settings.trackSpeed < 0 || m_settings.trackSpeed > 5) {
        addError("track_speed out of range (must be 0-5)");
    }

    if (m_settings.focus < -1 || m_settings.focus > 100) {
        addError("focus out of range (must be -1 or 0-100)");
    }

    if (!isValidTrackingModeProfile(m_settings.activeTrackingProfile)) {
        addError("tiny2 active tracking profile is invalid");
    }
    if (!m_settings.faceTracking
        && (m_settings.activeTrackingProfile.focusPolicy
                != TrackingFocusPolicy::Manual
            || m_settings.activeTrackingProfile.autoZoom)) {
        addError("tracking-off intent must use manual focus with auto zoom disabled");
    }
    for (std::size_t i = 0; i < m_settings.trackingModeProfiles.size(); ++i) {
        if (!isValidTrackingModeProfile(m_settings.trackingModeProfiles[i])) {
            addError("tiny2_" + std::string(tiny2TrackingModeName(i))
                     + " tracking profile is invalid");
        }
    }

    if (m_settings.paperCropMode < 0 || m_settings.paperCropMode > 2) {
        addError("paper_crop_mode out of range (must be 0-2)");
    }
    if (!validCropMargin(m_settings.paperCropLeft)) {
        addError("paper_crop_left out of range (must be 0.0-0.45)");
    }
    if (!validCropMargin(m_settings.paperCropTop)) {
        addError("paper_crop_top out of range (must be 0.0-0.45)");
    }
    if (!validCropMargin(m_settings.paperCropRight)) {
        addError("paper_crop_right out of range (must be 0.0-0.45)");
    }
    if (!validCropMargin(m_settings.paperCropBottom)) {
        addError("paper_crop_bottom out of range (must be 0.0-0.45)");
    }

    if (m_settings.whiteBalance == 255) {
        if (m_settings.whiteBalanceKelvin != -1 && (m_settings.whiteBalanceKelvin < 2000 || m_settings.whiteBalanceKelvin > 10000)) {
            addError("white_balance_kelvin out of range (must be 2000-10000)");
        }
    }

    for (size_t i = 0; i < m_settings.presets.size(); ++i) {
        const auto &preset = m_settings.presets[i];
        if (!std::isfinite(preset.pan) || preset.pan < -1.0 || preset.pan > 1.0) {
            addError("preset" + std::to_string(i + 1) + "_pan out of range (must be -1.0 to 1.0)");
        }
        if (!std::isfinite(preset.tilt) || preset.tilt < -1.0 || preset.tilt > 1.0) {
            addError("preset" + std::to_string(i + 1) + "_tilt out of range (must be -1.0 to 1.0)");
        }
        if (!std::isfinite(preset.zoom) || preset.zoom < 1.0 || preset.zoom > 2.0) {
            addError("preset" + std::to_string(i + 1) + "_zoom out of range (must be 1.0 to 2.0)");
        }
        const std::string presetName =
            "preset" + std::to_string(i + 1) + "_";
        if (preset.aiMode < 0 || preset.aiMode > 5) {
            addError(presetName + "ai_mode out of range (must be 0-5)");
        }
        if (preset.aiSubMode < 0 || preset.aiSubMode > 4) {
            addError(presetName + "ai_sub_mode out of range (must be 0-4)");
        }
        const TrackingModeProfile presetProfile{
            preset.focusPolicy,
            preset.manualFocusPosition,
            preset.autoZoom,
            preset.trackSpeed
        };
        if (!isValidTrackingModeProfile(presetProfile)) {
            addError(presetName + "tracking profile is invalid");
        }
        if (preset.sceneDefined && !preset.trackingEnabled
            && (preset.focusPolicy != TrackingFocusPolicy::Manual
                || preset.autoZoom)) {
            addError(presetName
                     + "tracking-off scene must use manual focus with auto zoom disabled");
        }
        if (preset.paperCropMode < 0 || preset.paperCropMode > 2) {
            addError(presetName + "paper_crop_mode out of range (must be 0-2)");
        }
        if (!validCropMargin(preset.paperCropLeft)) {
            addError(presetName + "paper_crop_left out of range (must be 0.0-0.45)");
        }
        if (!validCropMargin(preset.paperCropTop)) {
            addError(presetName + "paper_crop_top out of range (must be 0.0-0.45)");
        }
        if (!validCropMargin(preset.paperCropRight)) {
            addError(presetName + "paper_crop_right out of range (must be 0.0-0.45)");
        }
        if (!validCropMargin(preset.paperCropBottom)) {
            addError(presetName + "paper_crop_bottom out of range (must be 0.0-0.45)");
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

    std::vector<ValidationError> validationErrors;
    if (!validateSettings(validationErrors)) {
        for (const auto &error : validationErrors) {
            std::cerr << "[Config] refusing to save invalid settings: "
                      << error.message << std::endl;
        }
        return false;
    }

    std::string configPath = getConfigPath();
    std::string configDir = configPath.substr(0, configPath.find_last_of('/'));

    std::error_code directoryError;
    std::filesystem::create_directories(configDir, directoryError);
    if (directoryError) {
        std::cerr << "Failed to create config directory: " << configDir
                  << " (" << directoryError.message() << ")" << std::endl;
        return false;
    }

    // Serialize completely before touching the live path. The final helper
    // writes and fsyncs a same-directory temporary file, then atomically
    // renames it over the previous configuration.
    std::ostringstream file;

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
    file << "# Camera-bound intent categories (rollback-compatible metadata)\n";
    file << "#@camera_pan_tilt_intent_defined="
         << (m_settings.panTiltIntentDefined ? "enabled" : "disabled")
         << "\n";
    file << "#@camera_zoom_intent_defined="
         << (m_settings.zoomIntentDefined ? "enabled" : "disabled")
         << "\n";
    file << "#@camera_image_intent_defined="
         << (m_settings.imageIntentDefined ? "enabled" : "disabled")
         << "\n\n";

    file << "# AI Tracking Mode (0=None,1=Group,2=Human,3=Hand,4=Whiteboard,5=Desk)\n";
    file << "ai_mode=" << m_settings.aiMode << "\n\n";

    file << "# AI Human Sub-Mode (0=Normal,1=UpperBody,2=CloseUp,3=Headless,4=LowerBody)\n";
    file << "ai_sub_mode=" << m_settings.aiSubMode << "\n\n";

    file << "# Enable AI Auto Zoom\n";
    file << "auto_zoom=" << (m_settings.autoZoom ? "enabled" : "disabled") << "\n\n";

    file << "# Tracking Speed (0=Lazy,1=Slow,2=Standard,3=Fast,4=Crazy,5=Auto)\n";
    file << "track_speed=" << m_settings.trackSpeed << "\n\n";

    // Backward-compatible Tiny 2 profile metadata. The #@ prefix is parsed by
    // this version and ignored as a comment by older packages.
    const auto writeTrackingProfile = [&](const std::string &prefix,
                                          const TrackingModeProfile &profile) {
        file << "#@" << prefix << "focus_policy="
             << trackingFocusPolicyToken(profile.focusPolicy) << "\n";
        file << "#@" << prefix << "manual_focus="
             << profile.manualFocusPosition << "\n";
        file << "#@" << prefix << "auto_zoom="
             << (profile.autoZoom ? "enabled" : "disabled") << "\n";
        file << "#@" << prefix << "track_speed="
             << profile.trackSpeed << "\n";
    };
    file << "# Tiny 2 mode-specific tracking profiles\n";
    writeTrackingProfile("tiny2_active_", m_settings.activeTrackingProfile);
    for (std::size_t i = 0; i < m_settings.trackingModeProfiles.size(); ++i) {
        writeTrackingProfile(
            "tiny2_" + std::string(tiny2TrackingModeName(i)) + "_",
            m_settings.trackingModeProfiles[i]);
    }
    file << "\n";

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

    file << "# Manual focus position (0-100, -1 = auto)\n";
    file << "focus=" << m_settings.focus << "\n\n";

    for (size_t i = 0; i < m_settings.presets.size(); ++i) {
        const auto &preset = m_settings.presets[i];
        file << "# PTZ Preset " << (i + 1) << "\n";
        file << "preset" << (i + 1) << "_defined=" << (preset.defined ? "enabled" : "disabled") << "\n";
        file << "preset" << (i + 1) << "_pan=" << preset.pan << "\n";
        file << "preset" << (i + 1) << "_tilt=" << preset.tilt << "\n";
        file << "preset" << (i + 1) << "_zoom=" << preset.zoom << "\n\n";
        file << "preset" << (i + 1) << "_scene_defined=" << (preset.sceneDefined ? "enabled" : "disabled") << "\n";
        file << "preset" << (i + 1) << "_tracking_enabled=" << (preset.trackingEnabled ? "enabled" : "disabled") << "\n";
        file << "preset" << (i + 1) << "_ai_mode=" << preset.aiMode << "\n";
        file << "preset" << (i + 1) << "_ai_sub_mode=" << preset.aiSubMode << "\n";
        file << "preset" << (i + 1) << "_auto_zoom=" << (preset.autoZoom ? "enabled" : "disabled") << "\n";
        file << "#@preset" << (i + 1) << "_focus_policy="
             << trackingFocusPolicyToken(preset.focusPolicy) << "\n";
        file << "#@preset" << (i + 1) << "_manual_focus="
             << preset.manualFocusPosition << "\n";
        file << "#@preset" << (i + 1) << "_track_speed="
             << preset.trackSpeed << "\n";
        file << "preset" << (i + 1) << "_paper_crop_mode=" << preset.paperCropMode << "\n";
        file << "preset" << (i + 1) << "_paper_crop_left=" << preset.paperCropLeft << "\n";
        file << "preset" << (i + 1) << "_paper_crop_top=" << preset.paperCropTop << "\n";
        file << "preset" << (i + 1) << "_paper_crop_right=" << preset.paperCropRight << "\n";
        file << "preset" << (i + 1) << "_paper_crop_bottom=" << preset.paperCropBottom << "\n\n";
    }

    file << "# Audio auto gain control\n";
    file << "audio_auto_gain=" << (m_settings.audioAutoGain ? "enabled" : "disabled") << "\n\n";

    file << "# Preferred preview format (auto or WIDTHxHEIGHT@FPS)\n";
    file << "preview_format=" << (m_settings.previewFormat.empty() ? "auto" : m_settings.previewFormat) << "\n\n";
    file << "# Paper crop (0=Off, 1=Manual rectangle, 2=Automatic detection)\n";
    file << "paper_crop_mode=" << m_settings.paperCropMode << "\n";
    file << "paper_crop_left=" << m_settings.paperCropLeft << "\n";
    file << "paper_crop_top=" << m_settings.paperCropTop << "\n";
    file << "paper_crop_right=" << m_settings.paperCropRight << "\n";
    file << "paper_crop_bottom=" << m_settings.paperCropBottom << "\n\n";

    file << "# Application Settings\n";
    file << "# Start application minimized to system tray\n";
    file << "start_minimized=" << (m_settings.startMinimized ? "enabled" : "disabled") << "\n";

    file << "\n# Virtual camera output\n";
    file << "virtual_camera_enabled=" << (m_settings.virtualCameraEnabled ? "enabled" : "disabled") << "\n";
    file << "virtual_camera_device=" << (m_settings.virtualCameraDevice.empty() ? "/dev/video42" : m_settings.virtualCameraDevice) << "\n";
    file << "# Set 'match' to follow the preview output, or WIDTHxHEIGHT (e.g. 1280x720)\n";
    file << "virtual_camera_resolution=" << (m_settings.virtualCameraResolution.empty() ? "match" : m_settings.virtualCameraResolution) << "\n";

    file << "\n# Snapshot save directory (empty = ~/Pictures/obsbot-control/)\n";
    file << "snapshot_directory=" << m_settings.snapshotDirectory << "\n";

    if (!file.good()) {
        std::cerr << "Failed to serialize config for " << configPath
                  << std::endl;
        return false;
    }
    const std::string serialized = file.str();
    if (!writeConfigAtomically(configPath, configDir, serialized)) {
        return false;
    }
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
