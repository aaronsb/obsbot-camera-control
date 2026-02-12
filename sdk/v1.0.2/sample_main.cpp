#include <iostream>
#include <vector>
#include <thread>
#include <codecvt>
#include <algorithm>
#include <cstring>

#include <dev/devs.hpp>

using namespace std;

/// device sn list
std::vector<std::string> kDevs;
std::shared_ptr<Device> dev;

/// call when detect device connected or disconnected
void onDevChanged(std::string dev_sn, bool in_out, void *param)
{
    cout << "Device sn: " << dev_sn << (in_out ? " Connected" : " DisConnected") << endl;

    auto it = std::find(kDevs.begin(), kDevs.end(), dev_sn);
    if (in_out)
    {
        if (it == kDevs.end())
        {
            kDevs.emplace_back(dev_sn);
        }
    }
    else
    {
        if (it != kDevs.end())
        {
            kDevs.erase(it);
        }
    }

    cout << "Device num: " << kDevs.size() << endl;
}

/// call when camera's status update
void onDevStatusUpdated(void *param, const void *data)
{
    auto *status = static_cast<const Device::CameraStatus *>(data);
    switch (dev->productType())
    {
        /// for tiny series
    case ObsbotProdTiny:
    case ObsbotProdTiny4k:
    case ObsbotProdTiny2:
    {
        cout << dev->devName().c_str() << " status update:" << endl;
        cout << "zoom value: " << status->tiny.zoom_ratio << endl;
        cout << "ai mode: " << status->tiny.ai_mode << endl;
        break;
    }
        /// for meet series
    case ObsbotProdMeet:
    case ObsbotProdMeet4k:
    {
        cout << dev->devName().c_str() << " status update:" << endl;
        cout << "zoom value: " << status->meet.zoom_ratio << endl;
        cout << "background mode: " << status->meet.bg_mode << endl;
        break;
    }
        /// for tail air
    case ObsbotProdTailAir:
    {
        cout << dev->devName().c_str() << " status update:" << endl;
        cout << "zoom value: " << status->tail_air.digi_zoom_ratio << endl;
        cout << "ai mode: " << status->tail_air.ai_type << endl;
        break;
    }
    default:;
    }
}

/// call when device event notify
void onDevEventNotify(void *param, int event_type, const void *result)
{
    cout << "device event notify, event_type: " << event_type << endl;
}

/// call when file download finished
void onFileDownload(void *param, unsigned int file_type, int result, void *rev_param)
{
    cout << "file download callback, file_type: " << file_type << " result: " << result << endl;
}


std::string getProductNameByType(ObsbotProductType type)
{
    std::string productName = "UnKnown";
    switch (type)
    {
    case ObsbotProdTiny:
        productName = "Tiny";
        break;
    case ObsbotProdTiny4k:
        productName = "Tiny4K";
        break;
    case ObsbotProdMeet:
        productName = "Meet";
        break;
    case ObsbotProdMeet4k:
        productName = "Meet4K";
        break;
    case ObsbotProdMe:
        productName = "Me";
        break;
    case ObsbotProdTailAir:
        productName = "TailAir";
        break;
    case ObsbotProdTiny2:
        productName = "Tiny2";
        break;
    case ObsbotProdHDMIBox:
        productName = "HDMIBox";
        break;
    case ObsbotProdButt:
        productName = "Butt";
        break;
    default:
        break;
    }

    return productName;
}

static void zoneTrackInitPosCb(void *param, const void *rcvd_data)
{
    if (!rcvd_data)
    {
        return;
    }

    auto err_or_len = *reinterpret_cast<const int8_t *>(rcvd_data);
    if (err_or_len < 0)
    {
        cout << "zone track, response error!!!" << endl;
        return;
    }

    auto *bytes = reinterpret_cast<const uint8_t *>(rcvd_data);
    auto *preset_info = reinterpret_cast<const Device::PresetPosInfo *>(bytes + 1);
    cout << "zone track, init pos info:" << endl;
    cout << "yaw: " << preset_info->yaw << endl;
    cout << "pitch: " << preset_info->pitch << endl;
    cout << "zoom: " << preset_info->zoom << endl;
}

