#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <dev/devs.hpp>
#include <QCoreApplication>
#include "CameraSelection.h"
#include "CliOptions.h"
#include "Config.h"
#include "GuiRemoteClient.h"
#include "PresetCommand.h"

using namespace std;

// Forward declarations
bool handleConfigErrors(Config &config);
void applyConfigToCamera(shared_ptr<Device> dev, const Config::CameraSettings &settings);
void runInteractiveMode(shared_ptr<Device> dev);

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const CliParseResult parsed = parseCliOptions(argc, argv);
    if (!parsed.ok()) {
        cerr << "Error: " << parsed.error << "\n\n";
        printCliUsage(cerr, argv[0]);
        return 2;
    }
    if (parsed.options.showHelp) {
        printCliUsage(cout, argv[0]);
        return 0;
    }

    const CliAction action = parsed.options.action;
    const bool interactive = action == CliAction::Interactive;
    cout << "OBSBOT Control" << (interactive ? " - Interactive Mode" : "") << endl;

    Config config;
    vector<Config::ValidationError> errors;
    if (!config.load(errors)) {
        if (action == CliAction::RecallPreset || action == CliAction::ListPresets) {
            cerr << "Configuration is invalid; preset actions never prompt for input." << endl;
            for (const auto &error : errors) {
                cerr << (error.lineNumber > 0 ? "Line " + to_string(error.lineNumber) + ": " : "")
                     << error.message << endl;
            }
            return 2;
        }
        if (!handleConfigErrors(config)) {
            cout << "Continuing without saving settings." << endl;
        }
    } else if (!config.configExists()) {
        cout << "No config file found. Using defaults." << endl;
    } else {
        cout << "Configuration loaded from: " << config.getConfigPath() << endl;
    }

    const auto settings = config.getSettings();
    if (action == CliAction::ListPresets) {
        printPresetList(cout, settings);
        return 0;
    }

    const Config::CameraSettings::PresetSlot *requestedPreset = nullptr;
    if (action == CliAction::RecallPreset) {
        requestedPreset = &settings.presets[static_cast<size_t>(parsed.options.presetNumber - 1)];
        if (!requestedPreset->defined) {
            cerr << "Preset " << parsed.options.presetNumber
                 << " is empty. Save it in the GUI before recalling it." << endl;
            return 2;
        }
    }

    if (action == CliAction::RecallPreset && parsed.options.serial.empty()) {
        const GuiRemoteRecallResult remote = recallPresetViaRunningGui(parsed.options.presetNumber);
        if (remote.status == GuiRemoteRecallResult::Status::Accepted) {
            cout << "Scene preset " << parsed.options.presetNumber
                 << " recalled through the running GUI." << endl;
            return 0;
        }
        if (remote.status == GuiRemoteRecallResult::Status::Rejected
            || remote.status == GuiRemoteRecallResult::Status::Error) {
            cerr << (remote.message.empty() ? "The running GUI could not recall the preset."
                                           : remote.message)
                 << endl;
            return 1;
        }
    }

    const auto dev = waitForSelectedCamera(
        parsed.options.serial,
        action == CliAction::RecallPreset,
        10,
        cout,
        cerr);
    if (!dev) {
        return 1;
    }

    cout << "\nFound device:" << endl;
    cout << "  Name: " << dev->devName() << endl;
    cout << "  SN: " << dev->devSn() << endl;
    cout << "  Version: " << dev->devVersion() << endl;
    cout << "  Product Type: " << dev->productType() << endl;

    if (dev->productType() != ObsbotProdMeet2) {
        cout << "\nNote: This camera is not a Meet 2." << endl;
        cout << "      Some features may not work as expected." << endl;
    }

    if (action == CliAction::RecallPreset) {
        if (!applyPresetToCamera(dev, *requestedPreset, cout, cerr)) {
            return 1;
        }
        cout << "Scene preset " << parsed.options.presetNumber << " recalled." << endl;
    } else if (interactive) {
        runInteractiveMode(dev);
    } else {
        cout << "\nApplying configuration to camera..." << endl;
        applyConfigToCamera(dev, settings);
        cout << "Configuration applied successfully." << endl;
        cout << "Camera settings have been updated." << endl;
    }

    return 0;
}

