#include "CameraSelection.h"

#include <algorithm>
#include <chrono>
#include <dev/devs.hpp>
#include <list>
#include <ostream>
#include <thread>

std::shared_ptr<Device> waitForSelectedCamera(const std::string &serial,
                                              bool requireExplicitSelection,
                                              int timeoutSeconds,
                                              std::ostream &progress,
                                              std::ostream &errors)
{
    Devices::get().setDevChangedCallback(
        [](std::string, bool, void *) {},
        nullptr);
    Devices::get().setEnableMdnsScan(false);

    progress << "Waiting for OBSBOT camera..." << '\n';
    std::list<std::shared_ptr<Device>> devices;
    for (int i = 0; i < timeoutSeconds * 10; ++i) {
        devices = Devices::get().getDevList();
        const bool targetFound = serial.empty()
            ? !devices.empty()
            : std::any_of(devices.begin(), devices.end(), [&](const auto &device) {
                return device->devSn() == serial;
            });
        if (targetFound) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            devices = Devices::get().getDevList();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (devices.empty()) {
        errors << "No OBSBOT devices found!" << '\n';
        return {};
    }

    if (!serial.empty()) {
        const auto match = std::find_if(devices.begin(), devices.end(), [&](const auto &device) {
            return device->devSn() == serial;
        });
        if (match == devices.end()) {
            errors << "No OBSBOT camera with serial " << serial << " was found." << '\n';
            return {};
        }
        return *match;
    }

    if (requireExplicitSelection && devices.size() > 1) {
        errors << "Multiple OBSBOT cameras found; use --serial to choose one:" << '\n';
        for (const auto &device : devices) {
            errors << "  " << device->devSn() << " (" << device->devName() << ")" << '\n';
        }
        return {};
    }

    return devices.front();
}