enum ZoneTrackRange
{
    ZoneTrackRangeYawMin,
    ZoneTrackRangeYawMax,
    ZoneTrackRangePitchMin,
    ZoneTrackRangePitchMax,
};

static void zoneTrackRangeCb(void *param, const void *rcvd_data, ZoneTrackRange range)
{
    if (!rcvd_data)
    {
        return;
    }

    auto err_or_len = *reinterpret_cast<const int8_t *>(rcvd_data);
    if (err_or_len < 0)
    {
        cout << "zone track, response error!!!" << endl;
        return;
    }

    auto *bytes = reinterpret_cast<const uint8_t *>(rcvd_data);
    auto *limited_value = reinterpret_cast<const float *>(bytes + 1);

    switch (range)
    {
    case ZoneTrackRangeYawMin:
        cout << "zone track range, yaw min: " << *limited_value << endl;
        break;
    case ZoneTrackRangeYawMax:
        cout << "zone track range, yaw max: " << *limited_value << endl;
        break;
    case ZoneTrackRangePitchMin:
        cout << "zone track range, pitch min: " << *limited_value << endl;
        break;
    case ZoneTrackRangePitchMax:
        cout << "zone track range, pitch max: " << *limited_value << endl;
        break;
    default:;
    }
}

static void zoneTrackRangeYawMinCb(void *param, const void *rcvd_data)
{
    zoneTrackRangeCb(param, rcvd_data, ZoneTrackRangeYawMin);
}

static void zoneTrackRangeYawMaxCb(void *param, const void *rcvd_data)
{
    zoneTrackRangeCb(param, rcvd_data, ZoneTrackRangeYawMax);
}

static void zoneTrackRangePitchMinCb(void *param, const void *rcvd_data)
{
    zoneTrackRangeCb(param, rcvd_data, ZoneTrackRangePitchMin);
}

static void zoneTrackRangePitchMaxCb(void *param, const void *rcvd_data)
{
    zoneTrackRangeCb(param, rcvd_data, ZoneTrackRangePitchMax);
}

void getZoneTrackInfoForTinySE(std::string dev_sn)
{
    /// Get tinySE
    auto tinySE = Devices::get().getDevBySn(dev_sn);
    if (!tinySE)
    {
        return;
    }

    /// Get device's params, sync or nonblock
    bool sync = true;
    if (sync)
    {
        /// zone track init pos
        Device::PresetPosInfo init_pos;
        tinySE->aiGetLimitedZoneTrackInitPosR(&init_pos, 0, nullptr, nullptr, Device::Block);
        cout << "zone track, init pos info: " << endl;
        cout << "yaw: " << init_pos.yaw << endl;
        cout << "pitch: " << init_pos.pitch << endl;
        cout << "zoom: " << init_pos.zoom << endl;

        /// zone track range
        float curr_yaw_min, curr_yaw_max, curr_pitch_min, curr_pitch_max;
        tinySE->aiGetLimitedZoneTrackYawMinR(curr_yaw_min, 0, nullptr, nullptr, Device::Block);
        cout << "zone track range, yaw min: " << curr_yaw_min << endl;
        tinySE->aiGetLimitedZoneTrackYawMaxR(curr_yaw_max, 0, nullptr, nullptr, Device::Block);
        cout << "zone track range, yaw max: " << curr_yaw_max << endl;
        tinySE->aiGetLimitedZoneTrackPitchMinR(curr_pitch_min, 0, nullptr, nullptr, Device::Block);
        cout << "zone track range, pitch min: " << curr_pitch_min << endl;
        tinySE->aiGetLimitedZoneTrackPitchMaxR(curr_pitch_max, 0, nullptr, nullptr, Device::Block);
        cout << "zone track range, pitch max: " << curr_pitch_max << endl;
    } else {
        /// zone track init pos
        tinySE->aiGetLimitedZoneTrackInitPosR(nullptr, 0, zoneTrackInitPosCb, nullptr, Device::NonBlock);

        /// zone track range
        float curr_yaw_min, curr_yaw_max, curr_pitch_min, curr_pitch_max;
        tinySE->aiGetLimitedZoneTrackYawMinR(curr_yaw_min, 0, zoneTrackRangeYawMinCb, nullptr, Device::NonBlock);
        tinySE->aiGetLimitedZoneTrackYawMaxR(curr_yaw_max, 0, zoneTrackRangeYawMaxCb, nullptr, Device::NonBlock);
        tinySE->aiGetLimitedZoneTrackPitchMinR(curr_pitch_min, 0, zoneTrackRangePitchMinCb, nullptr, Device::NonBlock);
        tinySE->aiGetLimitedZoneTrackPitchMaxR(curr_pitch_max, 0, zoneTrackRangePitchMaxCb, nullptr, Device::NonBlock);
    }
    /// zone track auto select target
    bool auto_select_target = false;
    dev->aiGetLimitedZoneTrackAutoSelectR(auto_select_target);
    cout << "zone track auto select new target: " << auto_select_target << endl;
}

