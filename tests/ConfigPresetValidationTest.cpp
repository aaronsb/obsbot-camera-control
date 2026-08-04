#include "Config.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

std::string validConfigWithPreset(const std::string &pan,
                                  const std::string &tilt,
                                  const std::string &zoom)
{
    return
        "face_tracking=disabled\n"
        "hdr=disabled\n"
        "fov=wide\n"
        "face_ae=disabled\n"
        "face_focus=disabled\n"
        "zoom=1.0\n"
        "pan=0.0\n"
        "tilt=0.0\n"
        "brightness_auto=enabled\n"
        "brightness=128\n"
        "contrast_auto=enabled\n"
        "contrast=128\n"
        "saturation_auto=enabled\n"
        "saturation=128\n"
        "white_balance=auto\n"
        "start_minimized=disabled\n"
        "paper_crop_mode=automatic\n"
        "paper_crop_left=0.10\n"
        "paper_crop_top=0.05\n"
        "paper_crop_right=0.10\n"
        "paper_crop_bottom=0.05\n"
        "preset1_defined=enabled\n"
        "preset1_pan=" + pan + "\n"
        "preset1_tilt=" + tilt + "\n"
        "preset1_zoom=" + zoom + "\n"
        "preset1_scene_defined=enabled\n"
        "preset1_tracking_enabled=disabled\n"
        "preset1_ai_mode=0\n"
        "preset1_ai_sub_mode=0\n"
        "preset1_auto_zoom=disabled\n"
        "preset1_paper_crop_mode=manual\n"
        "preset1_paper_crop_left=0.15\n"
        "preset1_paper_crop_top=0.10\n"
        "preset1_paper_crop_right=0.15\n"
        "preset1_paper_crop_bottom=0.10\n";
}

std::string withSetting(std::string contents,
                        const std::string &key,
                        const std::string &value)
{
    const std::string marker = key + "=";
    const size_t markerPos = contents.find(marker);
    if (markerPos == std::string::npos) {
        return contents + marker + value + "\n";
    }
    const size_t valuePos = markerPos + marker.size();
    const size_t lineEnd = contents.find('\n', valuePos);
    contents.replace(valuePos, lineEnd - valuePos, value);
    return contents;
}

bool loadConfig(const std::filesystem::path &configPath,
                const std::string &pan,
                const std::string &tilt,
                const std::string &zoom,
                Config::CameraSettings *loadedSettings = nullptr)
{
    std::ofstream file(configPath);
    file << validConfigWithPreset(pan, tilt, zoom);
    file.close();

    Config config;
    std::vector<Config::ValidationError> errors;
    const bool loaded = config.load(errors);
    if (loaded && loadedSettings) {
        *loadedSettings = config.getSettings();
    }
    return loaded;
}

bool loadRawConfig(
    const std::filesystem::path &configPath,
    const std::string &contents,
    Config::CameraSettings *loadedSettings = nullptr)
{
    std::ofstream file(configPath);
    file << contents;
    file.close();

    Config config;
    std::vector<Config::ValidationError> errors;
    const bool loaded = config.load(errors);
    if (loaded && loadedSettings) {
        *loadedSettings = config.getSettings();
    }
    return loaded;
}

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

