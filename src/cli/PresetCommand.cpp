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
        errors << "Refusing to apply an undefined or invalid position preset." << '\n';
        return false;
    }

    progress << "Applying position preset: pan " << preset.pan
             << ", tilt " << preset.tilt << ", zoom " << preset.zoom << "x" << '\n';

    const int32_t panTiltResult = device->cameraSetPanTiltAbsolute(preset.pan, preset.tilt);
    if (panTiltResult != 0) {
        errors << "  Failed to set pan/tilt (code: " << panTiltResult << ")" << '\n';
    }

    const int32_t zoomResult = device->cameraSetZoomAbsoluteR(preset.zoom);
    if (zoomResult != 0) {
        errors << "  Failed to set zoom (code: " << zoomResult << ")" << '\n';
    }

    return panTiltResult == 0 && zoomResult == 0;
}
