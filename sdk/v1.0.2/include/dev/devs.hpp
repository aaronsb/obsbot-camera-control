#ifndef OBSBOT_DEVS_HPP
#define OBSBOT_DEVS_HPP

#include <functional>
#include <list>
#include <memory>

#include "util/comm.hpp"
#include "dev.hpp"


class DevicesPrivate;

class DevUpgradePrivate;

class DEV_EXPORT Devices
{
public:
    enum DevicesState
    {
        // Bluetooth
        BluetoothOn = 0,
        BluetoothOff,
        BluetoothDetectFailed,
        BluetoothDetectComplete,
        BluetoothUnknown,

        // Wifi
        WifiOk = 0,
        WifiTimeout,                                // receive response timeout
        WifiBluetoothOccupied,
        WifiConnectBluetoothFailed,
        WifiSetModeFailed,
        WifiGetHistoryFailed,
        WifiTrgScanFailed,                          // start scan failed
        WifiGetScanResultFailed,                    // failed to get scan wifi results
        WifiStopWifiScan,        					// stop wifi scan
        WifiSetConnectedFailed,                     // connect to route failed
        WifiSetPasswordError,                       // error password
        WifiGetIpFailed,                            // get ip from route failed
        WifiUpdArpFailed,                           // update device arp failed
        WifiSetCountryCodeError,                    // country code error in ap mode
        WifiGetApInfoFailed,                        // failed to get ap info
        WifiUnknown,
    };

    // For tail air,tail2
    class BluetoothInfo
    {
    public:
        int32_t err_code;                           // refer to DevicesState

        std::string identifier;
        std::string address;
        int16_t rssi;
        int16_t mtu;
        bool sleep_flag;                            // true: device is in sleep state
        int32_t battery_level;
        std::string dev_name;
        union
        {
            struct
            {
                uint8_t push_stream: 1;             // 1: device is pushing stream
                uint8_t app_connected: 1;           // 1: app is connected
                uint8_t remote_connected: 1;        // 1: remote controller is connected
                uint8_t tablet_connected: 1;        // 1: tablet is connected
                uint8_t swivel_base_connected: 1;   // swivel base connected
                uint8_t bat_overhead: 1;            // battery over headed
                uint8_t charging_flag: 1;           // battery is charging
                uint8_t fast_search: 1;             //
                uint8_t adapter_flag: 1;            // adapter flag
                uint8_t pc_connected: 1;            // 1: pc connected
                uint8_t reserve: 6;                 // reserve
            };
            uint16_t status;
        };
        ObsbotProductType product_type;
    };

	enum WifiCfgSteps {
		WifiCfgIdle,
		WifiCfgConnectBluetooth,
		WifiCfgSetMode,
		WifiCfgGetHistoryRecords,
        WifiCfgGetBleStatus,
		WifiCfgTrgScan,
		WifiCfgGetScanResults,
		WifiCfgSetConnect,
		WifiCfgGetIp,
		WifiCfgUpdateArp, // The last step in station mode

        WifiCfgSetCountryCode,
        WifiCfgGetApStatus,                     // The last step in ap mode
    };

    class WifiFoundInfo
    {
    public:
        int32_t err_code;                       // refer to DevicesState
        int32_t info_type;                      // reter to WifiCfgSteps

        // History connect record
        std::string if_name;
        uint32_t ip_proto;                      // 0-DHCP 1-STATIC
        uint32_t ipv4;
        uint32_t netmask;
        uint32_t gateway;
        struct record
        {
            int32_t priority = 0;               // higher value higher priority
            std::string ssid;                   // utf-8 binary, max 32 byte
            std::string password;               // max 32 byte
        } records[5];

		// Current scan result
		uint8_t signal_score; // 0-100 higher value better signal quality
		int32_t index;        // index
		std::string ssid;     // utf-8 binary, maybe empty
		int32_t channel;      // wifi channel
		int32_t freq;         // wifi frequency
        uint8_t pwd;          // wifi support 64 or not
	};

    class WifiCfgInfo : public Device::WifiInfo
    {
    public:
        int32_t err_code{};          // refer to DevicesState
        int32_t info_type{};         // reter to WifiCfgSteps
        std::string ble_mac_address; // device bluetooth mac address
    };