bool handleConfigErrors(Config &config)
{
    vector<Config::ValidationError> errors;
    config.load(errors);

    cout << "\n=== Configuration Error ===" << endl;
    for (const auto &err : errors) {
        if (err.lineNumber > 0) {
            cout << "Line " << err.lineNumber << ": " << err.message << endl;
        } else {
            cout << err.message << endl;
        }
    }

    while (true) {
        cout << "\nOptions:" << endl;
        cout << "  1. Ignore (continue without saving)" << endl;
        cout << "  2. Reset to defaults" << endl;
        cout << "  3. Try again (re-read config file)" << endl;
        cout << "Choose option (1-3): ";

        string choice;
        cin >> choice;

        if (choice == "1") {
            config.disableSaving();
            return false;
        } else if (choice == "2") {
            config.resetToDefaults(true);
            cout << "Config reset to defaults and saved." << endl;
            return true;
        } else if (choice == "3") {
            errors.clear();
            if (config.load(errors)) {
                cout << "Config loaded successfully!" << endl;
                return true;
            } else {
                cout << "\nConfig still has errors:" << endl;
                for (const auto &err : errors) {
                    if (err.lineNumber > 0) {
                        cout << "Line " << err.lineNumber << ": " << err.message << endl;
                    } else {
                        cout << err.message << endl;
                    }
                }
                // Continue loop to show options again
            }
        } else {
            cout << "Invalid choice. Please enter 1, 2, or 3." << endl;
        }
    }
}

void applyConfigToCamera(shared_ptr<Device> dev, const Config::CameraSettings &settings)
{
    int32_t ret;

    // Apply face tracking
    if (settings.faceTracking) {
        cout << "  Enabling face tracking..." << endl;
        ret = dev->cameraSetMediaModeU(Device::MediaModeAutoFrame);
        if (ret != 0) {
            cout << "    Failed to set MediaMode (code: " << ret << ")" << endl;
        } else {
            this_thread::sleep_for(chrono::milliseconds(500));
            ret = dev->cameraSetAutoFramingModeU(Device::AutoFrmSingle, Device::AutoFrmUpperBody);
            if (ret != 0) {
                cout << "    Failed to set AutoFraming mode (code: " << ret << ")" << endl;
            }
        }
    } else {
        cout << "  Disabling face tracking..." << endl;
        ret = dev->cameraSetMediaModeU(Device::MediaModeNormal);
        if (ret != 0) {
            cout << "    Failed to set MediaMode (code: " << ret << ")" << endl;
        }
    }

    // Apply HDR
    cout << "  Setting HDR: " << (settings.hdr ? "On" : "Off") << endl;
    ret = dev->cameraSetWdrR(settings.hdr ? Device::DevWdrModeDol2TO1 : Device::DevWdrModeNone);
    if (ret != 0) {
        cout << "    Failed (code: " << ret << ")" << endl;
    }

    // Apply FOV
    const char* fovNames[] = {"Wide (86°)", "Medium (78°)", "Narrow (65°)"};
    cout << "  Setting FOV: " << fovNames[settings.fov] << endl;
    Device::FovType fovType = settings.fov == 0 ? Device::FovType86 :
                               (settings.fov == 1 ? Device::FovType78 : Device::FovType65);
    ret = dev->cameraSetFovU(fovType);
    if (ret != 0) {
        cout << "    Failed (code: " << ret << ")" << endl;
    }

    // Apply Face AE
    cout << "  Setting Face AE: " << (settings.faceAE ? "On" : "Off") << endl;
    ret = dev->cameraSetFaceAER(settings.faceAE);
    if (ret != 0) {
        cout << "    Failed (code: " << ret << ")" << endl;
    }

    // Apply Face Focus
    cout << "  Setting Face Focus: " << (settings.faceFocus ? "On" : "Off") << endl;
    ret = dev->cameraSetFaceFocusR(settings.faceFocus);
    if (ret != 0) {
        cout << "    Failed (code: " << ret << ")" << endl;
    }

    // Apply Zoom
    cout << "  Setting Zoom: " << settings.zoom << "x" << endl;
    ret = dev->cameraSetZoomAbsoluteR(settings.zoom);
    if (ret != 0) {
        cout << "    Failed (code: " << ret << ")" << endl;
    }

    // Apply Pan/Tilt
    cout << "  Setting Pan/Tilt: " << settings.pan << ", " << settings.tilt << endl;
    ret = dev->cameraSetPanTiltAbsolute(settings.pan, settings.tilt);
    if (ret != 0) {
        cout << "    Failed (code: " << ret << ")" << endl;
    }

    // Apply Image Controls
    cout << "  Setting Brightness: " << settings.brightness << endl;
    ret = dev->cameraSetImageBrightnessR(settings.brightness);
    if (ret != 0) {
        cout << "    Failed (code: " << ret << ")" << endl;
    }

    cout << "  Setting Contrast: " << settings.contrast << endl;
    ret = dev->cameraSetImageContrastR(settings.contrast);
    if (ret != 0) {
        cout << "    Failed (code: " << ret << ")" << endl;
    }

    cout << "  Setting Saturation: " << settings.saturation << endl;
    ret = dev->cameraSetImageSaturationR(settings.saturation);
    if (ret != 0) {
        cout << "    Failed (code: " << ret << ")" << endl;
    }

    const char* wbNames[] = {"Auto", "Daylight", "Fluorescent", "Tungsten", "Flash", "Fine", "Cloudy", "Shade"};
    cout << "  Setting White Balance: " << wbNames[settings.whiteBalance] << endl;
    Device::DevWhiteBalanceType wbType = static_cast<Device::DevWhiteBalanceType>(settings.whiteBalance);
    ret = dev->cameraSetWhiteBalanceR(wbType, 0);
    if (ret != 0) {
        cout << "    Failed (code: " << ret << ")" << endl;
    }
}