void setZoneTrackInfoForTinySE(std::string dev_sn)
{
    /// Get tinySE
    auto tinySE = Devices::get().getDevBySn(dev_sn);
    if (!tinySE)
    {
        return;
    }

    /// Open the special led pattern before setting the zone tracking
    tinySE->cameraSetLedCtrlU(true);
    /// Disable the auto tracking of gimbal before setting
    tinySE->aiSetZoneTrackGimbalEnabledR(false);

    /// Set zone track params
    /// zone track range
    float yaw_min = -110;
    float yaw_max = 110;
    float pitch_min = -45;
    float pitch_max = 45;
    /// It is recommended to obtain the pos information from the device after moving the gimbal.
    bool get_pos_from_gimbal = true;
    if (get_pos_from_gimbal) {
        Device::AiGimbalStateInfo state_info;
        dev->aiGetGimbalStateR(&state_info);
        yaw_min = state_info.yaw_motor;
        pitch_min = state_info.pitch_motor;
    }
    tinySE->aiSetLimitedZoneTrackYawMinR(yaw_min);
    tinySE->aiSetLimitedZoneTrackYawMaxR(yaw_max);
    tinySE->aiSetLimitedZoneTrackPitchMinR(pitch_min);
    tinySE->aiSetLimitedZoneTrackPitchMaxR(pitch_max);

    /// zone track init pos, the init pos must be within the tracking range.
    Device::PresetPosInfo init_pos;
    init_pos.id = 0;
    init_pos.b_pitch = -1000;
    if (get_pos_from_gimbal) {
        Device::AiGimbalStateInfo state_info;
        dev->aiGetGimbalStateR(&state_info);
        init_pos.yaw = state_info.yaw_motor;
        init_pos.pitch = state_info.pitch_motor;
        init_pos.roll = state_info.roll_motor;
    }
    tinySE->aiSetLimitedZoneTrackInitPosR(&init_pos);

    /// zone track auto select target
    tinySE->aiSetLimitedZoneTrackAutoSelectR(true);

    /// Enable the auto tracking of gimbal after setting
    tinySE->aiSetZoneTrackGimbalEnabledR(true);
    /// Close the special led pattern before setting the zone tracking
    tinySE->cameraSetLedCtrlU(false);
}

