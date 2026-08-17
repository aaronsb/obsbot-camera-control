#include "PresetCommand.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <dev/dev.hpp>
#include <ostream>
#include <thread>

namespace {

bool isTiny2Family(const std::shared_ptr<Device> &device)
{
    const auto product = device->productType();
    return product == ObsbotProdTiny2
        || product == ObsbotProdTiny2Lite
        || product == ObsbotProdTinySE;
}

bool isOriginalTinyFamily(const std::shared_ptr<Device> &device)
{
    const auto product = device->productType();
    return product == ObsbotProdTiny || product == ObsbotProdTiny4k;
}

bool isMeetFamily(const std::shared_ptr<Device> &device)
{
    const auto product = device->productType();
    return product == ObsbotProdMeet
        || product == ObsbotProdMeet4k
        || product == ObsbotProdMeet2
        || product == ObsbotProdMeetSE;
}

bool waitForTiny2AiMode(const std::shared_ptr<Device> &device,
                        int targetMode, int targetSubMode)
{
    constexpr int attempts = 20;
    constexpr auto retryDelay = std::chrono::milliseconds(200);
    for (int attempt = 0; attempt < attempts; ++attempt) {
        Device::CameraStatus status{};
        if (device->cameraGetCameraStatusU(status) == 0
            && status.tiny.ai_mode == targetMode
            && (targetMode != Device::AiWorkModeHuman
                || status.tiny.ai_sub_mode == targetSubMode)) {
            return true;
        }
        if (attempt + 1 < attempts) {
            std::this_thread::sleep_for(retryDelay);
        }
    }
    return false;
}

bool applyTiny2TrackingProfile(
    const std::shared_ptr<Device> &device,
    const TrackingModeProfile &profile,
    bool trackingEnabled)
{
    if (!isValidTrackingModeProfile(profile)) {
        return false;
    }

    bool speedApplied = true;
    if (trackingEnabled) {
        speedApplied = device->aiSetTrackSpeedTypeR(
            static_cast<Device::AiTrackSpeedType>(profile.trackSpeed)) == 0;
    }

    int32_t firstFocusResult = -1;
    int32_t secondFocusResult = -1;
    switch (profile.focusPolicy) {
    case TrackingFocusPolicy::Face:
        firstFocusResult = device->cameraSetFocusAbsolute(
            profile.manualFocusPosition, true);
        secondFocusResult = device->cameraSetFaceFocusR(true);
        break;
    case TrackingFocusPolicy::Continuous:
        firstFocusResult = device->cameraSetFaceFocusR(false);
        secondFocusResult = device->cameraSetFocusAbsolute(
            profile.manualFocusPosition, true);
        break;
    case TrackingFocusPolicy::Manual:
        firstFocusResult = device->cameraSetFaceFocusR(false);
        secondFocusResult = device->cameraSetFocusAbsolute(
            profile.manualFocusPosition, false);
        break;
    }

    return speedApplied
        && firstFocusResult == 0
        && secondFocusResult == 0;
}

} // namespace

void printPresetList(std::ostream &out, const Config::CameraSettings &settings)
{
    for (size_t i = 0; i < settings.presets.size(); ++i) {
        const auto &preset = settings.presets[i];
        out << "Preset " << (i + 1) << ": ";
        if (preset.defined) {
            if (preset.sceneDefined) {
                out << (preset.trackingEnabled ? "tracking" : "manual")
                    << ", AI mode " << preset.aiMode
                    << ", crop mode " << preset.paperCropMode << ", ";
            }
            out << "pan " << preset.pan << ", tilt " << preset.tilt
                << ", zoom " << preset.zoom << "x";
        } else {
            out << "empty";
        }
        out << '\n';
    }
}