void runInteractiveMode(shared_ptr<Device> dev)
{
    cout << "\n=== Interactive Camera Control Menu ===" << endl;
    cout << "\n--- PTZ Control ---" << endl;
    cout << "1. Enable Face Tracking" << endl;
    cout << "2. Disable Face Tracking" << endl;
    cout << "3. Zoom In" << endl;
    cout << "4. Zoom Out" << endl;
    cout << "5. Pan Left" << endl;
    cout << "6. Pan Right" << endl;
    cout << "7. Tilt Up" << endl;
    cout << "8. Tilt Down" << endl;
    cout << "9. Center View" << endl;
    cout << "\n--- Focus Control ---" << endl;
    cout << "a. Enable Auto Focus" << endl;
    cout << "m. Set Manual Focus (0-100)" << endl;
    cout << "k. Focus Increase (Manual Mode)" << endl;
    cout << "j. Focus Decrease (Manual Mode)" << endl;
    cout << "\n--- HDR Control ---" << endl;
    cout << "h. Enable HDR" << endl;
    cout << "H. Disable HDR" << endl;
    cout << "\n--- AI Mode Control ---" << endl;
    cout << "i. Enable AI Mode (select mode)" << endl;
    cout << "I. Disable AI Mode" << endl;
    cout << "\n--- Other ---" << endl;
    cout << "0. Get Camera Status" << endl;
    cout << "q. Quit" << endl;

    // Track current pan/tilt/zoom position
    double current_pan = 0.0;
    double current_tilt = 0.0;
    double current_zoom = 1.0;
    int current_focus = 50;  // Default to middle position
    const double ptz_step = 0.1;
    const double zoom_step = 0.1;
    const int focus_step = 5;  // 5% increments

    string cmd;
    cout << "\nEnter command: ";
    while (cin >> cmd) {
        if (cmd == "q") break;

        char firstChar = cmd[0];
        int choice = (firstChar >= '0' && firstChar <= '9') ? (firstChar - '0') : firstChar;
        int32_t ret;

        switch (choice) {
            case 1:
                cout << "Enabling face tracking..." << endl;
                ret = dev->cameraSetMediaModeU(Device::MediaModeAutoFrame);
                if (ret == 0) {
                    this_thread::sleep_for(chrono::milliseconds(500));
                    ret = dev->cameraSetAutoFramingModeU(Device::AutoFrmSingle, Device::AutoFrmUpperBody);
                    cout << (ret == 0 ? "Success" : "Failed") << endl;
                } else {
                    cout << "Failed (code: " << ret << ")" << endl;
                }
                break;

            case 2:
                cout << "Disabling face tracking..." << endl;
                ret = dev->cameraSetMediaModeU(Device::MediaModeNormal);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;

            case 3:
                current_zoom += zoom_step;
                if (current_zoom > 2.0) current_zoom = 2.0;
                cout << "Zooming in (" << current_zoom << "x)..." << endl;
                ret = dev->cameraSetZoomAbsoluteR(current_zoom);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;

            case 4:
                current_zoom -= zoom_step;
                if (current_zoom < 1.0) current_zoom = 1.0;
                cout << "Zooming out (" << current_zoom << "x)..." << endl;
                ret = dev->cameraSetZoomAbsoluteR(current_zoom);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;

            case 5:
                current_pan -= ptz_step;
                if (current_pan < -1.0) current_pan = -1.0;
                cout << "Panning left..." << endl;
                ret = dev->cameraSetPanTiltAbsolute(current_pan, current_tilt);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;

            case 6:
                current_pan += ptz_step;
                if (current_pan > 1.0) current_pan = 1.0;
                cout << "Panning right..." << endl;
                ret = dev->cameraSetPanTiltAbsolute(current_pan, current_tilt);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;

            case 7:
                current_tilt += ptz_step;
                if (current_tilt > 1.0) current_tilt = 1.0;
                cout << "Tilting up..." << endl;
                ret = dev->cameraSetPanTiltAbsolute(current_pan, current_tilt);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;

            case 8:
                current_tilt -= ptz_step;
                if (current_tilt < -1.0) current_tilt = -1.0;
                cout << "Tilting down..." << endl;
                ret = dev->cameraSetPanTiltAbsolute(current_pan, current_tilt);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;

            case 9:
                current_pan = 0.0;
                current_tilt = 0.0;
                cout << "Centering view..." << endl;
                ret = dev->cameraSetPanTiltAbsolute(current_pan, current_tilt);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;

            case 0: {
                auto status = dev->cameraStatus();
                cout << "\nCamera Status:" << endl;
                cout << "  AI Mode: " << (int)status.tiny.ai_mode << endl;
                cout << "  Zoom: " << status.tiny.zoom_ratio << "%" << endl;
                cout << "  HDR: " << (status.tiny.hdr ? "On" : "Off") << endl;
                cout << "  Face AE: " << (status.tiny.face_ae ? "On" : "Off") << endl;
                cout << "  Auto Focus: " << (status.tiny.auto_focus ? "On" : "Off") << endl;
                break;
            }

            case 'a': {
                cout << "Enabling auto focus..." << endl;
                int32_t ret = dev->cameraSetFocusAbsolute(0, true);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;
            }

            case 'm': {
                cout << "Enter focus value (0-100): ";
                int focus_val;
                cin >> focus_val;
                if (focus_val < 0) focus_val = 0;
                if (focus_val > 100) focus_val = 100;
                current_focus = focus_val;
                cout << "Setting manual focus to " << focus_val << "..." << endl;
                int32_t ret = dev->cameraSetFocusAbsolute(focus_val, false);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;
            }

            case 'k': {
                current_focus += focus_step;
                if (current_focus > 100) current_focus = 100;
                cout << "Increasing focus to " << current_focus << "..." << endl;
                int32_t ret = dev->cameraSetFocusAbsolute(current_focus, false);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;
            }

            case 'j': {
                current_focus -= focus_step;
                if (current_focus < 0) current_focus = 0;
                cout << "Decreasing focus to " << current_focus << "..." << endl;
                int32_t ret = dev->cameraSetFocusAbsolute(current_focus, false);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;
            }

            case 'h': {
                cout << "Enabling HDR..." << endl;
                int32_t ret = dev->cameraSetWdrR(1);  // 1 = DevWdrModeDol2TO1 (HDR enabled)
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;
            }

            case 'H': {
                cout << "Disabling HDR..." << endl;
                int32_t ret = dev->cameraSetWdrR(0);  // 0 = DevWdrModeNone (HDR disabled)
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;
            }

            case 'i': {
                cout << "\nAI Mode Selection:" << endl;
                cout << "0. None (AI Off)" << endl;
                cout << "1. Group Tracking" << endl;
                cout << "2. Single Human Tracking" << endl;
                cout << "3. Hand Tracking" << endl;
                cout << "4. Whiteboard Mode" << endl;
                cout << "5. Desk Mode" << endl;
                cout << "Enter AI mode (0-5): ";
                int ai_mode;
                cin >> ai_mode;
                if (ai_mode >= 0 && ai_mode <= 5) {
                    cout << "Setting AI mode to " << ai_mode << "..." << endl;
                    int32_t ret = dev->cameraSetAiModeU(static_cast<Device::AiWorkModeType>(ai_mode));
                    cout << (ret == 0 ? "Success" : "Failed") << endl;
                } else {
                    cout << "Invalid AI mode (0-5)" << endl;
                }
                break;
            }

            case 'I': {
                cout << "Disabling AI Mode..." << endl;
                int32_t ret = dev->cameraSetAiModeU(Device::AiWorkModeNone);
                cout << (ret == 0 ? "Success" : "Failed") << endl;
                break;
            }

            default:
                cout << "Unknown command" << endl;
        }

        cout << "\nEnter command (or 'q' to quit): ";
    }

    cout << "Exiting..." << endl;
}
