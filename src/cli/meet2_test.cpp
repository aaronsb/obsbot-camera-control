#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <dev/devs.hpp>
#include "Config.h"

using namespace std;

// Forward declarations
bool handleConfigErrors(Config &config);
void applyConfigToCamera(shared_ptr<Device> dev, const Config::CameraSettings &settings);
void runInteractiveMode(shared_ptr<Device> dev);

int main(int argc, char **argv)
{
    bool interactive = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) {
            interactive = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            cout << "OBSBOT Control - CLI Tool" << endl;
            cout << "\nUsage: " << argv[0] << " [options]" << endl;
            cout << "\nOptions:" << endl;
            cout << "  -i, --interactive    Run in interactive menu mode" << endl;
            cout << "  -h, --help           Show this help message" << endl;
            cout << "\nDefault behavior:" << endl;
            cout << "  Loads configuration from ~/.config/obsbot-control/settings.conf" << endl;
            cout << "  Applies settings to camera and exits" << endl;
            return 0;
        }
    }

    cout << "OBSBOT Control" << (interactive ? " - Interactive Mode" : "") << endl;

    // Load configuration
    Config config;
    vector<Config::ValidationError> errors;
    if (!config.load(errors)) {
        // Config has validation errors
        if (!handleConfigErrors(config)) {
            cout << "Continuing without saving settings." << endl;
        }
    } else {
        if (!config.configExists()) {
            cout << "No config file found. Using defaults." << endl;
        } else {
            cout << "Configuration loaded from: " << config.getConfigPath() << endl;
        }
    }

    // Device detection callback
    bool device_connected = false;
    auto onDevChanged = [&device_connected](std::string dev_sn, bool connected, void *param) {
        if (connected) {
            cout << "Device " << dev_sn << " connected" << endl;
            device_connected = true;
        } else {
            cout << "Device " << dev_sn << " disconnected" << endl;
        }
    };

    // Register device detection
    Devices::get().setDevChangedCallback(onDevChanged, nullptr);
    Devices::get().setEnableMdnsScan(false);  // USB only

    // Wait for device detection with timeout
    cout << "Waiting for OBSBOT camera..." << endl;
    const int timeout_seconds = 10;
    for (int i = 0; i < timeout_seconds * 10; i++) {
        if (device_connected) {
            // Give a moment for device list to be populated after callback
            this_thread::sleep_for(chrono::milliseconds(200));
            break;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    auto dev_list = Devices::get().getDevList();
    if (dev_list.empty()) {
        cout << "No OBSBOT devices found!" << endl;
        return 1;
    }

    // Get first device
    auto dev = dev_list.front();
    cout << "\nFound device:" << endl;
    cout << "  Name: " << dev->devName() << endl;
    cout << "  SN: " << dev->devSn() << endl;
    cout << "  Version: " << dev->devVersion() << endl;
    cout << "  Product Type: " << dev->productType() << endl;

    // Note: This app was primarily tested with Meet 2, but may work with other models
    if (dev->productType() != ObsbotProdMeet2) {
        cout << "\nNote: This camera is not a Meet 2." << endl;
        cout << "      Some features may not work as expected." << endl;
    }

    if (interactive) {
        // Interactive mode - run menu
        runInteractiveMode(dev);
    } else {
        // Non-interactive mode - apply config and exit
        cout << "\nApplying configuration to camera..." << endl;
        auto settings = config.getSettings();
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