    enum DevNetType
    {
        DevNetAP = 0,
        DevNetSTA,
    };

    enum DevWakeUpState
    {
        WakeUpSuccess = 0,
        WakeUpTimeout,
        WakeUpFailed,
    };

    /**
     * @brief This callback function is triggered when a plug-in or unplug event of a device is detected. Register by
     *        @setDevChangedCallback function, only one function can be registered.
     * @param [in] std::string   The SN of the current device is a 14-digit string used to uniquely identify the device.
     * @param [in] bool          true for device plug-in event; false for device unplug event
     * @param [in] void *        Custom parameters passed in by the user
     */
    typedef std::function<void(std::string, bool, void *)> devChangedCallback;

    typedef std::function<void(const BluetoothInfo &, void *)> btDevFoundCallback;

    typedef std::function<void(const WifiFoundInfo &, void *)> wifiInfoFoundCallback;

    typedef std::function<void(const int, const std::string &, void *)> devWakeUpCallback;

    Devices(const Devices &) = delete;

    Devices &operator=(const Devices &) = delete;

    /**
     * @brief  Get the device management object, which is globally unique.
     * @return  Returns a reference to the device management object
     */
    static Devices &get();

    /**
     * @brief  Stop the device detection task and release the resources occupied by the device management object.
     */
    void close();

    /**
     * @brief  Register device detection event callback function.
     * @param  [in] callback   Callback function to be registered.
     * @param  [in] param      User-defined parameter
     */
    void setDevChangedCallback(devChangedCallback callback, void *param);

    /**
     * @brief  set network device heartbeat interval, default 3s.
     * @param  [in] interval   heartbeat interval, use milliseconds.
     */
    void setNetDevHeartbeatInterval(int interval);

    /**
     * @brief  Get the total number of valid devices detected in the current system.
     * @return  The number of devices currently exist in the system.
     */
    size_t getDevNum();

    /**
     * @brief   Check whether the specified device exists in the system.
     * @param   [in] uuid   Device's uuid.
     * @retval  Returns true if the specified device exists, otherwise returns false.
     */
    bool containDev(Device::DevUuid &uuid);

    /**
     * @brief  Get the specified device according to the device name.
     * @param  [in] dev_name   Device's name.
     * @retval  The device with the specified name, or NULL if it does not exist.
     */
    std::shared_ptr<Device> getDevByName(const std::string &dev_name);

    /**
     * @brief  Get the specified device according to the device uuid.
     * @param  [in] uuid   Device's uuid.
     * @return  The device with the specified uuid, or NULL if it does not exist.
     */
    std::shared_ptr<Device> getDevByUuid(Device::DevUuid &uuid);

    /**
     * @brief  Get the specified device according to the device SN.
     * @param  [in] dev_sn   Device's SN.
     * @return  The device with the specified SN, or NULL if it does not exist.
     */
    std::shared_ptr<Device> getDevBySn(const std::string &dev_sn);

    /**
     * @brief  Get all valid devices in the current system.
     * @return  A list of all valid devices currently in the system.
     */
    std::list<std::shared_ptr<Device>> getDevList();

    /**
     * @brief  Set the white list for tail when scanning devices through network.
     * @param  [in]  white_list   A list of bluetooth mac address.
     */
    void setTailAirWhiteList(std::list<std::string> white_list);

    /**
     * @brief  Start net scanning immediately.
     * @return  RM_RET_OK for success
     *          RM_RET_ERR for failed (Scanning is in progress, try again later, eg. 1 seconds later).
     */
    int32_t startNetworkScanImmediately();

    /**
     * @brief  Enable the device discovery over mdns, used for tail air.
     * @param  [in] enabled   Tru->Enable device discovery over mdns.
     */
    void setEnableMdnsScan(bool enabled);

private:

    DECLARE_PRIVATE(Devices)

    DevicesPrivate *d_ptr;

    explicit Devices();

    ~Devices();

    void setUgSn(const std::string &sn);

    friend class DevUpgradePrivate;
};

#endif // OBSBOT_DEVS_HPP
