#ifndef PRESETCOMMAND_H
#define PRESETCOMMAND_H

#include <iosfwd>
#include <memory>
#include "Config.h"

class Device;

void printPresetList(std::ostream &out, const Config::CameraSettings &settings);
bool applyPresetToCamera(std::shared_ptr<Device> device,
                         const Config::CameraSettings::PresetSlot &preset,
                         const Tiny2TrackingModeProfiles &modeProfiles,
                         std::ostream &progress,
                         std::ostream &errors);

#endif // PRESETCOMMAND_H