bool applyPresetToCamera(std::shared_ptr<Device> device,
                         const Config::CameraSettings::PresetSlot &preset,
                         const Tiny2TrackingModeProfiles &modeProfiles,
                         std::ostream &progress,
                         std::ostream &errors)
{
    const bool valid = std::isfinite(preset.pan) && preset.pan >= -1.0 && preset.pan <= 1.0
        && std::isfinite(preset.tilt) && preset.tilt >= -1.0 && preset.tilt <= 1.0
        && std::isfinite(preset.zoom) && preset.zoom >= 1.0 && preset.zoom <= 2.0;
    if (!preset.defined || !valid) {
        errors << "Refusing to apply an undefined or invalid scene preset." << '\n';
        return false;
    }

    progress << "Applying scene preset: pan " << preset.pan
             << ", tilt " << preset.tilt << ", zoom " << preset.zoom << "x" << '\n';

    bool trackingApplied = true;
    const bool tiny2Family = isTiny2Family(device);
    const TrackingIntentState storedTracking{
        preset.trackingEnabled,
        preset.aiMode,
        preset.aiSubMode,
        {
            preset.focusPolicy,
            preset.manualFocusPosition,
            preset.autoZoom,
            preset.trackSpeed
        }
    };
    const TrackingIntentState effectiveIntent = preset.sceneDefined
        ? applyAutomaticPaperCropTrackingPolicy(
            preset.paperCropMode,
            tiny2Family,
            storedTracking,
            modeProfiles)
        : storedTracking;
    const bool automaticDeskScene = tiny2Family
        && preset.sceneDefined && preset.paperCropMode == 2;
    const bool effectiveTracking =
        preset.sceneDefined && effectiveIntent.enabled;
    int effectiveMode = effectiveIntent.aiMode;
    int effectiveSubMode = effectiveIntent.aiSubMode;
    TrackingModeProfile effectiveProfile = effectiveIntent.profile;
    if (automaticDeskScene) {
        progress << "  Automatic paper crop selects Tiny 2 Desk mode."
                 << '\n';
    }

    if (effectiveTracking) {
        if (tiny2Family) {
            if (effectiveMode == Device::AiWorkModeNone) {
                effectiveMode = Device::AiWorkModeHuman;
            }
            if (effectiveMode != Device::AiWorkModeHuman) {
                effectiveSubMode = 0;
            }
            const int32_t autoZoomResult =
                device->aiSetAiAutoZoomR(effectiveProfile.autoZoom);
            const int32_t aiResult = device->cameraSetAiModeU(
                static_cast<Device::AiWorkModeType>(effectiveMode),
                effectiveSubMode);
            trackingApplied = autoZoomResult == 0 && aiResult == 0;
            if (trackingApplied) {
                trackingApplied = waitForTiny2AiMode(
                    device, effectiveMode, effectiveSubMode);
            }
            if (trackingApplied) {
                trackingApplied = applyTiny2TrackingProfile(
                    device, effectiveProfile, true);
            }
        } else if (isOriginalTinyFamily(device)) {
            trackingApplied = device->aiSetTargetSelectR(true) == 0;
        } else if (isMeetFamily(device)) {
            const int32_t mediaResult =
                device->cameraSetMediaModeU(Device::MediaModeAutoFrame);
            int32_t framingResult = -1;
            if (mediaResult == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                framingResult = device->cameraSetAutoFramingModeU(
                    Device::AutoFrmSingle, Device::AutoFrmUpperBody);
            }
            trackingApplied = mediaResult == 0 && framingResult == 0;
        } else {
            trackingApplied = false;
        }
        if (!trackingApplied) {
            errors << "  Failed to restore the saved tracking profile." << '\n';
        }
    } else {
        if (tiny2Family) {
            effectiveProfile.focusPolicy = TrackingFocusPolicy::Manual;
            effectiveProfile.autoZoom = false;
            const int32_t autoZoomResult = device->aiSetAiAutoZoomR(false);
            const int32_t aiResult =
                device->cameraSetAiModeU(Device::AiWorkModeNone);
            trackingApplied = autoZoomResult == 0 && aiResult == 0;
            if (trackingApplied) {
                trackingApplied = waitForTiny2AiMode(
                    device, Device::AiWorkModeNone, 0);
            }
            if (trackingApplied) {
                trackingApplied = applyTiny2TrackingProfile(
                    device, effectiveProfile, false);
            }
        } else if (isOriginalTinyFamily(device)) {
            trackingApplied = device->aiSetTargetSelectR(false) == 0;
        } else if (isMeetFamily(device)) {
            trackingApplied =
                device->cameraSetMediaModeU(Device::MediaModeNormal) == 0;
        } else {
            trackingApplied = false;
        }
        if (!trackingApplied) {
            errors << "  Failed to disable tracking and restore manual focus."
                   << '\n';
        }
    }

    if (preset.paperCropMode != 0) {
        progress << "  Software paper crop is applied by the running GUI/virtual camera." << '\n';
    }

    if (effectiveTracking) {
        return trackingApplied;
    }

    if (!trackingApplied) {
        return false;
    }

    const int32_t panTiltResult = device->cameraSetPanTiltAbsolute(preset.pan, preset.tilt);
    if (panTiltResult != 0) {
        errors << "  Failed to set pan/tilt (code: " << panTiltResult << ")" << '\n';
        return false;
    }

    const int32_t zoomResult = device->cameraSetZoomAbsoluteR(preset.zoom);
    if (zoomResult != 0) {
        errors << "  Failed to set zoom (code: " << zoomResult << ")" << '\n';
    }

    return zoomResult == 0;
}