bool check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    const char *tmpDir = std::getenv("TMPDIR");
    if (!tmpDir || tmpDir[0] == '\0') {
        std::cerr << "FAIL: TMPDIR must be set for scratch-backed tests\n";
        return 1;
    }

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::path(tmpDir) / ("obsbot-config-test-" + std::to_string(nonce));
    const std::filesystem::path configDir = root / "obsbot-control";
    const std::filesystem::path configPath = configDir / "settings.conf";
    std::filesystem::create_directories(configDir);
    setenv("XDG_CONFIG_HOME", root.c_str(), 1);

    bool passed = true;
    Config::CameraSettings loadedSettings{};
    passed &= check(loadConfig(configPath, "0.25", "-0.5", "1.75", &loadedSettings),
                    "finite scene preset and crop values are accepted");
    passed &= check(loadedSettings.paperCropMode == 2,
                    "automatic global crop mode is parsed");
    passed &= check(
        loadedSettings.panTiltIntentDefined
        && loadedSettings.zoomIntentDefined
        && loadedSettings.imageIntentDefined,
        "legacy configs migrate with their existing camera intent categories defined");
    passed &= check(loadedSettings.presets[0].sceneDefined
                    && !loadedSettings.presets[0].trackingEnabled
                    && loadedSettings.presets[0].paperCropMode == 1,
                    "manual scene state is parsed");
    std::string legacyOffAutoZoom = withSetting(
        validConfigWithPreset("0.25", "-0.5", "1.75"),
        "preset1_auto_zoom", "enabled");
    passed &= check(loadRawConfig(
                        configPath, legacyOffAutoZoom, &loadedSettings)
                    && !loadedSettings.presets[0].autoZoom,
                    "legacy tracking-off scene auto zoom is safely normalized");
    passed &= check(
        loadedSettings.activeTrackingProfile.focusPolicy
            == TrackingFocusPolicy::Manual
        && !loadedSettings.activeTrackingProfile.autoZoom,
        "legacy tracking-off config migrates to a safe manual active profile");
    passed &= check(
        loadedSettings.trackingModeProfiles[0].focusPolicy
            == TrackingFocusPolicy::Face
        && loadedSettings.trackingModeProfiles[2].focusPolicy
            == TrackingFocusPolicy::Continuous
        && loadedSettings.trackingModeProfiles[4].focusPolicy
            == TrackingFocusPolicy::Continuous,
        "Group, Hand, and Desk receive approved independent defaults");

    std::string legacyHuman = validConfigWithPreset(
        "0.25", "-0.5", "1.75");
    legacyHuman = withSetting(legacyHuman, "face_tracking", "enabled");
    legacyHuman = withSetting(legacyHuman, "face_focus", "enabled");
    legacyHuman +=
        "ai_mode=2\n"
        "focus=73\n"
        "auto_zoom=enabled\n"
        "track_speed=4\n";
    passed &= check(loadRawConfig(
                        configPath, legacyHuman, &loadedSettings),
                    "legacy Human tracking config migrates without profile keys");
    passed &= check(
        loadedSettings.activeTrackingProfile.focusPolicy
            == TrackingFocusPolicy::Face
        && loadedSettings.activeTrackingProfile.manualFocusPosition == 73
        && loadedSettings.activeTrackingProfile.autoZoom
        && loadedSettings.activeTrackingProfile.trackSpeed == 4,
        "active legacy focus, fallback position, zoom, and speed are retained");
    passed &= check(
        loadedSettings.trackingModeProfiles[1]
            == loadedSettings.activeTrackingProfile,
        "legacy face and auto-zoom intent seeds the Human profile");
    passed &= check(
        loadedSettings.trackingModeProfiles[0].focusPolicy
            == TrackingFocusPolicy::Face
        && !loadedSettings.trackingModeProfiles[0].autoZoom
        && loadedSettings.trackingModeProfiles[2].focusPolicy
            == TrackingFocusPolicy::Continuous
        && !loadedSettings.trackingModeProfiles[2].autoZoom
        && loadedSettings.trackingModeProfiles[2].trackSpeed == 4,
        "legacy Human choices do not leak into other mode focus/zoom defaults");

    const std::string partialProfile = legacyHuman
        + "#@tiny2_hand_focus_policy=manual\n"
          "#@tiny2_hand_manual_focus=81\n"
          "#@tiny2_hand_auto_zoom=enabled\n"
          "#@tiny2_hand_track_speed=5\n";
    passed &= check(loadRawConfig(
                        configPath, partialProfile, &loadedSettings),
                    "explicit profile metadata is parsed");
    passed &= check(
        loadedSettings.trackingModeProfiles[2]
            == TrackingModeProfile{
                TrackingFocusPolicy::Manual, 81, true, 5},
        "explicit Hand profile overrides migration field-for-field");

    TrackingIntentState humanIntent{
        true,
        2,
        1,
        loadedSettings.trackingModeProfiles[1]
    };
    const TrackingIntentState deskIntent =
        applyAutomaticPaperCropTrackingPolicy(
            2, true, humanIntent,
            loadedSettings.trackingModeProfiles);
    passed &= check(
        deskIntent.enabled && deskIntent.aiMode == 5
        && deskIntent.aiSubMode == 0
        && deskIntent.profile == loadedSettings.trackingModeProfiles[4],
        "automatic paper crop deterministically selects the Desk profile");
    passed &= check(
        applyAutomaticPaperCropTrackingPolicy(
            0, true, deskIntent,
            loadedSettings.trackingModeProfiles) == deskIntent,
        "turning crop off leaves Desk intent active");

    passed &= check(!loadRawConfig(
                        configPath,
                        legacyHuman
                            + "#@tiny2_hand_focus_policy=FACE\n"),
                    "unknown focus policy spelling is rejected");
    passed &= check(!loadRawConfig(
                        configPath,
                        legacyHuman
                            + "#@tiny2_hand_manual_focus=101\n"),
                    "out-of-range profile focus is rejected");
    passed &= check(!loadRawConfig(
                        configPath,
                        legacyHuman
                            + "#@tiny2_hnad_track_speed=2\n"),
                    "misspelled profile metadata is rejected");
    passed &= check(!loadConfig(configPath, "nan", "-0.5", "1.75"),
                    "NaN preset pan is rejected");
    passed &= check(!loadConfig(configPath, "0.25", "inf", "1.75"),
                    "infinite preset tilt is rejected");
    passed &= check(!loadConfig(configPath, "0.25junk", "-0.5", "1.75"),
                    "partially numeric preset pan is rejected");
    passed &= check(!loadConfig(configPath, "0.25", "-0.5", "1.75garbage"),
                    "partially numeric preset zoom is rejected");
    const std::string validConfig =
        validConfigWithPreset("0.25", "-0.5", "1.75");
    passed &= check(!loadRawConfig(
                        configPath, withSetting(validConfig, "zoom", "nan")),
                    "NaN global zoom is rejected");
    passed &= check(!loadRawConfig(
                        configPath, withSetting(validConfig, "pan", "-inf")),
                    "infinite global pan is rejected");
    passed &= check(!loadRawConfig(
                        configPath, withSetting(validConfig, "tilt", "0.2junk")),
                    "partially numeric global tilt is rejected");

    std::string undefinedInvalidPreset =
        withSetting(validConfig, "preset1_defined", "disabled");
    undefinedInvalidPreset =
        withSetting(undefinedInvalidPreset, "preset1_pan", "nan");
    passed &= check(!loadRawConfig(configPath, undefinedInvalidPreset),
                    "undefined presets still reject non-finite positions");
    passed &= check(!loadRawConfig(
                        configPath,
                        validConfigWithPreset("0.25", "-0.5", "1.75") + "ai_mode=6\n"),
                    "transitional AI mode cannot be persisted as an operator mode");
    passed &= check(!loadRawConfig(configPath, validConfig + "ai_mode=2junk\n"),
                    "partially numeric AI mode is rejected");
    passed &= check(!loadRawConfig(configPath, validConfig + "ai_sub_mode=1junk\n"),
                    "partially numeric AI sub-mode is rejected");
    passed &= check(!loadRawConfig(configPath, validConfig + "track_speed=2junk\n"),
                    "partially numeric tracking speed is rejected");

    std::string invalidSubMode = validConfigWithPreset("0.25", "-0.5", "1.75");
    const std::string validSubMode = "preset1_ai_sub_mode=0";
    invalidSubMode.replace(invalidSubMode.find(validSubMode), validSubMode.size(),
                           "preset1_ai_sub_mode=5");
    passed &= check(!loadRawConfig(configPath, invalidSubMode),
                    "AI sub-mode sentinel cannot be persisted in a scene");
    passed &= check(!loadRawConfig(
                        configPath,
                        validConfig
                            + "#@camera_pan_tilt_intent_defined=maybe\n"),
                    "invalid camera intent metadata is rejected");

    Config profileRoundTrip;
    auto profileSettings = profileRoundTrip.getSettings();
    profileSettings.faceTracking = true;
    profileSettings.aiMode = 5;
    profileSettings.panTiltIntentDefined = false;
    profileSettings.zoomIntentDefined = false;
    profileSettings.imageIntentDefined = false;
    profileSettings.activeTrackingProfile = {
        TrackingFocusPolicy::Continuous, 67, false, 3};
    profileSettings.trackingModeProfiles[4] =
        profileSettings.activeTrackingProfile;
    profileSettings.presets[0].defined = true;
    profileSettings.presets[0].sceneDefined = true;
    profileSettings.presets[0].trackingEnabled = true;
    profileSettings.presets[0].aiMode = 5;
    profileSettings.presets[0].focusPolicy = TrackingFocusPolicy::Continuous;
    profileSettings.presets[0].manualFocusPosition = 67;
    profileSettings.presets[0].trackSpeed = 3;
    profileRoundTrip.setSettings(profileSettings);
    passed &= check(profileRoundTrip.save(),
                    "valid mode and scene profiles are serialized");
    const std::string serializedProfiles = readFile(configPath);
    passed &= check(
        serializedProfiles.find(
            "#@tiny2_desk_focus_policy=continuous")
            != std::string::npos
        && serializedProfiles.find(
            "#@preset1_manual_focus=67")
            != std::string::npos
        && serializedProfiles.find(
            "#@camera_pan_tilt_intent_defined=disabled")
            != std::string::npos,
        "profiles use rollback-compatible #@ metadata comments");
    Config profileReload;
    std::vector<Config::ValidationError> profileErrors;
    passed &= check(profileReload.load(profileErrors),
                    "serialized profiles reload successfully");
    const auto reloadedProfiles = profileReload.getSettings();
    passed &= check(
        reloadedProfiles.activeTrackingProfile
            == profileSettings.activeTrackingProfile
        && reloadedProfiles.trackingModeProfiles[4]
            == profileSettings.trackingModeProfiles[4]
        && reloadedProfiles.presets[0].focusPolicy
            == TrackingFocusPolicy::Continuous
        && reloadedProfiles.presets[0].manualFocusPosition == 67
        && reloadedProfiles.presets[0].trackSpeed == 3
        && !reloadedProfiles.panTiltIntentDefined
        && !reloadedProfiles.zoomIntentDefined
        && !reloadedProfiles.imageIntentDefined,
        "active, mode, preset, and camera-bound intent fields round-trip exactly");

    const std::string beforeWriteFailure = readFile(configPath);
    std::error_code permissionError;
    const auto originalDirectoryPermissions =
        std::filesystem::status(configDir, permissionError).permissions();
    passed &= check(!permissionError,
                    "config directory permissions can be inspected");
    if (!permissionError) {
        std::filesystem::permissions(
            configDir,
            std::filesystem::perms::owner_read
                | std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::replace,
            permissionError);
        passed &= check(!permissionError,
                        "config directory can be made non-writable for failure testing");
        bool writeRejected = false;
        if (!permissionError) {
            Config blockedSave;
            auto blockedSettings = blockedSave.getSettings();
            blockedSettings.startMinimized = true;
            blockedSave.setSettings(blockedSettings);
            writeRejected = !blockedSave.save();
        }

        std::error_code restoreError;
        std::filesystem::permissions(
            configDir, originalDirectoryPermissions,
            std::filesystem::perm_options::replace, restoreError);
        passed &= check(!restoreError,
                        "config directory permissions are restored after failure testing");
        passed &= check(writeRejected,
                        "I/O failure rejects a configuration save");
        passed &= check(readFile(configPath) == beforeWriteFailure,
                        "I/O failure leaves the previous configuration atomically intact");
    }

    const std::string sentinelContents = "preserve-existing-settings\n";
    {
        std::ofstream sentinel(configPath);
        sentinel << sentinelContents;
    }

    Config invalidGlobal;
    auto invalidSettings = invalidGlobal.getSettings();
    invalidSettings.zoom = std::numeric_limits<double>::quiet_NaN();
    invalidGlobal.setSettings(invalidSettings);
    passed &= check(!invalidGlobal.save(),
                    "in-memory non-finite global settings are not serialized");
    passed &= check(readFile(configPath) == sentinelContents,
                    "rejected save leaves the existing settings file untouched");

    Config invalidUndefinedPreset;
    invalidSettings = invalidUndefinedPreset.getSettings();
    invalidSettings.presets[0].defined = false;
    invalidSettings.presets[0].pan =
        std::numeric_limits<double>::quiet_NaN();
    invalidUndefinedPreset.setSettings(invalidSettings);
    passed &= check(!invalidUndefinedPreset.save(),
                    "invalid undefined preset values are not serialized");

    Config invalidPresetAi;
    invalidSettings = invalidPresetAi.getSettings();
    invalidSettings.presets[0].aiMode = 99;
    invalidPresetAi.setSettings(invalidSettings);
    passed &= check(!invalidPresetAi.save(),
                    "invalid in-memory preset AI mode is not serialized");

    Config invalidGlobalCrop;
    invalidSettings = invalidGlobalCrop.getSettings();
    invalidSettings.paperCropLeft =
        std::numeric_limits<double>::quiet_NaN();
    invalidGlobalCrop.setSettings(invalidSettings);
    passed &= check(!invalidGlobalCrop.save(),
                    "invalid in-memory global crop is not serialized");

    Config invalidPresetCrop;
    invalidSettings = invalidPresetCrop.getSettings();
    invalidSettings.presets[0].paperCropMode = 3;
    invalidPresetCrop.setSettings(invalidSettings);
    passed &= check(!invalidPresetCrop.save(),
                    "invalid in-memory preset crop is not serialized");

    Config invalidModeProfile;
    invalidSettings = invalidModeProfile.getSettings();
    invalidSettings.trackingModeProfiles[2].trackSpeed = 6;
    invalidModeProfile.setSettings(invalidSettings);
    passed &= check(!invalidModeProfile.save(),
                    "invalid mode profile is not serialized");

    Config invalidOffProfile;
    invalidSettings = invalidOffProfile.getSettings();
    invalidSettings.activeTrackingProfile.focusPolicy =
        TrackingFocusPolicy::Continuous;
    invalidOffProfile.setSettings(invalidSettings);
    passed &= check(!invalidOffProfile.save(),
                    "tracking-off active profile must remain manual");

    Config invalidPresetProfile;
    invalidSettings = invalidPresetProfile.getSettings();
    invalidSettings.presets[0].manualFocusPosition = 101;
    invalidPresetProfile.setSettings(invalidSettings);
    passed &= check(!invalidPresetProfile.save(),
                    "invalid undefined preset profile is not serialized");

    Config invalidLegacyFocus;
    invalidSettings = invalidLegacyFocus.getSettings();
    invalidSettings.focus = 101;
    invalidLegacyFocus.setSettings(invalidSettings);
    passed &= check(!invalidLegacyFocus.save(),
                    "invalid legacy focus compatibility value is rejected");

    const std::filesystem::path missingParent =
        root / "new-xdg-parent" / "nested";
    setenv("XDG_CONFIG_HOME", missingParent.c_str(), 1);
    Config firstSave;
    passed &= check(
        firstSave.save()
        && std::filesystem::exists(
            missingParent / "obsbot-control" / "settings.conf"),
        "first save creates the complete missing XDG parent hierarchy");

    std::filesystem::remove_all(root);
    return passed ? 0 : 1;
}
