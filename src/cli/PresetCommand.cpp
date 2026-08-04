#include "PresetCommand.h"

#include <cmath>
#include <cstdint>
#include <dev/dev.hpp>
#include <ostream>

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
    if (preset.sceneDefined && preset.trackingEnabled) {
        const int32_t mediaResult = device->cameraSetMediaModeU(Device::MediaModeAutoFrame);
        const int32_t aiResult = device->cameraSetAiModeU(
            static_cast<Device::AiWorkModeType>(preset.aiMode), preset.aiSubMode);
        const int32_t autoZoomResult = device->aiSetAiAutoZoomR(preset.autoZoom);
        trackingApplied = mediaResult == 0 && aiResult == 0 && autoZoomResult == 0;
        if (!trackingApplied) {
            errors << "  Failed to restore the saved tracking mode." << '\n';
        }
    } else {
        const int32_t aiResult = device->cameraSetAiModeU(Device::AiWorkModeNone);
        const int32_t mediaResult = device->cameraSetMediaModeU(Device::MediaModeNormal);
        trackingApplied = aiResult == 0 && mediaResult == 0;
        if (!trackingApplied) {
            errors << "  Failed to disable tracking before manual positioning." << '\n';
        }
    }

    if (preset.paperCropMode != 0) {
        progress << "  Software paper crop is applied by the running GUI/virtual camera." << '\n';
    }

    if (preset.sceneDefined && preset.trackingEnabled) {
        return trackingApplied;
    }

    const int32_t panTiltResult = device->cameraSetPanTiltAbsolute(preset.pan, preset.tilt);
    if (panTiltResult != 0) {
        errors << "  Failed to set pan/tilt (code: " << panTiltResult << ")" << '\n';
    }

    const int32_t zoomResult = device->cameraSetZoomAbsoluteR(preset.zoom);
    if (zoomResult != 0) {
        errors << "  Failed to set zoom (code: " << zoomResult << ")" << '\n';
    }

    return trackingApplied && panTiltResult == 0 && zoomResult == 0;
}