int main(int argc, char **argv)
{
    cout << "Hello World" << endl;
    kDevs.clear();

    /// register device changed callback
    Devices::get().setDevChangedCallback(onDevChanged, nullptr);

    /// enable mdns scan for detect device by network
    Devices::get().setEnableMdnsScan(false);

    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    /// select the first device
    int deviceIndex = 0;
    string cmd;
    cout << "please input command('h' to get command info): " << endl;
    while (cin >> cmd)
    {
        if (cmd == "h")
        {
            cout << "==========================================" << endl;
            cout << "q:             quit!" << endl;
            cout << "p:             printf device info!" << endl;
            cout << "s:             select device!" << endl;
            cout << "1              set status callback!" << endl;
            cout << "2              set event notify callback!" << endl;
            cout << "3              wakeup or sleep!" << endl;
            cout << "4              control the gimbal to move to the specified angle!" << endl;
            cout << "5              control the gimbal to move by the specified speed!" << endl;
            cout << "6              set the boot initial position and zoom ratio and move to the preset position!" << endl;
            cout << "7              set the preset position and move to the preset positions!" << endl;
            cout << "8              set ai mode!" << endl;
            cout << "9              cancel ai mode!" << endl;
            cout << "10             set ai tracking type!" << endl;
            cout << "11             set the absolute zoom level!" << endl;
            cout << "12             set the absolute zoom level and speed!" << endl;
            cout << "13             set fov of the camera!" << endl;
            cout << "14             set media mode!" << endl;
            cout << "15             set hdr!" << endl;
            cout << "16             set face focus!" << endl;
            cout << "17             set the manual focus value!" << endl;
            cout << "18             set the white balance!" << endl;
            cout << "19             start or stop taking photos!" << endl;
            cout << "21             download file!" << endl;
            cout << "==========================================" << endl;
            cout << "please input command('h' to get command info): ";
            continue;
        }

        if (cmd == "q")
        { exit(0); }

        if (kDevs.empty())
        {
            cout << "No devices connected" << endl;
            cout << "please input command('h' to get command info): ";
            continue;
        }

        /// print device's info
        if (cmd == "p")
        {
            int index = 0;
            cout << "Current connected devices:" << endl;
            auto dev_list = Devices::get().getDevList();
            for (auto &item : dev_list)
            {
                cout << "---------------------------------------------------" << endl;
                cout << "Device SN: " << item->devSn() << endl;
                cout << "  index: " << index++ << endl;
                cout << "  deviceName: " << item->devName().c_str() << endl;
                cout << "  deviceVersion: " << item->devVersion().c_str() << endl;
#ifdef _WIN32
                if (item->devMode() == Device::DevModeUvc)
                {
                    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
                    cout << "  videoDevPath: " << converter.to_bytes(item->videoDevPath()).c_str() << endl;
                    cout << "  videoFriendlyName: " << converter.to_bytes(item->videoFriendlyName()).c_str() << endl;
                    cout << "  audioDevPath: " << converter.to_bytes(item->audioDevPath()).c_str() << endl;
                    cout << "  audioFriendlyName: " << converter.to_bytes(item->audioFriendlyName()).c_str() << endl;
                }
#endif
                std::string strProductType = getProductNameByType(item->productType());
                cout << "  product: " << strProductType << endl;
                /// network mode
                if (item->productType() == ObsbotProductType::ObsbotProdTailAir &&
                    item->devMode() == Device::DevModeNet)
                {
                    cout << "  deviceBluetoothMac: " << item->devBleMac().c_str() << endl;
                    cout << "  deviceWifiMode: " << item->devWifiMode().c_str() << endl;
                    if (item->devWifiMode() == "station")
                    {
                        cout << "  deviceWifiSsid: " << item->devWifiSsid().c_str() << endl;
                        cout << "  deviceWiredIp: " << item->devWiredIp().c_str() << endl;
                        cout << "  deviceWirelessIp: " << item->devWirelessIp().c_str() << endl;
                    }
                }
            }
            cout << "please input command('h' to get command info): ";
            continue;
        }

        /// select the first device
        dev = Devices::get().getDevBySn(kDevs[deviceIndex]);

        /// update selected device
        if (cmd == "s")
        {
            cout << "Input the index of device:";
            cin >> deviceIndex;
            if (deviceIndex < 0 || deviceIndex >= kDevs.size())
            {
                cout << "Invalid device index, valid range: 0 ~ " << kDevs.size() - 1 << endl;
                cout << "please input command('h' to get command info): ";
                continue;
            }
            dev = Devices::get().getDevBySn(kDevs[deviceIndex]);
            cout << "select the device: " << dev->devName().c_str() << endl;
            cout << "please input command('h' to get command info): ";
            continue;
        }

        /// control the device to do something
        int cmd_code = atoi(cmd.c_str());
        switch (cmd_code)
        {
            /// set status callback
        case 1:
        {
            dev->setDevStatusCallbackFunc(onDevStatusUpdated, nullptr);
            dev->enableDevStatusCallback(true);
            break;
        }
            /// set event notify callback, only for tail air
        case 2:
        {
            if (dev->productType() == ObsbotProdTailAir)
            {
                dev->setDevEventNotifyCallbackFunc(onDevEventNotify, nullptr);
            }
            break;
        }
            /// wakeup or sleep
        case 3:
        {
            dev->cameraSetDevRunStatusR(Device::DevStatusRun);
            break;
        }
            /// control the gimbal to move to the specified angle, only for tiny2 and tail air
        case 4:
        {
            if (dev->productType() == ObsbotProdTiny2 || dev->productType() == ObsbotProdTailAir)
            {
                dev->aiSetGimbalMotorAngleR(0.0f, -45.0f, 90.0f);
            }
            break;
        }
            /// control the gimbal to move by the specified speed, the gimbal will be stop if the speed is 0
        case 5:
        {
            dev->aiSetGimbalSpeedCtrlR(-45, 60, 60);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            dev->aiSetGimbalSpeedCtrlR(0, 0, 0);
            break;
        }
            /// set the boot initial position and zoom ratio and move to the preset position
        case 6:
        {
            Device::PresetPosInfo BootPosPresetInfo;
            BootPosPresetInfo.id = 0;
            std::string BootPosPresetPosName = "BootPresetInfoZero";
            memcpy(BootPosPresetInfo.name, BootPosPresetPosName.c_str(), BootPosPresetPosName.length());
            BootPosPresetInfo.name_len = BootPosPresetPosName.length();
            BootPosPresetInfo.zoom = 1.4;
            BootPosPresetInfo.yaw = 45.0f;
            BootPosPresetInfo.pitch = 0.0f;
            BootPosPresetInfo.roll = 90.0f;
            BootPosPresetInfo.roi_cx = 2.0;
            BootPosPresetInfo.roi_cy = 2.0;
            BootPosPresetInfo.roi_alpha = 2.0;
            dev->aiSetGimbalBootPosR(BootPosPresetInfo);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            dev->aiTrgGimbalBootPosR();
            break;
        }
            /// set the preset position and move to the preset position
        case 7:
        {
            Device::PresetPosInfo presetInfo;
            presetInfo.id = 0;
            std::string PresetPosName = "PresetInfoZero";
            memcpy(presetInfo.name, PresetPosName.c_str(), PresetPosName.length());
            presetInfo.name_len = PresetPosName.length();
            presetInfo.zoom = 1.6;
            presetInfo.yaw = 25.0f;
            presetInfo.pitch = 45.0f;
            presetInfo.roll = 60.0f;
            presetInfo.roi_cx = 2.0;
            presetInfo.roi_cy = 2.0;
            presetInfo.roi_alpha = 2.0;
            dev->aiAddGimbalPresetR(&presetInfo);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            dev->aiTrgGimbalPresetR(presetInfo.id);
        }
            /// set ai mode
        case 8:
        {
            if (dev->productType() == ObsbotProdTiny || dev->productType() == ObsbotProdTiny4k)
            {
                dev->aiSetTargetSelectR(true);
            }
            else if (dev->productType() == ObsbotProdTiny2)
            {
                dev->cameraSetAiModeU(Device::AiWorkModeHuman, Device::AiSubModeUpperBody);
            }
            else if (dev->productType() == ObsbotProdTailAir)
            {
                dev->aiSetAiTrackModeEnabledR(Device::AiTrackHumanNormal, true);
            }
            break;
        }
            /// cancel ai mode
        case 9:
        {
            if (dev->productType() == ObsbotProdTiny || dev->productType() == ObsbotProdTiny4k)
            {
                dev->aiSetTargetSelectR(false);
            }
            else if (dev->productType() == ObsbotProdTiny2)
            {
                dev->cameraSetAiModeU(Device::AiWorkModeNone);
            }
            else if (dev->productType() == ObsbotProdTailAir)
            {
                int ai_type = dev->cameraStatus().tail_air.ai_type;
                if (ai_type == 5)
                { dev->aiSetAiTrackModeEnabledR(Device::AiTrackGroup, false); }
                else
                { dev->aiSetAiTrackModeEnabledR(Device::AiTrackNormal, false); }
            }
            break;
        }
            /// set ai tracking type
        case 10:
        {
            dev->aiSetTrackingModeR(Device::AiVTrackStandard);
            break;
        }
            /// set the absolute zoom level
        case 11:
        {
            dev->cameraSetZoomAbsoluteR(1.5);
            break;
        }
            /// set the absolute zoom level and speed
        case 12:
        {
            dev->cameraSetZoomWithSpeedAbsoluteR(150, 6);
            break;
        }
            /// set fov of the camera
        case 13:
        {
            dev->cameraSetFovU(Device::FovType86);
            break;
        }
            /// set media mode, only for meet and meet4K
        case 14:
        {
            if (dev->productType() == ObsbotProdMeet || dev->productType() == ObsbotProdMeet4k)
            {
                dev->cameraSetMediaModeU(Device::MediaModeBackground);
                dev->cameraSetBgModeU(Device::MediaBgModeReplace);
            }
            break;
        }
            /// set hdr
        case 15:
        {
            dev->cameraSetWdrR(Device::DevWdrModeDol2TO1);
            break;
        }
            /// set face focus
        case 16:
        {
            dev->cameraSetFaceFocusR(true);
            break;
        }
            /// set the manual focus value
        case 17:
        {
            dev->cameraSetFocusAbsolute(50, false);
            break;
        }
            /// set the white balance
        case 18:
        {
            dev->cameraSetWhiteBalanceR(Device::DevWhiteBalanceAuto, 100);
            break;
        }
            /// start or stop taking photos, only for tail air
        case 19:
        {
            if (dev->productType() == ObsbotProdTailAir)
            {
                dev->cameraSetTakePhotosR(0, 0);
            }
            break;
        }
            /// download file
        case 21:
        {
            bool enable = false;
            dev->aiGetLimitedZoneTrackAutoSelectR(enable);
            if (dev->productType() == ObsbotProdMeet || dev->productType() == ObsbotProdMeet4k
                || dev->productType() == ObsbotProdTiny2)
            {
                std::string image_mini = "C:/obsbot/image";
                std::string image = "C:/obsbot/image";
                dev->setLocalResourcePath(image_mini, image, 0);
                dev->setFileDownloadCallback(onFileDownload, nullptr);
                dev->startFileDownloadAsync(Device::DownloadImage0);
            }
            break;
        }
            /// get zone track info for TinySE
        case 22:
        {
            auto dev_list = Devices::get().getDevList();
            for (auto dev : dev_list)
            {
                if (dev->productType() == ObsbotProdTinySE)
                {
                    getZoneTrackInfoForTinySE(dev->devSn());
                    break;
                }
            }
            break;
        }
            /// set zone track info for TinySE
        case 23:
        {
            auto dev_list = Devices::get().getDevList();
            for (auto dev : dev_list)
            {
                if (dev->productType() == ObsbotProdTinySE)
                {
                    setZoneTrackInfoForTinySE(dev->devSn());
                    break;
                }
            }
            break;
        }

        default:;
            cout << "unknown command, please input 'h' to get command info" << endl;
        }
        cout << "please input command('h' to get command info): ";
    }
    return 0;
}
