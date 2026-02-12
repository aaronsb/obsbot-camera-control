#ifndef OBSBOT_DEV_HPP
#define OBSBOT_DEV_HPP

#include <utility>
#include <array>
#include <string>
#include <functional>
#include <vector>
#include <list>

#include "util/comm.hpp"

#ifdef __APPLE__

#include <IOKit/IOTypes.h>

#endif


#define UVC_DEV_CAM_STATUS_REFRESH_PERIOD (100)
#define FAST_UVC_DEV_CAM_STATUS_REFRESH_PERIOD (25)

// UUID length
#define DEV_UUID_SIZE 24

#define RM_RET_OK       (0)
#define RM_RET_ERR      (-1)

#define DEV_UG_NOT_NEEDED   0
#define DEV_UG_NEEDED       1
#define DEV_UG_CHECK_ERR    2

/// transferred file type, used in meet, meet4k and tiny2.
#define RESOURCE_TYPE_NONE  0
#define RESOURCE_TYPE_IMAGE 1           /// Image
#define RESOURCE_TYPE_VIDEO 2           /// Video

#define TINY_OFFSET(member) ((int64_t)&(((Device::CameraStatus *)0)->tiny.member))
#define TAIL_OFFSET(member) ((int64_t)&(((Device::CameraStatus *)0)->tail_air.member))

/// error type during the file transfer.
enum ResDownloadState
{
    kResNameErr = -4,              // file name error
    kResTypeErr = -3,              // file type error
    kResDownloadErr = -2,          // file download error
    kResNotExist = -1,             // file not exist in dev
    kResDownloadSuccess = 0,       // file download success
    kResSameWithLocal = 1,         // file in dev is the same as local
};

/// product type.
enum ObsbotProductType
{
    ObsbotProdTiny,
    ObsbotProdTiny4k,
    ObsbotProdTiny2,
    ObsbotProdTiny2Lite,
    ObsbotProdTailAir,              ///
    ObsbotProdMeet,
    ObsbotProdMeet4k,
    ObsbotProdMe,
    ObsbotProdHDMIBox,              /// uvc to hdmi
    ObsbotProdNDIBox,
    ObsbotProdMeet2,
    ObsbotProdTail2,
    ObsbotProdTinySE,
    ObsbotProdMeetSE,
    ObsbotProdTail2S = 16,
    ObsbotProdButt,
};

/// progress status indication
enum DevProgressType
{
    kErrCodeSuccess = 100,          /// 100 -> process finished successfully
    kErrCodeError = -1,             /// -1  -> an irreversible error occurred during the process
    kErrCodeWarn = -2,              /// -2  -> a warning occurred during the process, and can continue after the \
                                    ///        warning is eliminated
    kErrCodeFailure = -3,           /// -3  -> process finished failed
};

/// video format
enum class RmVideoFormat
{
    Any,
    Unknown,

    /// raw formats
    ARGB = 100,
    XRGB,
    RGB24,

    /// planar YUV formats
    I420 = 200,
    NV12,
    YV12,
    Y800,
    P010,

    /// packed YUV formats
    YVYU = 300,
    YUY2,
    UYVY,
    HDYC,

    /// encoded formats
    MJPEG = 400,
    H264,
    HEVC,
};

class DevicePrivate;

class DevUpgradePrivate;

class DevicesPrivate;

class DeviceId;

class DEV_EXPORT Device
{
public:
    /**
     * @brief  When the resource (image or video) is downloaded from the device, the callback function is used to
     *         asynchronously notify the download result.
     *         Only for meet series and tiny2.
     * @param  [in] param       User-defined parameter.
     * @param  [in] file_type   The downloaded file type, refer to FileType.
     * @param  [in] result      1    File in the device is the same with the local cache, no need to download.
     *                          0    File downloaded successfully.
     *                          -1   File does not exist in the device.
     *                          -2   Download failed.
     */
    typedef std::function<void(void *param, uint32_t file_type, int32_t result, void *rev_param)> FileDownloadCallback;

    /**
     * @brief  When the resource image is uploaded to the device, the callback function is used to asynchronously
     *         notify the download progress and result.
     *         Only for meet series and tiny2.
     * @param  param    User-defined parameter.
     * @param  result   Download progress or result, refer to DevProgressType.
     *                  0~100    Current download progress.
     *                  Others   Error occurred, refer to DevProgressType.
     */
    typedef std::function<void(void *param, int32_t result)> FileUploadCallback;

    /**
     * @brief  This callback function is used to periodically push the current device status to the outside. The
     *         push cycle is about every two or three seconds.
     * @param  param   User-defined parameter.
     * @param  data    The device status info, refer to CameraStatus.
     */
    typedef std::function<void(void *param, const void *data)> DevStatusCallback;

    /**
     * @brief  This callback function is used to periodically push the
     *         current device status to the outside. The push cycle is
     *         about every two or three seconds, fast status.
     * @param  param   User-defined parameter.
     * @param  data    The device status info, refer to CameraStatus.
     */
    typedef std::function<void(void *param, const void *data, const std::string &dev_sn)> FastDevStatusCallback;

    /**
     * @brief  This callback function is used to obtain the range parameters
     *         related to the device system, peripherals, and media.
     * @param  param   User-defined parameter.
     * @param  data    The device status info, refer to DevConfigRangeInfo.
     */
    typedef std::function<void(void *param, const void *data)> DevConfigRangeCallback;

    /// Device uuid
    using DevUuid = std::array<uint8_t, DEV_UUID_SIZE>;

    /**
     * @brief  This callback function is used to asynchronously process the received messages from the device.
     * @param  param       User-defined parameter.
     * @param  rcvd_data   Received message from the device, message type is related to the specific calling function.
     *                     The first data (u8) is the data length (>= 0) or error code(< 0).
     */
    typedef std::function<void(void *param, const void *rcvd_data)> RxDataCallback;

    /**
     * @brief  This callback function is used to asynchronously process the
     *         received messages with special length from the device.
     * @param  param       User-defined parameter.
     * @param  rcvd_data   Received message from the device, message type
     *                     is related to the specific calling function.
     *                     The first data (u8) is the data length (>= 0) or
     *                     error code(< 0).
     * @param  len         The length of Received message
     */
    typedef std::function<void(void *param, const void *rcvd_data, int32_t len)> RxDataWithLenCallback;
	
    /**
     * @brief  When receiving a event from the device, the callback will be called.
     *         Only for tail air.
     * @param  [in] param       User-defined parameter.
     * @param  [in] event_type  The event type, refer to RmEventType.
     * @param  [in] result      The value is associated with a specified event.
     */
    typedef std::function<void(void *param, int32_t event_type, const void *result)> DevEventNotifyCallback;

    /**
     * @brief  Settable parameter range, step value, default value
     */
    class UvcParamRange
    {
    public:
        long min_;                  // the minimum value
        long max_;                  // the maximum value
        long step_;                 // the smallest step between settings
        long default_;              // the default value
        long caps_flags_;           // not used at present

        bool valid_;                // for internal use

        UvcParamRange() : min_(0), max_(0), step_(0), default_(0), caps_flags_(0), valid_(false)
        {}
    };

    /// device event notify, for tail air
    enum RmEventType
    {
        kEvtErrGimbalComm = 0,                  /// gimbal communication error
        kEvtErrAiComm,                          /// ai communication error
        kEvtErrBatComm,                         /// battery communication error
        kEvtErrLensComm,                        /// lens communication error
        kEvtErrSensor,                          /// sensor error, eg. no image
        kEvtErrMedia,                           /// camera media error, eg. no image
        kEvtErrTof,                             /// TOF connection error
        kEvtErrBluetooth,                       /// bluetooth error
        kEvtErrDevTempHigh,                     /// Device temperature is too high
        kEvtErrBatLowCapacity,                  /// low battery capacity < 3%
        kEvtErrSdFormatting,                    /// sdcard is formatting
        kEvtErrSdFileSystem,                    /// sdcard filesystem error
        kEvtErrSdMount,                         /// sdcard mount failed
        kEvtErrSdNotSupport,                    /// sdcard filesystem is not supported
        kEvtErrSdInitializing,                  /// sdcard is initializing
        kEvtErrSdWriteProtect,                  /// sdcard is write protected
        kEvtErrSdNoPartition,                   /// sdcard is no partition

        kEvtWarnSdWriteSlow = 1000,             /// sdcard write speed is slow
        kEvtWarnFileFixFailed,                  /// file fixed failed
        kEvtWarnSdLowSpeed,                     /// sdcard is low speed
        kEvtWarnSdcardNotExist,                 /// sdcard not present
        kEvtWarnSdcardFull,                     /// sdcard is full
        kEvtWarnBatLowCapacity10,               /// low battery capacity < 10%
        kEvtWarnBatLowCapacity5,                /// low battery capacity < 5%
        kEvtWarnStreamConn,                     /// live stream connect failed
        kEvtWarnNetException,                   /// network exception
        kEvtWarnStreamAppExit,                  /// app exit in live stream
        kEvtWarnSdCardFormatFail,               /// sd card format fail
        kEvtWarnNoAudioInput,                   /// no audio input
        kEvtWarnMtpStreamFailed,                /// mtp start stream failed
        kEvtInfoMicPlugin = 2000,               /// mic in plugin
        kEvtInfoMicUnplug,                      /// mic in unplug
        kEvtInfoLinePlugin,                     /// line in plugin
        kEvtInfoLineUnplug,                     /// line in unplug
        kEvtInfoSwivelConn,                     /// swivel base connected
        kEvtInfoRemoteConn,                     /// remote controller connected
        kEvtInfoMonitorConn,                    /// microphone connected
        kEvtInfoTargetLoss,                     /// target loss
        kEvtInfoNewMediaFile,                   /// new video or photo file is generated, refer to  /// CameraFileNotify.
        kEvtInfoApStatus,                       /// ap status changed
        kEvtInfoSdCardFormatSuccess,            /// sd card format success
        kEvtInfoBatCharging,                    /// battery is charging
        kEvtInfoSdReady,                        /// sd card is ready
        kEvtInfoDevTemp,                        /// Device temperature is normal
        kEvtInfoGimbalComm,                     /// gimbal communication error
        kEvtInfoAiComm,                         /// ai communication info
        kEvtInfoBatComm,                        /// battery communication info
        kEvtInfoLensComm,                       /// lens communication info
        kEvtInfoSensor,                         /// sensor info
        kEvtInfoMedia,                          /// camera media info
        kEvtInfoTof,                            /// TOF connection info
        kEvtInfoBluetooth,                      /// bluetooth info
        kEvtInfoHDRWith60Fps,                   /// for tiny2, fps is 60 when HDR is enabled
        kEvtInfoSyncBootState,                  /// the presets are synchronized to the initial state
        kEvtInfoHDRWithPortrait,                /// the screen mode is portrait when hdr is enabled
        kEvtInfoBluetoothMatchFailed,           /// bluetooth match failed
        kEvtInfoHDRWith100Fps,                  /// for meetSE、tinySE, fps is larger than 100 when HDR is enabled
        kEvtInfoMtpStreamResume,                /// mtp start stream resume
        kEvtTipsBatState = 3000,                /// battery state info
        kEvtTipsNetStrength,                    /// network strength
        kEvtTipsMicIntensity,                   /// microphone intensity
        kEvtTipsNameChanged,                    /// device name changed
        kEvtTipsTransStart,                     /// device file transport start
        kEvtTipsTransEnd,                       /// device file transport end
        kEvtTipsWifiChanged,                    /// device wifi changed
        kEvtTipsUvcStatus,                      /// device uvc switch status
        kEvtTipsNdiStatus,                      /// device ndi switch status
        kEvtTipsRtspStatus,                     /// device rtsp switch status
        kEvtTipsSrtStatus,                      /// device srt switch status
        kEvtTipsRemoteUpStatus,                 /// remote controller upgrade status
        kEvtTipsAdapterPower,                   /// adapter power status
    };

    /// Anti-Flicker enumerate
    enum PowerLineFreqType
    {
        PowerLineFreqOff = 0,
        PowerLineFreq50,
        PowerLineFreq60,
        PowerLineFreqAuto,
    };

    /// Device mode
    enum DevMode
    {
        DevModeUvc,                             /// normal uvc mode, default mode
        DevModeNet,                             /// network mode
        DevModeMtp,                             /// MTP mode
        DevModeBle,                             /// bluetooth mode
    };

    /// Communication error type
    enum ErrorType
    {
        CommErrorMode = -7,                     /// mode error (current DevMode does not support this function)
        CommErrorInited = -6,                   /// initialization error
        CommErrorLength = -5,                   /// frame length error
        CommErrorBusy = -4,                     /// device is busy
        CommErrorTimeout = -3,                  /// timeout error
        CommErrorResp = -2,                     /// error response from device
        CommErrorOther = -1,                    /// other unknown error
        CommErrorNone = 0,                      /// no error
    };

    /// Device running state: run / sleep / privacy
    enum DevStatus
    {
        DevStatusErr = -1,
        DevStatusRun = 1,                       /// normal mode
        DevStatusSleep = 3,                     /// sleep mode
        DevStatusPrivacy = 4,                   /// in privacy mode, stream will not be fetched from the device.
    };

    /// Device system type
    enum DevSysType
    {
        DevUnknownSys,                          /// for devices with old firmware, cannot get system info
        DevMainSys = 1,                         /// in normal system
        DevUgSys = 2,                           /// in upgrade system, internal use only
    };

    /// Get response from the device
    enum GetMethod
    {
        Block,                                  /// get response synchronously
        NonBlock,                               /// get response asynchronously
    };

    /// internal use only
    enum ImageFormat
    {
        ImgFormatMjpeg = 0,
        ImgFormatYpCbCr = 1,
    };

    /// Device fov type
    enum FovType
    {
        FovType86 = 0,                          /// field of view 86°, wide view
        FovType78 = 1,                          /// field of view 78°, medium view
        FovType65 = 2,                          /// field of view 65°, narrow view
        FovTypeNull = 3,
        FovTypeDefault = FovType86,
    };

    /// Camera media mode
    /// Only for meet series
    enum MediaMode
    {
        MediaModeNormal = 0,                    /// normal mode
        MediaModeBackground = 1,                /// virtual background mode
        MediaModeAutoFrame = 2,                 /// auto framing mode
        MediaModeIllegal = 255,                 /// unknown mode
    };

    /// Camera virtual background mode
    /// Only for meet series
    enum MediaBgMode
    {
        MediaBgModeDisable = 0,                 /// none
        MediaBgModeColor = 1,                   /// virtual background: green mode
        MediaBgModeReplace = 17,                /// virtual background: replace mode
        MediaBgModeBlur = 18,                   /// virtual background: blur mode
    };

    /// Camera virtual background mode, green mode
    /// Only for meet series
    enum MediaBgModeColorType
    {
        MediaBgColorDisable = -2,
        MediaBgColorNull = -1,
        MediaBgColorBlue = 0,                   /// background color: blue
        MediaBgColorGreen = 1,                  /// background color: green
        MediaBgColorRed = 2,                    /// background color: red
        MediaBgColorBlack = 3,                  /// background color: black
        MediaBgColorWhite = 4,                  /// background color: white
    };

    /// Camera auto framing mode
    /// Only for meet series
    enum AutoFramingType
    {
        AutoFrmGroup = 0,                       /// group mode
        AutoFrmSingle = 1,                      /// single mode
        AutoFrmCloseUp = 0,                     /// single mode: close up mode
        AutoFrmUpperBody = 1,                   /// single mode: upper body mode
        AutoFrmNull = -1,
    };

    /// Smart tracking mode
    /// Only for tiny series
    enum AiVerticalTrackType
    {
        AiVTrackStandard = 0,                   /// standard mode
        AiVTrackHeadroom,                       /// headroom mode
        AiVTrackMotion,                         /// motion mode
        AiVTrackButt,
    };

    enum AiTrackSpeedType
    {
        AiTrackSpeedLazy = 0,
        AiTrackSpeedSlow,
        AiTrackSpeedStandard,
        AiTrackSpeedFast,
        AiTrackSpeedCrazy,
        AiTrackSpeedAuto,
        AiTrackSpeedButt,
    };

    /// AI smart tracking mode
    /// For tiny2 series, tinySE.
    enum AiWorkModeType
    {
        AiWorkModeNone = 0,                     /// Normal mode, AI smart tracking off
        AiWorkModeGroup,                        /// Multi-person tracking mode
        AiWorkModeHuman,                        /// Single-person tracking mode
        AiWorkModeHand,                         /// Hand tracking mode
        AiWorkModeWhiteBoard,                   /// White board mode
        AiWorkModeDesk,                         /// Desk mode
        AiWorkModeSwitching,                    /// Mode switching is ongoing
        AiWorkModeButt,
    };

    /// AI smart tracking mode
    /// For tail air.
    enum AiTrackModeType
    {
        AiTrackNormal = 0,
        AiTrackHumanNormal = 1,
        AiTrackHumanFullBody = 2,
        AiTrackHumanHalfBody = 3,
        AiTrackHumanCloseUp = 4,
        AiTrackHumanAutoView = 5,
        AiTrackAnimalNormal = 10,
        AiTrackAnimalCloseUp = 11,
        AiTrackAnimalAutoView = 12,
        AiTrackGotNormal = 20,
        AiTrackGotCloseUp = 21,
        AiTrackGotAutoView = 22,
        AiTrackGroup = 30,

        AiTrackPrivateFlag1 = 65530,
    };

    /// AI main mode
    /// Only for tail2 and later products
    enum AiMainModeType
    {
        AiMainModeTypeNormal = 0,
        AiMainModeTypeGroup,
        AiMainModeTypeTrack,
        AiMainModeTypeGestureTrack,
        AiMainModeTypeDesk,
        AiMainModeTypeWhiteBoard,
    };

    /// AI track sub mode, when main mode is AiMainModeTypeTrack
    /// Only for tail2 and later products
    enum AiTrackSubModeType
    {
        /// Human track
        AiTrackSubModeTypeHumanNormal = 0,
        AiTrackSubModeTypeHumanFullBody,
        AiTrackSubModeTypeHumanHalfBody,
        AiTrackSubModeTypeHumanCloseUp,
        AiTrackSubModeTypeHumanCustomAutoZoom,
        AiTrackSubModeTypeHumanHeadless,
        AiTrackSubModeTypeHumanLowerBody,
        /// Animal track
        AiTrackSubModeTypeAnimalNormal = 10,
        AiTrackSubModeTypeAnimalCloseUp = 13,
        AiTrackSubModeTypeAnimalCustomAutoZoom,
        /// Common object track
        AiTrackSubModeTypeCommonNormal = 20,
        AiTrackSubModeTypeCommonCloseUp = 23,
        AiTrackSubModeTypeCommonCustomAutoZoom,
    };

    /// Voice control language type
    enum AudioCtrlLanguageType
    {
        AudioCtrlLangZhCN = 0,                  /// Chinese
        AudioCtrlLangEnUS,                      /// English
    };

    /// In Voice control, each bit in uint8 is used to indicate enabled state of a voice control command.
    /// 0->off / 1->on
    /// For tiny2.
    enum AudioCtrlCmdType
    {
        AudioCtrlHiTiny = 0,                    /// Voice control command: Hi Tiny
        AudioCtrlSleepTiny,                     /// Voice control command: Sleep Tiny
        AudioCtrlTrack,                         /// Voice control command: Track Me
        AudioCtrlUnlock,                        /// Voice control command: Unlock Me
        AudioCtrlZoomIn,                        /// Voice control command: Zoom In Close
        AudioCtrlZoomOut,                       /// Voice control command: Zoom Out Further
        AudioCtrlPreset,                        /// Voice control command: Preset Position 1/2/3
        AudioCtrlZoomFactor = 100,              /// Used to set zoom factor
        AudioCtrlLanguage,                      /// User to set voice control language
    };

    /// Hand tracking type
    /// For tiny2 series, tinySE.
    enum AiHandTrackType
    {
        AiHandTrackRight,                       /// Track right hand
        AiHandTrackLeft,                        /// Track left hand
        AiHandTrackButt,
    };

    /// AI sub mode for AiWorkModeHuman
    /// For tiny2 series, tinySE.
    enum AiSubModeType
    {
        AiSubModeNormal = 0,                    /// Normal tracking mode
        AiSubModeUpperBody,                     /// Upper body tracking mode
        AiSubModeCloseUp,                       /// Close up mode
        AiSubModeHeadHide,                      /// Head hide mode
        AiSubModeLowerBody,                     /// Lower body tracking mode
        AiSubModeButt,
    };

    /// Image transfer type
    /// For meet series: background image
    /// For tiny2 series, tinySE: background image or video in sleep mode
    static const int32_t kResMaxNum = 4;
    enum FileType
    {
        DownloadImageMini0 = 1 << 0,            /// download thumbnail 1 2 3 4
        DownloadImageMini1 = 1 << 1,
        DownloadImageMini2 = 1 << 2,
        DownloadImageMini3 = 1 << 3,
        DownloadImage0 = 1 << 4,                /// download original image 1 2 3 4
        DownloadImage1 = 1 << 5,
        DownloadImage2 = 1 << 6,
        DownloadImage3 = 1 << 7,
        UploadImage0 = 1 << 8,                  /// upload original image 1 2 3 4
        UploadImage1 = 1 << 9,
        UploadImage2 = 1 << 10,
        UploadImage3 = 1 << 11,
        DownloadVideo0 = 1 << 12,                /// download video 1 2 3 4
        DownloadVideo1 = 1 << 13,
        DownloadVideo2 = 1 << 14,
        DownloadVideo3 = 1 << 15,
        UploadVideo0 = 1 << 16,                 /// upload video 1 2 3 4
        UploadVideo1 = 1 << 17,
        UploadVideo2 = 1 << 18,
        UploadVideo3 = 1 << 19,
        DownloadLog = 1 << 20,                  /// download log
    };

    /// Device record status.
    /// For tail air.
    enum DevRecordStatus
    {
        DevRecordStatusIdle = 0,
        DevRecordStatusStarting,
        DevRecordStatusRunning,
        DevRecordStatusStopping,
    };

    /// Device live stream status.
    /// For tail air.
    enum DevLiveStreamStatus
    {
        DevLiveStreamStatusNotStart = 0,                /// The live stream is not started.
        DevLiveStreamStatusBroadcasting,                /// The live stream is broadcasting.
        DevLiveStreamStatusPreparing,                   /// The live stream is preparing.
        DevLiveStreamStatusFailed,                      /// The live stream started failed.
        DevLiveStreamStatusRetrying,                    /// The live stream is Retrying.
    };

    typedef struct
    {
        int32_t id;                             /// id of preset position
        float roll;                             /// degree
        float pitch;
        float yaw;
        float zoom;                             /// zoom value, 1.0~2.0(or 1.0~4.0)
        float b_pitch;                          /// for tiny2 series, tinySE, not used at present
        int32_t name_len;
        char name[64];

        /// for tail air
        float roi_cx;
        float roi_cy;
        float roi_alpha;
    } PresetPosInfo;

    typedef struct
    {
        float roll_euler;                     /// roll euler angle
        float pitch_euler;                    /// pitch euler angle
        float yaw_euler;                      /// yaw euler angle
        float roll_motor;                     /// roll motor angle
        float pitch_motor;                    /// pitch motor angle
        float yaw_motor;                      /// yaw motor angle
        float roll_v;                         /// roll angular velocity
        float pitch_v;                        /// pitch angular velocity
        float yaw_v;                          /// yaw angular velocity
    } AiGimbalStateInfo;

    typedef struct
    {
        int32_t len;
        union
        {
            uint8_t data_uint8[64];
            int8_t data_int8[64];
            uint16_t data_uint16[32];
            int16_t data_int16[32];
            int32_t data_int32[16];
            uint32_t data_uint32[16];
        };
    } DevDataArray;

    typedef struct
    {
        int32_t band_mode;                      /// ap: 0->auto select; 1->2.4G; 2->5G / sta: not used
        std::string if_name;                    /// ap: name / sta: name
        uint8_t ip_proto;                       /// sta 0->DHCP 1->STATIC
        uint32_t ipv4;                          /// ap: ipv4 / sta: ipv4
        uint32_t netmask;                       /// ap: netmask / sta: netmask
        uint32_t channel;                       /// ap: channel / sta: not used
        std::string ssid;                       /// ap: ssid / sta: ssid
        std::string password;                   /// ap: password / sta: not used
        uint8_t signal_score;                   /// ap: not used / sta: 0-100 higher value better signal quality
    } WifiInfo;

    /// AI state definition
    /// tiny, tiny4k, tiny2 series, tinySE, tail series.
    typedef struct
    {
        bool gesture_target;                    /// gesture target select: 0->off; 1->on
        bool gesture_zoom;                      /// gesture zoom: 0->off; 1->on
        bool gesture_dynamic_zoom;              /// (tiny2 series, tinySE, tail air, tail2) gesture dynamic zoom: 0->off; 1->on
        bool gesture_record;                    /// (tail air, tail2) gesture record
        bool gesture_mirror;                    /// (tiny2 series, tinySE, tail air, tail2) gesture direction mirror
        bool gesture_snapshot;			/// (tail2) gesture snapshot
        bool gesture_rolling;			/// (tail2) gesture rolling
        float gesture_zoom_factor;              /// zoom factor for gesture zoom, for tiny and meet is 1.0~2.0  \
                                                /// for tiny4k, meet4k, tiny2, tail air is 1.0~4.0
                                                /// for tail2 is 1.0-20.0

        uint8_t yaw_reverse;                    /// (tiny2 series, tinySE, tail air, tail2) gimbal yaw control: direction mirror
        uint8_t presets_num;			/// (tail2) presets number
        AiHandTrackType hand_track_type;        /// (tiny2 series, tinySE) refer to AiHandTrackType
        int32_t ai_zone_track;                  /// (tiny2 series, tinySE) 0->standard, 1->region tracking
        AiVerticalTrackType v_track_landscape;  /// (FrmVer0, tiny2 series, tinySE, meet2, meetSE) tracking mode in landscape mode, refer to AiVerticalTrackType
        AiVerticalTrackType v_track_portrait;   /// (FrmVer0) tracking mode in portrait mode, refer to AiVerticalTrackType
        AiTrackModeType main_mode;              /// (tail air) refer to AiTrackModeType
        AiTrackSpeedType speed_mode;            /// (tail air) refer to AiTrackSpeedType
        AiMainModeType ai_main_mode;		/// (tail2) refer to AiMainModeType
        AiTrackSubModeType ai_track_sub_mode;	/// (tail2) refer to AiTrackSubModeType
    } AiStatus;

    /// AI hand tracking zone info
    /// For tiny2 series, tinySE
    typedef struct
    {
        float yaw_min;                          /// The min degree of yaw axis
        float yaw_max;                          /// The max degree of yaw axis
        float pitch_min;                        /// The min degree of pitch axis
        float pitch_max;                        /// The max degree of pitch axis
        int32_t view_id;                        /// Index, -1 if not set yet.
        AiHandTrackType hand_type;              /// Track left or right hand, refer to AiHandTrackType
    } AiHandTrackStateInfo;

    /// Notification info when new media file is generated
    typedef struct
    {
        int32_t storage_midia_type;             /// 0->sd card; 1->emmc; 2->usb flash; 3->ssd
        int32_t storage_index;                  /// storage media index
        int32_t file_type;                      /// 0->unknown; 1->video; 2->photo; 3->capture
        bool is_dcf_file;
        bool is_image;                          /// true->photo; false->video
        std::string file_path;                  /// file path, prefix if ignored. \
                                                /// eg. /app/sd/DCIM/100MEDIA/VIDN0001.MP4->100MEDIA/VIDN0001.MP4
    } CameraFileNotify;

#pragma pack(1)
    /// Camera status definition
    /// Devices with old firmwares may not support some of these states
    typedef union
    {
        /// tiny, tiny4k, tiny2 series, tinySE, meet2, meetSE ////////////////////////////////////////
        struct
        {
            union
            {
                uint8_t ai_target;              /// non-zero means the target is selected
                uint8_t length;                 /// For tiny2, total length of this union
            };
            uint8_t rvd1;                       /// not used
            uint8_t rvd2;                       /// not used
            uint8_t anti_flicker;               /// refer to PowerLineFreqType
            uint16_t zoom_ratio;                /// zoom ratio, 0~100
            uint8_t hdr;                        /// hdr: 0->off, 1->on
            uint8_t face_ae;                    /// face auto exposure: 0->off, 1->on
            uint8_t noise_cancellation;         /// 0->off, 1->on
            uint8_t dev_status;                 /// refer to DevStatus
            int16_t auto_sleep_time;            /// auto sleep time, seconds, 0->do not sleep
            uint8_t vertical;                   /// 0->landscape mode, 1->portrait mode
            uint8_t face_auto_focus;            /// face auto focus, 0->off, 1->on
            uint8_t auto_focus;                 /// auto focus, 0->off, 1->on
            uint8_t manual_focus_value;         /// manual focus value, 0~100
            uint8_t sleep_micro;                /// device in sleep mode: 0->close microphone, 1->open microphone
            uint8_t fov;                        /// fov, refer to FovType
            uint8_t rvd3;                       /// not used
            uint8_t image_flip_hor;             /// Image flipped horizontally: 0->off, 1->on
            uint8_t voice_ctrl_language;        /// Voice ctrl: language, 0->Chinese, 1->English
            /// Voice ctrl: bit0: HiTiny, bit1->Sleep Tiny, bit2->Zoom in, bit3->Zoom out, bit4->Track, bit5->Unlock,
            /// bit6->preset pos
            uint8_t voice_ctrl;                 /// voice control switch, each bit indicates a voice control command
            uint16_t voice_ctrl_zoom;           /// voice ctrl: zoom ratio, 0~100
            uint8_t ai_mode;                    /// AI mode, refer to AiWorkModeType
            uint8_t audio_auto_gain;            /// audio auto gain, 0->off, 1->on
            /// sleep background type: image or video. bit0~bit3: 1->image, 0-none; bit4~bit7: 1->video, 0->none.
            uint8_t sleep_bg_type;
            uint8_t bg_img_idx;                 /// current background index in sleep
            uint8_t ai_sub_mode;                /// Ai sub-mode, refer to AiSubModeType
            uint8_t bg_img_mirror;              /// background image is mirrored or not in sleep mode. 0->off, 1->on.
            uint8_t hdr_support;                /// hdr is support or not at current mode
            uint8_t fps;                        /// current video stream fps
            uint8_t boot_mode;                  /// boot mode: bit0~4->ai_sub_mode, bit5-8->ai_mode
            uint8_t led_brightness_level;       /// 0->close led, 1-3->led brightness level
            struct
            {
                uint8_t distance: 4;            /// Audio reception distance, 0->near, 1->standard, 2->far
                uint8_t uac_enabled: 1;         /// 0->UAC disabled, 1->UAC enabled
                uint8_t reserve: 3;
            } audio_opt;
            uint8_t ble_status;                 /// 0->disconnected, 1->connected, 2->connecting
            uint8_t ai_tracker_speed;           /// 0->normal, 2->motion
            uint8_t live_stream_mode;           /// 0->disabled, 1->enabled
            struct
            {
                uint16_t gesture_auto_frame : 1;     /// 0->disabled, 1->enabled
                uint16_t auto_frame_mode : 2; 	     /// 0->Auto-frame, 1->Close up, 2->Half body, 3->Full body
                uint16_t gesture_zoom : 1;           /// 0->disabled, 1->enabled
                uint16_t gesture_zoom_ratio : 12;    /// 100-400 (1x-4x)
            } gesture_para; 			     /// for meet2, meetSE
            uint8_t rvd[20];
        } tiny; /// used for tiny, tiny4k, tiny2 series, tinySE, meet2. meetSE

        /// meet, meet4k ///////////////////////////////////////////////////////////////////////////////////////////////
        struct
        {
            uint8_t media_mode;                 /// refer to MediaMode
            uint8_t hdr;                        /// hdr: 0->off, 1->on
            uint8_t dev_status;                 /// refer to DevStatus
            uint8_t face_ae;                    /// face auto exposure: 0->off, 1->on
            uint8_t fov;                        /// fov, refer to FovType
            uint8_t bg_mode;                    /// refer to MediaBgMode
            uint8_t blur_level;                 /// virtual background, blur mode, 0~100
            uint8_t anti_flicker;               /// refer to PowerLineFreqType
            uint16_t zoom_ratio;                /// zoom ratio, 0~100
            uint8_t key_mode;                   /// 0->normal mode, 1->rotation mode
            uint8_t rvd1[3];                    /// not used
            uint8_t noise_cancellation;         /// 0->off, 1->on
            uint8_t vertical;                   /// 0->landscape mode, 1->portrait mode
            uint8_t group_single;               /// refer to AutoFramingType
            uint8_t close_upper;                /// refer to AutoFramingType
            int16_t auto_sleep_time;            /// auto sleep time, seconds, 0->do not sleep
            uint8_t img_idx;                    /// virtual background, replacement mode, active image index
            uint8_t rvd2;                       /// not used
            uint8_t bg_color;                   /// refer to MediaBgModeColorType
            uint8_t face_auto_focus;            /// face auto focus, 0->off, 1->on
            uint8_t auto_focus;                 /// auto focus, 0->off, 1->on
            uint8_t manual_focus_value;         /// manual focus value, 0~100
            uint8_t mask_disable;               /// 1->the virtual background is disabled
            uint8_t sleep_micro;                /// device in sleep mode: 0->close microphone, 1->open microphone
            uint8_t image_flip_hor;             /// Image flipped horizontally: 0->off, 1->on
            uint8_t rvd[31];
        } meet;                                 /// for meet, meet4k

        /// tail series ///////////////////////////////////////////////////
        struct
        {
            uint8_t length; 			/// length of data in tail series struct
            uint8_t work_mode;                  /// 0->REC; 1->SNAP; 2->playback mode
            struct
            {
                uint8_t sd_size_extern : 1; 	/// used when sd size > 65535
                uint8_t sd_type : 1; 		/// 1: is ExFat type; 0: si not ExFat type
                uint8_t sleep_micro : 1; /// 0->close microphone, 1->open microphone，for tail2s
                uint8_t dev_status : 1;    	/// 0->sleep, 1->wake, for tail2s
                uint8_t bg_img_idx : 2; 	/// current background index in sleep，for tail2s
                uint8_t bg_img_mirror : 1; /// 0->off, 1->on，for tail2s
                uint8_t live_stream_mode : 1; /// 0->disabled, 1->enabled，for tail2s
            } extern_flag;
            /// sleep background type: image or video.
            /// bit0~bit3: 1->image, 0-none;
            /// bit4~bit7: 1->video, 0->none.
            uint8_t sleep_bg_type;
            struct
            {
                uint8_t start_record: 1;
                uint8_t ndi_boot_enable: 1;
                uint8_t remote_start_record : 1; /// Remote control recording, for Tail2s
                uint8_t remote_start_shoot : 1; /// Remote control shoot, for Tail2s
                uint8_t rvd : 4;
            } boot_media_setting;
            struct
            {
                uint16_t hdr: 1;                /// 1->hdr enabled; 0->hdr disabled
                uint16_t mirror: 1;             /// image mirror status
                uint16_t flip: 1;               /// image flip status
                uint16_t portrait: 1;           /// 1->portrait mode; 0->landscape mode
                uint16_t anti_flick: 2;         /// refer to PowerLineFreqType
                uint16_t face_ae: 1;            /// face auto exposure
                uint16_t face_af: 1;            /// face auto focus
                uint16_t ae_lock: 1;            /// auto exposure lock status
                uint16_t exp_fix_rate: 1;       /// 1->exposure fixed rate
                uint16_t af_mode: 2;            /// 1->afc; 2->afs; 3->mf
                uint16_t af_status;             /// 0->last success; 1->last failed; 2->focusing; 3-cancel
            } media_flags;
            struct
            {
                uint8_t media_switching: 1;     /// media mode switching
                uint8_t hdmi_plugin: 1;         /// 1->hdmi connected; 0->hdmi disconnected
                uint8_t hdmi_osd_enable: 1;     /// 1->hdmi osd enabled; 0->hdmi osd disabled
                uint8_t capture_status: 2;      /// 0 - idle, 1- starting; 2-running 3 stopping
                uint8_t record_status: 2;       /// 0 - idle, 1- starting; 2-running 3 stopping
                uint8_t has_exception: 1;       /// 1->has exception; 0->no exception
            } media_running;
            uint16_t digi_zoom_ratio: 12;       /// digital zoom ratio
            uint16_t digi_zoom_speed: 4;        /// digital zoom speed
            uint8_t hdmi_res_runtime;           /// hdmi resolution runtime
            uint8_t sd_card_speed;
            uint8_t hdmi_size;                  /// video size: 0->1280x720, 1->1920x1080, 2->2704x1520, 3->3840x2160
            uint8_t recording_size;             /// the same as above
            uint8_t ndi_rtsp_size;              /// the same as above
            uint8_t rtmp_size;                  /// the same as above
            uint8_t sensor_fps;                 /// 0xFF->29.97, 0xFE->59.94
            uint8_t mf_code;
            uint8_t srt_enabled;                /// 0->disabled, 1->enabled
            uint8_t sd_status;
            uint8_t brightness;                 /// image brightness
            uint8_t contrast;                   /// image contrast
            uint8_t hue;                        /// image hue
            uint8_t saturation;                 /// image saturation
            uint8_t sharpness;                  /// image sharpness
            uint8_t style;                      /// 0->STANDARD, 1->TEXT, 2->LANDSCAPE, 3->PORTRAIT, 4->NIGHTSCAPE, 5->FILM, 254->CUSTOMER
            uint8_t usb_status;                 /// 0->idel, 1->uvc_uac, 2->uvc_rndis, 3->rndis, 4->mtp, 5->msc, 6->host
            struct
            {
                uint8_t capacity: 7;            /// 0~100: battery capacity
                uint8_t charging: 1;            /// 1->battery is charging
            } battery;
            struct
            {
                uint16_t ai_online: 1;          ///
                uint16_t gim_online: 1;         ///
                uint16_t bat_online: 1;         ///
                uint16_t lens_online: 1;        ///
                uint16_t tof_online: 1;         ///
                uint16_t bluetooth_online: 1;   ///
                uint16_t line_attached : 1; 	/// 1->3.5mm line in connected
                uint16_t poe_attached: 1;       /// 1->poe connected
                uint16_t swivel_base: 1;        /// 1->swivel base connected
                uint16_t mic_attached : 1;	/// 1->3.5mm external microphone connected
                uint16_t sd_insert: 1;          /// 1->sdcard inserted
                uint16_t sensor_err: 1;         /// 1->sensor error
                uint16_t remote_attached: 1;    /// 1->remote controller connected
                uint16_t media_err: 1;          /// 1->camera media error
                uint16_t hdmi_sleep_bg : 1;     /// 0-> close hdmi output,1->open hdmi output when dev sleep
                uint16_t rvd : 1;
            } online_status;
            union
            {
                struct
                {
                    uint16_t sd_total_size; 	/// total size of sd card
                    uint16_t sd_left_size; 	/// left size of sd card
                } ori_sd_size; 			/// original sd size, when sd_size_extern is false
                uint32_t sd_total_size; 	/// total size of sd card, when sd_size_extern is true
            } sd_size;
            int16_t auto_sleep_time;            /// auto sleep time, 0->no auto sleep, num->auto sleep
            uint16_t color_temp;                /// color temperature
            uint8_t ai_type;                    /// 0:normal track, 1:human track, 2:human-upper, 3:human-close up, 4:animal track, 5:group
            uint8_t battery_status;
            uint8_t event_count;
            struct
            {
                uint16_t preset_update: 1;
                uint16_t fov_status: 2;
                uint16_t lens_temp_status:2;     /// 0->normal, 1->warning, 2->error
                uint16_t cpu_temp_status:2;      /// 0->normal, 1->warning, 2->error
                uint16_t px30_attached: 1;       /// 1->px30 connected, 0->px30 disconnected
                uint16_t adapter_plugin: 1;      /// 1->battery adapter connected, 0->battery adapter disconnected
                uint16_t adapter_power : 1;
		uint16_t rvd : 6;
            } misc_status;
            uint32_t sd_left_size; 		/// left size of sd card, when sd_size_extern is true
            uint8_t rvd[11];
        } tail_air;                             /// for tail air

    } CameraStatus;

    /// Just for compatibility, no longer used.
    typedef struct
    {
        uint8_t click_once;
        uint8_t click_double;
        uint8_t default_mode;
    } ButtonAction;
#pragma pack()

    /// video format info the device support
    class VideoFormatInfo
    {
    public:
        int32_t width_, height_;                /// video width and height
        int32_t fps_min_, fps_max_;             /// maximum and minimum fps
        RmVideoFormat format_;                  /// video format

        VideoFormatInfo() : width_(0), height_(0), fps_min_(0), fps_max_(0), format_(RmVideoFormat::Unknown)
        {}

        VideoFormatInfo(int32_t w, int32_t h, int32_t fts_min, int32_t ftp_max, RmVideoFormat format) :
            width_(w), height_(h), fps_min_(fts_min), fps_max_(ftp_max), format_(format)
        {}
    };

    /// Video formats currently supported by the camera
    std::vector<VideoFormatInfo> videoFormatInfo();

    /**
     * @brief Internal use only.
     */
    class DevInfo
    {
    public:
        std::string product_;
        std::string branch_;
        std::string platform_;
        std::string status_;
        std::string version_;
        std::string uuid_;
        uint32_t sys_type_ = INT32_MAX;
        uint32_t soc_ver_ = INT32_MAX;
        std::string sn_;
    };

#ifdef _WIN32

    /**
     * @brief  Get the UVC device path
     * @return  UVC device path
     */
    const std::wstring &videoDevPath() const;

    /**
     * @brief  Get the UAC device path
     * @return  UAC device path
     */
    const std::wstring &audioDevPath() const;

    /**
     * @brief  Get the UVC device friendly name
     * @return  UVC friendly name
     */
    const std::wstring &videoFriendlyName() const;

    /**
     * @brief  Get the UAC device friendly name
     * @return  UAC friendly name
     */
    const std::wstring &audioFriendlyName() const;

#elif __APPLE__

    /// System path for UVC device and UAC device
    /**
     * @brief  uvc protocol version
     * @return the version of the UVC specification which the device implements (as a binary-coded decimal value, e.g.
     *         0x0210 = 2.10).
     */
    UInt16 uvcVersion();

    /**
     * @brief  Get the UVC device path
     * @return  UVC device path
     */
    const std::string &videoDevPath() const;

    /**
     * @brief  Get the UAC device path
     * @return  UAC device path
     */
    const std::string &audioDevPath() const;
#elif __linux__
    /**
     * @brief  Get the UVC device path
     * @return  UVC device path
     */
	const std::string &videoDevPath() const;

	/**
     * @brief  Get the UAC device path
     * @return  UAC device path
     */
	const std::string &audioDevPath() const;

#endif

    explicit Device(DeviceId *id);

    ~Device();

    /**
     * @brief  Get the ip address of device
     * @return  device ip address
     */
    std::string devIp();

public:
    // -----------------------------------------------------------------------------------------------------------------
    /**
     * @brief  Get the device name.
     * @return  Device name.
     */
    const std::string &devName();

    /**
     * @brief  Get the device model.
     * @return  A string representing the device model.
     */
    const std::string &devModelCode();

    /**
     * @brief  Get the device wifi mac address.
     * @return  Device wifi mac address.
     */
    std::string devWifiMac();

    /**
     * @brief  Get the device bluetooth mac address.
     * @return  Device bluetooth mac address.
     */
    std::string devBleMac();

    /**
     * @brief  Get the wifi mode of the device.
     * @return  "ap" or "station" or "unknown".
     */
    const std::string &devWifiMode();

    /**
     * @brief  Get the wifi ssid of the device.
     * @return  Wifi ssid.
     */
    const std::string &devWifiSsid();

    /**
     * @brief  Get the device wireless ip address.
     * @return  Device wireless ip address.
     */
    std::string devWirelessIp();

    /**
     * @brief  Get the device wired ip address.
     * @return  Device wired address.
     */
    std::string devWiredIp();

    /**
     * @brief  Get the current DevMode mode.
     * @return  The current device mode.
     */
    DevMode devMode();

    /**
     * @brief  Get the current firmware version of the device.
     * @return  A string similar to "1.2.3.4".
     */
    std::string devVersion();

    /**
     * @brief  Get the current SN of the device, which is used to uniquely identify the device.
     * @return  A string of 14 characters.
     */
    std::string devSn();

    /**
     * @brief  Get the current system type of the device, refer to DevSysType.
     * @return  The current system type.
     */
    DevSysType devSysType();

    /**
     * @brief  Internal use only.
     */
    void setDevSysType(DevSysType sys_type);

    /**
     * @brief  After registering the DevStatusCallback function, it will take effect only by calling
     *         enableDevStatusCallback.
     * @param  [in] enabled   true to enable DevStatusCallback, false to disable.
     */
    void enableDevStatusCallback(bool enabled);

    /**
     * @brief  Get the uuid of the device.
     * @return  The uuid of the device.
     */
    DevUuid uuid();

    /**
     * @brief  Indicates whether the device has been initialized. The device will be initialized automatically when it
     *         is detected by the system. Just for test.
     * @return  true if the device has been initialized, or false.
     */
    bool isInited();

    /**
     * @brief  Internal use only.
     */
    DevInfo devInfo();

    /**
     * @brief  When downloading or uploading the resource from the device, you need to specify the save path
     *         of the file first. The image format is jpg. Up to 3 or 4 images can be stored in device.
     * @category  meet, meet4k, meet2, meetSE, tiny2 series, tinySE.
     * @param  [in] resource_mini   The full path for the thumbnail, such as "C:/image_mini.jpg"
     * @param  [in] resource        The full path for the original image, such as "C:/image.jpg" or "C:/video.mp4"
     * @param  [in] index           The index of image to set, valid value range: 0~2.
     */
    void setLocalResourcePath(std::string resource_mini, std::string resource, uint32_t index);

    /**
     * @brief  Get the original image save path. An empty string will be return if not set.
     * @category  meet, meet4k, tiny2.
     * @param  [in] index   The index of image to get, valid value range: 0~2 for meet and meet4k, 0~3 for tiny2.
     * @return  The original image save path or an empty string if not set.
     */
    std::string localFilePath(int32_t index);

    /**
     * @brief  Get the thumbnail save path. An empty string will be return if not set.
     * @category  meet, meet4k, tiny2.
     * @param  [in] index   The index of thumbnail to get, valid value range: 0~2 for meet and meet4k, 0~3 for tiny2.
     * @return  The thumbnail save path or an empty string if not set.
     */
    std::string localFileMiniPath(int32_t index);

    /**
     * @brief  Get the camera status, not the current status, there may be a lag of two or three seconds.
     * @category  all.
     * @return  Returns the most recently fetched camera status from the device.
     */
    CameraStatus cameraStatus();

    /**
     * @brief  Get the product type, refer to ObsbotProductType.
     * @category  all.
     * @return  The product type.
     */
    ObsbotProductType productType();

    /**
     * @brief  The device internally manages a counter. When the count value reaches UVC_DEV_CAM_STATUS_REFRESH_PERIOD,
     *         it will fetch camera status from the device once, then the callback function DevStatusCallback will be
     *         called. The value of the counter can be dynamically changed to control the time point when the next
     *         callback function is triggered.
     * @category  all.
     * @param  [in] value   The counter value.
     */
    void nextRefreshDevStatus(int32_t value = UVC_DEV_CAM_STATUS_REFRESH_PERIOD);

    /**
     * @brief  The device internally manages a counter. When the count value
     *         reaches UVC_DEV_CAM_STATUS_REFRESH_PERIOD, it will fetch
     *         camera status from the device once, then the callback
     *         function DevStatusCallback will be called. The value of the
     *         counter can be dynamically changed to control the time point
     *         when the next callback function is triggered, fast status.
     * @category  all.
     * @param  [in] value   The counter value.
     */
    void fastNextRefreshDevStatus(int32_t value = FAST_UVC_DEV_CAM_STATUS_REFRESH_PERIOD);

    /**
     * @brief  Get the current value of the counter, refer to nextRefreshDevStatus.
     * @return  The current value of the counter.
     */
    int32_t stateCnt();

    /**
     * @brief  Get the current value of the counter, refer to
     *         nextRefreshDevStatus.
     * @return  The current value of the counter.
     */
    int32_t fastStateCnt();

    /**
     * @brief  Register DevStatusCallback function, refer to DevStatusCallback.
     * @category  all.
     * @param  [in] callback   The callback function to register.
     * @param  [in] param      User-defined parameter.
     */
    void setDevStatusCallbackFunc(DevStatusCallback callback, void *param);

    /**
     * @brief  Register DevStatusCallback function, refer to
     *         DevStatusCallback, fast status.
     * @category  all.
     * @param  [in] callback   The callback function to register.
     * @param  [in] param      User-defined parameter.
     */
    void setFastDevStatusCallbackFunc(FastDevStatusCallback callback, void *param);

    /**
     * @brief  Register DevEventNotifyCallback function, refer to DevEventNotifyCallback.
     * @category  tail air.
     * @param  [in] callback   The callback function to register.
     * @param  [in] param      User-defined parameter.
     */
    void setDevEventNotifyCallbackFunc(DevEventNotifyCallback callback, void *param);

    /**
     * @brief  Register FileDownloadCallback function, refer to DevStatusCallback.
     * @category  meet, meet4k, meet2, meetSE, tiny2 series, tinySE.
     * @param  [in] callback   The callback function to register.
     * @param  [in] param      User-defined parameter.
     */
    void setFileDownloadCallback(FileDownloadCallback callback, void *param);

    /**
     * @brief  Register FileUploadCallback function, refer to DevStatusCallback.
     * @category  meet, meet4k, meet2, meetSE, tiny2 series, tinySE.
     * @param  [in] callback   The callback function to register.
     * @param  [in] param      User-defined parameter.
     */
    void setFileUploadCallback(FileUploadCallback callback, void *param);

    /**
     * @brief  Start to upload background image from the device. Before uploading, the path of the uploaded file must
     *         first be set with function setLocalImagePath.
     * @category  meet, meet4k, meet2, meetSE, tiny2 series, tinySE.
     * @param  [in] file_type   The image type that will be uploaded, refer to FileType.
     * @return  true for success, false for failed.
     */
    bool startFileUploadAsync(FileType file_type);

    /**
     * @brief  Start to download background image from the device. Before downloading, the save path of the downloaded
     *         file must first set with function setLocalImagePath.
     * @category  meet, meet4k, meet2, meetSE, tiny2 series, tinySE.
     * @param  [in] file_type   The image type that will be downloaded, refer to FileType.
     * @return  true for success, false for failed.
     */
    bool startFileDownloadAsync(FileType file_type);

public:
    // -----------------------------------------------------------------------------------------------------------------
    /**
     * @brief  Internal use only.
     */
    int32_t normalizedZoom(int32_t zoom_ratio);

    /**
     * @brief  Set the relative movement speed of video preview in pan and tilt axis.
     * @category  meet, meet4k, meet2, meetSE.
     * @param  [in] pan_speed    Param range: -1.0~1.0
     * @param  [in] tilt_speed   Param range: -1.0~1.0
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetPanTiltRelative(double pan_speed, double tilt_speed);

    /**
     * @brief  Set the absolute movement position of video preview in pan and tilt axis.
     * @category  meet, meet4k, meet2, meetSE.
     * @param  [in] pan_deg    Param range: -1.0~1.0
     * @param  [in] tilt_deg   Param range: -1.0~1.0
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetPanTiltAbsolute(double pan_deg, double tilt_deg);

public:
    // -----------------------------------------------------------------------------------------------------------------
    /// Remo Protocol
    /**
     * @brief  AI selects or deselects a target.
     * @category  tiny, tiny4k.
     * @param  [in] flag   true    Select a target.
     *                     false   Deselect a target.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetTargetSelectR(bool flag);

    /**
     * @deprecated  Just for compatibility, no longer used.
     */
    int32_t aiSetGestureCtrlR(bool flag);

    /**
     * @brief  Turn on or off the specified gesture function.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [in] gesture   Gesture type.
     *                        0       Gesture target.
     *                        1       Gesture zoom.
     *                        2       Gesture dynamic zoom. (tiny2 series, tail air)
     *                        3       Gesture dynamic zoom direction. (tiny2 series, tail air)
     *                        4       Gesture record. (tail air)
     * @param  [in] flag      false   Turn off.
     *                        true    Turn on.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetGestureCtrlIndividualR(int32_t gesture, bool flag);

    /**
     * @brief  Get current AI status synchronously or asynchronously, refer to AiStatus.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [out] ai_status   Receive AI status, refer to AiStatus. It can be NULL in async mode.
     * @param  [in] callback     Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param        User-defined parameter, not used in sync mode.
     * @param  [in] method       Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetAiStatusR(AiStatus *ai_status, RxDataCallback callback = nullptr, void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Enable extra functions of the button.
     * @category  me.
     */
    int32_t aiSetButtonSwitchR(bool enabled);

    /**
     * @brief  Set the rotation speed of the gimbal, it can be set to 0 to stop the gimbal. Invalid value has no effect.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [in] pitch   The pitch speed, valid range: -90~90.
     * @param  [in] pan     The pan speed, valid range: -180~180.
     * @param  [in] roll    The roll speed, valid range: -180~180. (not used at present)
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetGimbalSpeedCtrlR(double pitch, double pan, double roll = 200.0);

    /**
     * @brief  Stop the gimbal.
     * @category  tiny2 series, tail air.
     */
    int32_t aiSetGimbalStop();

    /**
     * @brief  Move the gimbal to specified motor angle. Invalid value has no effect.
     * @category  tiny2 series, tail air.
     * @param  [float] roll    Aim roll degree, valid range -180~180, not used at present, set to 0.
     * @param  [float] pitch   Aim pitch degree, valid range -90~90.
     * @param  [float] yaw     Aim yaw degree, valid range -180~180.
     */
    int32_t aiSetGimbalMotorAngleR(float pitch, float yaw, float roll = -1000.f);

    /**
     * @brief  Get current Gimbal state info synchronously or asynchronously, refer to AiGimbalStateInfo.
     * @category  tiny2 series, tail air.
     * @param  [out] gim_info   Receive Gimbal state info, refer to AiGimbalStateInfo. It can be NULL in async mode.
     * @param  [in] callback    Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param       User-defined parameter, not used in sync mode.
     * @param  [in] method      Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetGimbalStateR(AiGimbalStateInfo *gim_info, RxDataCallback callback = nullptr, void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Set the boot initial position and zoom ratio.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [in] preset_info    Refer to PresetPosInfo.
     * @param  [in] presets_flag   The preset actions is exist or not.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetGimbalBootPosR(const PresetPosInfo &preset_info, bool presets_flag = false);

    /**
     * @brief  Get the boot initial position.
     * @category  tiny, tiny4k, tiny2, tail air.
     * @param  [out] preset_info   Receive Gimbal boot info, refer to PresetPosInfo. It can be NULL in async mode.
     * @param  [in] callback       Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param          User-defined parameter, not used in sync mode.
     * @param  [in] method         Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetGimbalBootPosR(PresetPosInfo *preset_info, RxDataCallback callback = nullptr, void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Move the gimbal to the boot initial position. reset_mode should be set to true in zone tracking mode.
     * @category  tiny2 series, tail air.
     * @param  reset_mode   Must be set to true in zone tracking mode, false for others.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiTrgGimbalBootPosR(bool reset_mode = false);

    /**
     * @brief  Reset the boot initial position to default value (include gimbal boot position and zoom ratio).
     * @category  tiny2 series, tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiRstGimbalBootPosR();

    /**
     * @brief  Turn on/off the AI function. When using aiSetGimbalSpeedCtrlR to control the movement of the gimbal, the
     *         AI function should be turned of first; after the end of control, the AI function should be turned on.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [in] enabled   true    Turn on AI.
     *                        false   Turn off AI.
     * @return
     */
    int32_t aiSetEnabledR(bool enabled);

    /**
     * @brief  Set AI smart tracking mode.
     * @category  tiny, tiny4k, tiny2 series.
     * @param  [in] mode   Refer to AiVerticalTrackType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetTrackingModeR(AiVerticalTrackType mode);

    /**
     * @brief  Reset the gimbal to zero position. If the AI smart tracking is enabled, gimbal is always controlled by
     *         AI. The follow command be used to disable AI smart tracking.
     *         aiSetTargetSelectR(false)                    For tiny, tiny4k.
     *         cameraSetAiModeU(Device::AiWorkModeNone)     For tiny2, tail air.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t gimbalRstPosR();

    /**
     * @brief  Set the speed of the gimbal. If the AI smart tracking is enabled, gimbal is always controlled by AI.
     *         The follow command be used to disable AI smart tracking.
     *         aiSetTargetSelectR(false)                    For tiny, tiny4k.
     *         cameraSetAiModeU(Device::AiWorkModeNone)     For tiny2, tail air.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [in] pitch   Pitch speed, valid range: -90~90.
     * @param  [in] pan     Pan speed, valid range: -180~180.
     * @param  [in] roll    Roll speed, valid range: -90~90, not used at present, set to 0.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t gimbalSpeedCtrlR(double pitch, double pan, double roll = 0.0);

    /**
     * @brief  Get the motor angle of the gimbal synchronously or asynchronously.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [in] xyz        Receive the roll, pitch, pan angle in deg. It can be NULL in async mode.
     * @param  [in] callback   Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param      User-defined parameter, not used in sync mode.
     * @param  [in] method     Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t gimbalGetAttitudeInfoR(float xyz[3], RxDataCallback callback = nullptr, void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Set the aim position of the gimbal and the reference speed for running to the target position.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [in] roll      Target position for roll, not used.
     * @param  [in] pitch     Target position for roll, valid range: -90~90.
     * @param  [in] yaw       Target position for roll, valid range: -120~120.
     * @param  [in] s_roll    Roll reference speed, valid range: -90~90.
     * @param  [in] s_pitch   Pitch reference speed, valid range: -90~90.
     * @param  [in] s_yaw     Yaw reference speed, valid range: -90~90.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t gimbalSetSpeedPositionR(float roll, float pitch, float yaw, float s_roll, float s_pitch, float s_yaw);

    /**
     * @brief  Get the absolute zoom param range.
     * @category  tiny, tiny4k, tiny2 series, tail air, meet, meet4k.
     * @param  range   Refer to UvcParamRange.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeZoomAbsoluteR(UvcParamRange &range);

    /**
     * @brief  Set the camera's zoom level.
     * @category  tiny, tiny4k, tiny2 series, meet, meet4k, tail air.
     * @param  [in] zoom   The input value is normalized, valid range: 1.0~2.0.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetZoomAbsoluteR(float zoom);

    /**
     * @brief  Get the normalized zoom value.
     * @category  tiny, tiny4k, tiny2 series, meet, meet4k, tail air.
     * @return  The normalized zoom value, 1.0~2.0
     */
    int32_t cameraGetZoomAbsoluteR(float &zoom);

    /**
     * @brief  Whether to enable face focus function.
     * @category  all.
     * @param  [in] enable   true    Enable face focus.
     *                       false   Disable face focus.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetFaceFocusR(bool enable);


    enum DevWdrMode
    {
        DevWdrModeNone,                     /// HDR disabled
        DevWdrModeDol2TO1,                  /// HDR enabled
        DevWdrModeDol3TO1,                  /// not used at present
        DevWdrModeDolPixelGainBySensor,     /// not used at present
        DevWdrModeDolPixelGainByIsp,        /// not used at present
    };

    /**
     * @brief  Whether to wdr_mode the WDR function. Since WDR switching is a time-consuming task, it is not suitable to
     *         switch frequently. The recommended interval is 3 seconds between two mode switches.
     * @category  tiny4k, tiny2 series, meet series, tail air.
     * @param  [in] wdr_mode   Refer to DevWdrMode;
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetWdrR(int32_t wdr_mode);

    /**
     * @brief  Get the current WDR state.
     * @category  tail air.
     * @param  [out] wdr_mode   true    Enable HDR.
     *                          false   Disable HDR.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetWdrR(int32_t &wdr_mode);

    /**
     * @brief  Get the current support WDR states.
     * @category  tail air.
     * @param  [out] wdr_mode   true    Enable HDR.
     *                        false   Disable HDR.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetWdrListR(std::vector<int32_t> &wdr_list);

    /**
     * @brief  Reset the device to factory settings.
     * @category  all.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetRestoreFactorySettingsR();

    /**
     * @brief  Set the working state of the device. When the device is not in use, it can be set to sleep to save power.
     * @category  all.
     * @param  [in] type   Refer to DevStatus.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetDevRunStatusR(DevStatus type);

    /**
     * @brief  Whether to enable face autofocus function.
     * @category  all.
     * @param  [in] face_ae   0   Disable face autofocus.
     *                        1   Enable face autofocus.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetFaceAER(int32_t face_ae);

    /**
     * @brief  Whether to disable the automatic sleep when the device does not output video stream.
     * @category  meet series.
     * @param  enable   true    Disable the automatic sleep.
     *                  false   Enable the automatic sleep.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetDisableSleepWithoutStreamU(bool enable);

    /**
     * @brief  Whether to enable microphone function during device is in sleep mode.
     * @category  tiny4k, tiny2 series, meet series.
     * @param  [in] microphone   0   Disable microphone in sleep mode.
     *                           1   Enable microphone in sleep mode.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetMicrophoneDuringSleepU(int32_t microphone);

    /**
     * @brief  Whether to enable image horizontal flip function.
     * @category  tiny4k, tiny2 series, meet series.
     * @param  [in] horizon   0   Disable image horizontal flip.
     *                        1   Enable image horizontal flip.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetImageFlipHorizonU(int32_t horizon);

    /**
     * @brief  Set the automatic sleep time of the device.
     * @category  tiny, tiny4k, tiny2 series, meet series.
     * @param  [in] sleep_time   The sleep time to set, valid range: -65535~65535 seconds.
     *                           A negative value or 0 can be used to disable automatic sleep.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetSuspendTimeU(int32_t sleep_time);

    /**
     * @brief  Set the auto frame mode, refer to AutoFramingType.
     * @category  meet series.
     * @param  [in] group_single   AutoFrmGroup       Parameter "close_upper" will be ignored in this mode.
     *                             AutoFrmSingle
     * @param  [in] close_upper    AutoFrmCloseUp     Only valid in AutoFrmSingle mode.
     *                             AutoFrmUpperBody   Only valid in AutoFrmSingle mode.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetAutoFramingModeU(AutoFramingType group_single, AutoFramingType close_upper);

    /**
     * @brief  Select or delete the background image with the specified index. For tiny2, it is used to set the sleep
     *         background image or video; for meet and meet4k, it is used to set the virtual background.
     * @category  tiny2 series, meet series.
     * @param  [in] action   0   Select background image or video.
     *                       1   Delete background image or video.
     *                       2   Mirror background image, video is not supported. Only support tiny2.
     * @param  [in] idx_or_state    For action 0~1, the index of the background image, valid range: 0~2.
     *                              For action 2, 0->disabled, 1->enabled.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetResourceActionU(int32_t action, int32_t idx_or_state);

    /**
     * @brief  Set the device to portrait mode. The device will restart automatically.
     * @category  tiny4k.
     * @param  [in] vertical
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetVerticalModeU(int32_t vertical);

    /**
     * @brief  Set the FOV of the camera.
     * @category  tiny series, meet series.
     * @param  [in] fov_type  The fov type to set, refer to FovType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetFovU(FovType fov_type);

    /**
     * @brief  Get the camera status info.
     * @category  all.
     * @param  [out] camera_status   Refer to CameraStatus.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetCameraStatusU(CameraStatus &camera_status);

    /**
     * @brief  Set the media mode. Since media mode switching is a time-consuming task, it is not suitable to switch
     *         frequently. The recommended interval is 3 seconds between two mode switches.
     * @category  meet, meet4k.
     * @param  [in] mode   The media mode to set, refer to MediaMode.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetMediaModeU(MediaMode mode);

    /**
     * @brief  Set the virtual background mode. Before it can take effect, the MediaMode should be set to
     *         "MediaModeBackground" using cameraSetMediaModeU.
     * @category  meet, meet4k.
     * @param  [in] mode   The expected virtual background mode, refer to MediaBgMode.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetBgModeU(MediaBgMode mode);

    /**
     * @brief  Set the background color in the green mode. Before it can take effect, the MediaMode should be set to
     *         "MediaModeBackground" using cameraSetMediaModeU and the MediaBgMode should be set to "MediaBgModeColor"
     *         using cameraSetBgModeU.
     * @category  meet, meet4k.
     * @param  [in] bg_color   The background color to set, refer to MediaBgModeColorType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetBgColorU(MediaBgModeColorType bg_color);

    /**
     * @brief  Whether to enable the virtual background function of the device. Before using the virtual background,
     *         this function must be enabled first.
     * @category  meet, meet4k.
     * @param  [in] enable   true->Enable the virtual background function.
     *                       false->Disable the virtual background function.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetBgEnableU(bool enable);

    /**
     * @brief  Set the button mode to be used. The current mode can be get by CameraStatus-meet-key_mode.
     * @category  meet, meet4k.
     * @param  [in] mode   The button mode to be used.
     *                     0   Normal mode
     *                     1   Rotation mode
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetButtonModeU(int32_t mode);

    /**
     * @brief  Customize the function of the button, not used any more.
     * @deprecated  Just for compatibility, no longer used.
     * @category  meet, meet4k.
     * @param  [in] btn_action
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetCustomizeButtonActionU(ButtonAction btn_action);

    /**
     * @brief  Set the blur level of the background. Before it can take effect, the MediaMode should be set to
     *         "MediaModeBackground" using cameraSetMediaModeU and the MediaBgMode should be set to "MediaBgModeBlur"
     *         using cameraSetBgModeU.
     * @category  meet, meet4k.
     * @param  [in] level   Blur level, valid range is 0 ~ 100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetMaskLevelU(int32_t level);

    /**
     * @brief  Set current AI mode for tiny2.
     * @category  tiny2.
     * @param  [in] mode               The aim AI mode, refer to AiWorkModeType.
     * @param  [in] sub_mode_or_from   For AiWorkModeHuman mode, it means the AI sub-mode, refer to AiSubModeType.
     *                                 For other modes, it means where the command comes from.
     *                                 0->Normal case.
     *                                 1->From remote controller.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetAiModeU(AiWorkModeType mode, int32_t sub_mode_or_from = 0);

    /**
     * @brief  Enable or Disable the specified voice control command, select the voice control language or set the zoom
     *         factor.
     * @category  tiny2 series.
     * @param  [in] cmd     Command type, refer to AudioCtrlCmdType
     * @param  [in] state   If cmd is AudioCtrlHiTiny ~ AudioCtrlPreset, state can be 0 or 1
     *                         0->Disable the voice control command
     *                         1->Enable the voice control command
     *                      If cmd is AudioCtrlZoomFactor, state is used to set the zoom factor with valid range 0~100.
     *                      If cmd is AudioCtrlLanguage, state is used to set the voice control language, refer to
     *                         AudioCtrlLanguageType
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetAudioCtrlStateU(AudioCtrlCmdType cmd, int32_t state);

    /**
     * @brief  Enable the audio auto gain control or not.
     * @category  tiny2 series.
     * @param  [in] enabled   true->Enable audio auto gain control.
     *                        false->Disable audio auto gain control.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetAudioAutoGainU(bool enabled);

    /**
     * @brief  Open the special led pattern before setting the zone tracking or hand tracking and close it after
     *         finishing setting.
     * @category  tiny2.
     * @param  [in] enabled   true->Open the special LED pattern.
     *                        false->Close the special LED pattern.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetLedCtrlU(bool enabled);

    /**
     * @brief  Set the boot AI smart mode and sub-mode.
     * @category  tiny2.
     * @param  [in] main_mode   Refer to AiWorkModeType.
     * @param  [in] sub_mode    Refer to AiSubModeType, only take effect in AiWorkModeHuman.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetBootModeU(AiWorkModeType main_mode, AiSubModeType sub_mode);

    /**
     * @brief  Get the id list of existing zone preset positions. The zone preset position info can be got according to
     *         specified id.
     * @category  tiny2 series.
     * @param  [out] ids       Receive thd id list of existing zone preset positions. It can be NULL in async mode.
     *                         data_int32 is used in DevDataArray.
     * @param  [in] callback   Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param      User-defined parameter, not used in sync mode.
     * @param  [in] method     Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetZonePresetListR(DevDataArray *ids, RxDataCallback callback = nullptr, void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Get the zone preset position info with specified id.
     * @category  tiny2 series.
     * @param  [out] preset_info  Receive the zone preset position info. It can be NULL in async mode.
     * @param  [in] id            Id of the aim zone preset position.
     * @param  [in] callback      Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param         User-defined parameter, not used in sync mode.
     * @param  [in] method        Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetZonePresetInfoWithIdR(PresetPosInfo *preset_info, int32_t id, RxDataCallback callback = nullptr, void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Get the name of zone preset position with specified id.
     * @category  tiny2 series.
     * @param  [out] name      Receive the name of zone preset position with specified id. It can be NULL in async mode.
     *                         data_int8 is used in DevDataArray.
     * @param  [in] id         Id of the aim zone preset position.
     * @param  [in] callback   Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param      User-defined parameter, not used in sync mode.
     * @param  [in] method     Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetZonePresetNameWithIdR(DevDataArray *name, int32_t id, RxDataCallback callback = nullptr, void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Set the name of zone preset position with specified id.
     * @category  tiny2 series.
     * @param  [in] name   The name of the zone preset position.
     * @param  [in] id     The index of the zone preset position.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetZonePresetNameWithIdR(const std::string &name, int32_t id);

    /**
     * @brief  Add a zone preset position. The zone preset position will be updated if it has already exist.
     * @category  tiny2 series.
     * @param  [in] preset_info  The zone preset position info, refer to PresetPosInfo.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiAddZonePresetR(PresetPosInfo *preset_info);

    /**
     * @brief  Delete a zone preset position with specified id.
     * @category  tiny2 series.
     * @param  [in] id   The zone preset position index to be deleted.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiDelZonePresetR(int32_t id);

    /**
     * @brief  Update the zone preset position. The setting will be ignored if the zone preset position with specified
     *         id does not exist.
     * @category  tiny2 series.
     * @param  [in] preset_info   The zone preset position info to be updated.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiUpdZonePresetUpdateR(PresetPosInfo *preset_info);

    /**
     * @brief  Move the gimbal to the zone tracking preset position with specified id.
     * @category  tiny2 series.
     * @param  [in] id   The id of the aim zone tracking position.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiTrgZonePresetR(int32_t id);

    /**
     * @brief  Used to disable the auto tracking of gimbal before setting the zone preset position, it should be
     *         re-enabled after that.
     * @category  tiny2 series.
     * @param  [in] enabled   true->Enable the auto track.
     *                        false->Disable the auto track.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetZoneTrackGimbalEnabledR(bool enabled);

    /**
     * @brief  Set the min degree of yaw in limited zone track. Right is positive when facing the device.
     * @category  tiny2 series.
     * @param  deg   The minimum degree, valid range: -120~120.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetLimitedZoneTrackYawMinR(float deg);

    /**
     * @brief  Get the min degree of yaw in limited zone track. Right is positive when facing the device.
     * @category  tiny2 series.
     * @param  [out] deg          The minimum degree, valid range: -120~120.
     * @param  [in] id            The index of the initial hand tracking info to get.
     *                            -1->Get the default value.
     *                            0->Get the current value.
     * @param  [in] callback      Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param         User-defined parameter, not used in sync mode.
     * @param  [in] method        Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetLimitedZoneTrackYawMinR(float &deg, int32_t id, RxDataCallback callback = nullptr, void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Set the max degree of yaw in limited zone track. Right is positive when facing the device.
     * @category  tiny2 series.
     * @param  deg   The maximum degree, valid range: -120~120.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetLimitedZoneTrackYawMaxR(float deg);

    /**
     * @brief  Get the max degree of yaw in limited zone track. Right is positive when facing the device.
     * @category  tiny2 series.
     * @param  [out] deg          The minimum degree, valid range: -120~120.
     * @param  [in] id            The index of the initial hand tracking info to get.
     *                            -1->Get the default value.
     *                            0->Get the current value.
     * @param  [in] callback      Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param         User-defined parameter, not used in sync mode.
     * @param  [in] method        Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetLimitedZoneTrackYawMaxR(float &deg, int32_t id, RxDataCallback callback = nullptr, void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Set the min degree of pitch in limited zone track. Down is positive when facing the device.
     * @category  tiny2 series.
     * @param  deg   The minimum degree, valid range: -90~90.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetLimitedZoneTrackPitchMinR(float deg);

    /**
     * @brief  Get the min degree of pitch in limited zone track. Down is positive when facing the device.
     * @category  tiny2 series.
     * @param  [out] deg          The minimum degree, valid range: -120~120.
     * @param  [in] id            The index of the initial hand tracking info to get.
     *                            -1->Get the default value.
     *                            0->Get the current value.
     * @param  [in] callback      Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param         User-defined parameter, not used in sync mode.
     * @param  [in] method        Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetLimitedZoneTrackPitchMinR(float &deg, int32_t id, RxDataCallback callback = nullptr,
                                           void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Set the max degree of pitch in limited zone track. Down is positive when facing the device.
     * @category  tiny2 series.
     * @param  deg   The maximum degree, valid range: -90~90.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetLimitedZoneTrackPitchMaxR(float deg);

    /**
     * @brief  Get the max degree of pitch in limited zone track. Down is positive when facing the device.
     * @category  tiny2 series.
     * @param  [out] deg          The minimum degree, valid range: -120~120.
     * @param  [in] id            The index of the initial hand tracking info to get.
     *                            -1->Get the default value.
     *                            0->Get the current value.
     * @param  [in] callback      Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param         User-defined parameter, not used in sync mode.
     * @param  [in] method        Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetLimitedZoneTrackPitchMaxR(float &deg, int32_t id, RxDataCallback callback = nullptr,
                                           void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Set whether to enable automatic target selection for region-limited tracking.
     * @category  tiny2 series.
     * @param  [int] enabled  false->Disabled auto-select function
     *                        true->Enabled auto-select function
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetLimitedZoneTrackAutoSelectR(bool enabled);

    /**
     * @brief  Get whether to enable automatic target selection for region-limited tracking.
     * @category  tiny2 series.
     * @param  [out] enabled  false->Disabled auto-select function
     *                        true->Enabled auto-select function
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetLimitedZoneTrackAutoSelectR(bool &enabled);

    /**
     * @brief  Set the initial position of limited zone tracking.
     * @category  tiny2 series.
     * @param  [in] preset_info   The initial position, refer to PresetPosInfo.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetLimitedZoneTrackInitPosR(PresetPosInfo *preset_info);

    /**
     * @brief  Get the initial position info of limited zone tracking, refer to PresetPosInfo.
     * @category  tiny2 series.
     * @param  [out] preset_info   Receive the initial position info of limited zone tracking. It can be NULL in async mode.
     * @param  [in] id             The index of the initial limited zone tracking info to get.
     *                             -1->Get the default value.
     *                             0->Get the current value.
     * @param  [in] callback       Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param          User-defined parameter, not used in sync mode.
     * @param  [in] method         Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetLimitedZoneTrackInitPosR(PresetPosInfo *preset_info, int32_t id, RxDataCallback callback = nullptr,
                                          void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Reset the initial position of limited zone tracking to default value.
     * @category  tiny2 series.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiRstLimitedZoneTrackInitPosR();

    /**
     * @brief  Move the gimbal to the limited zone tracking initial position.
     * @category  tiny2 series.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiTrgLimitedZoneTrackInitPosR();

    /**
     * @brief  Reset the minimum yaw degree of limited zone tracking zone to default value.
     * @category  tiny2 series.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiRstLimitedZoneTrackYawMinR();

    /**
     * @brief  Reset the maximum yaw degree of limited zone tracking zone to default value.
     * @category  tiny2 series.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiRstLimitedZoneTrackYawMaxR();

    /**
     * @brief  Reset the minimum pitch degree of limited zone tracking zone to default value.
     * @category  tiny2 series.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiRstLimitedZoneTrackPitchMinR();

    /**
     * @brief  Reset the maximum pitch degree of limited zone tracking zone to default value.
     * @category  tiny2 series.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiRstLimitedZoneTrackPitchMaxR();

    /**
     * @brief  Set whether to enable the limited track region function.
     * @category  tiny2 series.
     * @param  [in] enabled   false->Disabled the limited track region function
     *                        true->Enabled the limited track region function
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetLimitedZoneTrackEnabledR(bool enabled);

    /**
     * @brief  Get whether to enable the limited track region function.
     * @category  tiny2 series.
     * @param  [out] enabled  false->Disabled the limited track region function
     *                        true->Enabled the limited track region function
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetLimitedZoneTrackEnabledR(bool &enabled);

    /**
     * @brief  Get the id list of existing preset positions. The preset position info can be got according to specified
     *         id.
     * @category  tiny2 series, tail air.
     * @param  [out] ids       Receive the id list of existing preset positions. It can be NULL in async mode.
     *                         data_int32 is used in DevDataArray.
     * @param  [in] callback   Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param      User-defined parameter, not used in sync mode.
     * @param  [in] method     Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetGimbalPresetListR(DevDataArray *ids, RxDataCallback callback = nullptr, void *param = nullptr,
                                   GetMethod method = Block);

    /**
     * @brief  Get the preset position info with specified id.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [out] preset_info  Receive the preset position info. It can be NULL in async mode.
     * @param  [in] id            Id of the aim preset position.
     * @param  [in] callback      Callback function after receiving the response from device, not used in sync mode.
     *                            Not used for tiny, tiny4k.
     * @param  [in] param         User-defined parameter, not used in sync mode.
     *                            Not used for tiny, tiny4k.
     * @param  [in] method        Refer to GetMethod.
     *                            Not used for tiny, tiny4k.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetGimbalPresetInfoWithIdR(PresetPosInfo *preset_info, int32_t id, RxDataCallback callback = nullptr,
                                         void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Get the name of preset position with specified id.
     * @category  tiny2 series, tail air.
     * @param  [out] name      Receive the name of preset position with specified id. It can be NULL in async mode.
     *                         data_int8 is used in DevDataArray.
     * @param  [in] id         Id of the aim preset position.
     * @param  [in] callback   Callback function after receiving the response from device, not used in sync mode.
     * @param  [in] param      User-defined parameter, not used in sync mode.
     * @param  [in] method     Refer to GetMethod.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetGimbalPresetNameWithIdR(DevDataArray *name, int32_t id, RxDataCallback callback = nullptr,
                                         void *param = nullptr, GetMethod method = Block);

    /**
     * @brief  Set the name of preset position with specified id.
     * @category  tiny2 series, tail air.
     * @param  [in] name   The name of preset position.
     * @param  [in] id     The index of preset position.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetGimbalPresetNameWithIdR(const std::string &name, int32_t id);

    /**
     * @brief  Add a preset position. The preset position will be updated if it has already exist.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [in] preset_info   The preset position info, refer to PresetPosInfo.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiAddGimbalPresetR(PresetPosInfo *preset_info);

    /**
     * @brief  Delete a preset position with specified id.
     * @category  tiny, tiny4k, tiny2, tail air.
     * @param  [in] id   The preset position index to be deleted.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiDelGimbalPresetR(int32_t id);

    /**
     * @brief  Update the preset position. The setting will be ignored if the preset position with specified id does
     *         not exist.
     * @category  tiny2 series, tail air.
     * @param  [in] preset_info   The zone preset position info to be updated.
     * @param  [in] presets_flag   The preset actions is exist or not.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiUpdGimbalPresetR(PresetPosInfo *preset_info, bool presets_flag = false);

    /**
     * @brief  Move the gimbal to the preset position with specified id.
     * @category  tiny, tiny4k, tiny2 series, tail air.
     * @param  [in] pos_id  The specified preset id.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiTrgGimbalPresetR(int pos_id);

    /**
     * @brief  Enable the zone tracking function or not.
     * @category  tiny2 series, tail air.
     * @param  [in] enabled   true->Enable the zone tracking.
     *                        false->Disable the zone tracking.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetZoneTrackStateR(bool enabled);

    /**
     * @brief  Enable the auto zoom function of AI or not.
     * @category  tiny2 series, tail air.
     * @param  [in] enabled   true->Enable the auto zoom function of AI.
     *                        false->Disable the auto zoom function of AI.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetAiAutoZoomR(bool enabled);

    /**
     * @brief  Enable the direction reverse of yaw axis of gimbal or not.
     * @category  tiny2 series, tail air.
     * @param  [in] enabled   true->Enable the direction reverse of yaw axis.
     *                        false->Disable the direction reverse of yaw axis.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetGimbalYawDirReverseR(bool enabled);

    Device(const Device &) = delete;

    Device &operator=(const Device &) = delete;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// tiny2 series, tail air, remo protocol v3 ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// tiny2 series, tail air, remo protocol v3 /// Camera media mode setting ////////////////////////////////////////////////
    /**
     * @brief  Start or stop taking photos.
     * @category  tail air.
     * @param  [in] operation   0->Stop taking photos.
     *                          1->Start taking photos in normal mode.
     *                          2->Start taking photos in burst mode.
     * @param  [in] param       Only valid in burst mode.
     *                          range: 0~0xFFFE, the number of photos.
     *                          range: > 0xFFFF, keep taking photos until receiving stop command.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetTakePhotosR(uint32_t operation, uint32_t param);

    /**
     * @brief  Start or stop video recording.
     * @category  tail air.
     * @param  [in] operation   0->Stop video recording.
     *                          1->Start video recording.
     * @param  [in] param       Not used at present.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetVideoRecordR(uint32_t operation, uint32_t param = 0);

    /**
     * @brief  Set the delay time in time-lapse photograph.
     * @category  tail air.
     * @param  [in] delay   The delay time in seconds.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetDelayTimeInTimelapse(uint32_t delay);

    /**
     * @brief  Get the delay time in time-lapse photograph.
     * @category  tail air.
     * @param  [out] delay   The delay time in seconds.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetDelayTimeInTimelapse(uint32_t &delay);

    /**
     * @brief  Cancel the time-lapse action before the delay time expires.
     * @category  tail air.
     * @param  [out] delay   The delay time in seconds.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetCancelDelayActionInTimelapse();

    /**
     * @brief  Set the boot mode of the camera.
     * @category  tail air.
     * @param  [in] enabled     false->Enable the boot settings.
     *                          true->Disable the boot settings.
     * @param  [in] main_mode   0->Recode mode.
     *                          1->Capture mode.
     * @param  [in] sub_mode    In record mode, 0->normal record mode; 1->The last saved record mode.
     *                          In capture mode, 0->normal record mode; 1->The last saved capture mode.
     * @param  [in] action      0->Idle; 1->Default action.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetBootStatus(bool enabled, uint32_t main_mode, uint32_t sub_mode, uint32_t action);

    /**
     * @brief  Get the boot mode info of the camera.
     * @category  tail air.
     * @param  [out] enabled     true->The boot settings is enabled.
     *                           false->The boot settings is disable.
     * @param  [out] main_mode   0->Recode mode.
     *                           1->Capture mode.
     * @param  [out] sub_mode    In record mode, 0->normal record mode; 1->The last saved record mode.
     *                           In capture mode, 0->normal record mode; 1->The last saved capture mode.
     * @param  [out] action      0->Idle; 1->Default action.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetBootStatus(bool &enabled, uint32_t &main_mode, uint32_t &sub_mode, uint32_t &action);

    /**
     * @brief  Set the photo quality.
     * @category  tail air.
     * @param  [in] quality   0->Default quality.
     *                        1->Low quality, QP=90.
     *                        2->Medium quality, QP=95.
     *                        3->High quality, QP=99.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetPhotoQualityR(int32_t quality);

    /**
     * @brief  Set the photo format.
     * @category  tail air.
     * @param  [in] format   0->Default format.
     *                       1->Jpeg.
     *                       2->raw.
     *                       3->jpeg + raw.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetPhotoFormatR(int32_t format);

    // video resolution and fps
    enum DevVideoResType
    {
        DevVideoResAuto = 0,
        DevVideoRes4KP30 = 1,
        DevVideoRes4KP25 = 2,
        DevVideoRes4KP24 = 3,
        DevVideoRes4KP60 = 4,
        DevVideoRes4KP50 = 5,
        DevVideoRes4KP48 = 6,
        DevVideoRes4KP29D97 = 9,
        DevVideoRes1080P30 = 0x21,
        DevVideoRes1080P25 = 0x22,
        DevVideoRes1080P24 = 0x23,
        DevVideoRes1080P60 = 0x24,
        DevVideoRes1080P50 = 0x25,
        DevVideoRes1080P48 = 0x26,
        DevVideoRes1080P29D97 = 0x2B,
        DevVideoRes1080P59D94 = 0x2C,
        DevVideoRes720P30 = 0x31,
        DevVideoRes720P25 = 0x32,
        DevVideoRes720P24 = 0x33,
        DevVideoRes720P60 = 0x34,
        DevVideoRes720P50 = 0x35,
        DevVideoRes720P48 = 0x36,
        DevVideoRes720P29D97 = 0x3B,
        DevVideoRes720P59D94 = 0x3C,
    };

    /**
     * @brief  Set the video resolution for recording.
     * @category  tail air.
     * @param  [in] res_type   refer to DevVideoResType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetRecordResolutionR(DevVideoResType res_type);

    enum DevVideoSplitSizeType
    {
        DevVideoSplitAuto = 0,              /// auto select
        DevVideoSplitOff,                   /// split off
        DevVideoSplit4GB,                   /// split by 4GB
        DevVideoSplit8GB,                   /// split by 8GB
        DevVideoSplit16GB,                  /// split by 16GB
        DevVideoSplit32GB,                  /// split by 32GB
        DevVideoSplit64GB,                  /// split by 64GB
    };

    /**
     * @brief  Get the split size of video file when recording.
     * @category  tail air.
     * @param  [out] split_type   refer to DevVideoSplitSizeType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRecordSplitSizeR(DevVideoSplitSizeType &split_type);

    /**
     * @brief  Set the split size of video file when recording.
     * @category  tail air.
     * @param  [in] split_type   refer to DevVideoSplitSizeType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetRecordSplitSizeR(DevVideoSplitSizeType split_type);

    // video encoder format, for tail air, tail2
    enum DevVideoEncoderFormat
    {
        DevVideoEncoderAuto = 0,
        DevVideoEncoderH264,
        DevVideoEncoderH265,
        DevVideoEncoderMJPEG,
        DevVideoEncoderAV1,
        DevVideoEncoderNdiFull,
    };

    /**
     * @brief  Get the encoder format for main video.
     * @category  tail air.
     * @param  [in] format   Refer to DevVideoEncoderFormat.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetMainVideoEncoderFormatR(DevVideoEncoderFormat &format);

    /**
     * @brief  Set the encoder format for main video.
     * @category  tail air.
     * @param  [in] format   Refer to DevVideoEncoderFormat.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetMainVideoEncoderFormatR(DevVideoEncoderFormat format);

    enum DevVideoBitLevelType
    {
        DevVideoBitLevelDefault = 0,        /// Default quality
        DevVideoBitLevelLow,                /// Low bitrate
        DevVideoBitLevelMedium,             /// Medium bitrate
        DevVideoBitLevelHigh,               /// High bitrate
    };

    /**
     * @brief  Get the video bitrate level for main video.
     * @category  tail air.
     * @param  [in] quality   Refer to DevVideoBitLevelType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetMainVideoBitrateLevelR(DevVideoBitLevelType &bit_level);

    /**
     * @brief  Set the video bitrate level for main video.
     * @category  tail air.
     * @param  [in] quality   Refer to DevVideoBitLevelType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetMainVideoBitrateLevelR(DevVideoBitLevelType bit_level);

    enum DevActivateModuleType
    {
        DevActivateModuleTypeDefault = 0,    /// Default
        DevActivateModuleTypeNdi,            /// Ndi
    };

    /**
     * @brief  Get the activation status of the specified module.
     * @category  tail air.
     * @param  [in] module_type   Refer to DevActivateModuleType.
     * @param  [out] activate     true->The module is activated.
     *                            false->The module is not activated.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetModuleActiveR(DevActivateModuleType module_type, bool &activate);

    /**
     * @brief  Set the video resolution for KCP preview.
     * @category  tail air.
     * @param  [in] res_type   refer to DevVideoResType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetKcpPreviewResolutionR(DevVideoResType res_type);

    /**
     * @brief  Set the video resolution for NDI and RTSP.
     * @category  tail air.
     * @param  [in] res_type   refer to DevVideoResType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetNdiRtspResolutionR(DevVideoResType res_type);

    /**
     * @brief  Get the video bitrate level for NDI and RTSP.
     * @category  tail air.
     * @param  [in] bit_level   Refer to DevVideoBitLevelType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetNdiRtspBitrateLevelR(DevVideoBitLevelType &bit_level);

    /**
     * @brief  Set the video bitrate level for NDI and RTSP.
     * @category  tail air.
     * @param  [in] bit_level   Refer to DevVideoBitLevelType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetNdiRtspBitrateLevelR(DevVideoBitLevelType bit_level);

    /**
     * @brief  Get the encoder format for NDI and RTSP.
     * @category  tail air.
     * @param  [in] format   Refer to DevVideoEncoderFormat.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetNdiRtspEncoderFormatR(DevVideoEncoderFormat &format);

    /**
     * @brief  Set the encoder format for NDI and RTSP.
     * @category  tail air.
     * @param  [in] format   Refer to DevVideoEncoderFormat.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetNdiRtspEncoderFormatR(DevVideoEncoderFormat format);

    enum RtspOrNdiEnabled
    {
        RtspDisabledAndNdiDisabled = 0,        /// The NDI is disabled, and the RTSP is disabled.
        RtspDisabledAndNdiEnabled,             /// The NDI is enabled, and the RTSP is disabled.
        RtspEnabledAndNdiDisabled,             /// The NDI is disabled, and the RTSP is enabled.
    };

    /**
     * @brief  Get the switch to select rtsp or ndi.
     * @category  tail air.
     * @param  [out] type   Refer to RtspOrNdiEnabled.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetSelectNdiOrRtspR(RtspOrNdiEnabled &type);

    /**
     * @brief  Set the switch to select rtsp or ndi.
     * @category  tail air.
     * @param  [in] type   Refer to RtspOrNdiEnabled.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetSelectNdiOrRtspR(RtspOrNdiEnabled type);

    /**
     * @brief  Enable the NDI when start up or not.
     * @category  tail air.
     * @param  [in] enabled   true->Enable the NDI when device start up.
     *                        false->Disable the NDI when device start up.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetBootNdiEnabledR(bool enabled);

    enum DevImageMirrorFlipType
    {
        DevImgMirrorOffFlipOff = 0,
        DevImgMirrorOnFlipOff,
        DevImgMirrorOffFlipOn,
        DevImgMirrorOnFlipOn,
        DevImgMirrorOnSingle,               /// Only set mirror On
        DevImgMirrorOffSingle,              /// Only set mirror Off
        DevImgFlipOnSingle,                 /// Only set flip On
        DevImgFlipOffSingle,                /// Only set flip On
    };

    /**
     * @brief  Mirror or flip the video image.
     * @category  tail air.
     * @param  [in] mirror_flip   Refer to DevImageMirrorFlipType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetMirrorFlipR(int32_t mirror_flip);

    /**
     * @brief  Get the mirror and flip state of the video image.
     * @category  tail air.
     * @param  [out] mirror_flip   Refer to DevImageMirrorFlipType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetMirrorFlipR(int32_t &mirror_flip);

    enum DevRotationState
    {
        DevRotation0,                       /// no rotation
        DevRotation90,                      /// rotate 90
        DevRotation180,                     /// rotate 180
        DevRotation270,                     /// rotate 270
    };

    /**
     * @brief  Set the rotation state of the device.
     * @category  tail air.
     * @param  [in] rotation   Refer to DevRotationState.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetRotationDegree(int32_t rotation);

    /**
     * @brief  Get the rotation state of the device.
     * @category  tail air.
     * @param  [in] rotation   Refer to DevRotationState.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRotationDegree(int32_t &rotation);

    /**
     * @brief  Set the zoom ratio and zoom speed.
     * @category  tiny2, tail air.
     * @param  [in] zoom_ratio   The aim zoom ratio. 100x magnification of the actual value.
     * @param  [in] zoom_speed   The zoom speed.
     *                           0   ->Default zoom speed decided by device.
     *                           1-10->Larger value means faster zoom speed.
     *                           255 ->The maximum zoom speed.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetZoomWithSpeedAbsoluteR(uint32_t zoom_ratio, uint32_t zoom_speed);

    /**
     * @brief  Set the zoom speed in relative mode, the zoom direction (zoom in or out), and the zoom mode.
     * @category  tail air.
     * @param  [in] zoom_step    The zoom step. Only valid in step mode (step_mode = true).
     * @param  [in] zoom_speed   The zoom speed.
     *                           0   ->Default zoom speed decided by device.
     *                           1-10->Larger value means faster zoom speed.
     *                           255 ->The maximum zoom speed.
     * @param  [in] step_mode    true->move on step every command; false->keep moving until receiving stop command.
     * @param  [in] in_out       true->zoom in; false->zoom out.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetZoomWithSpeedRelativeR(uint32_t zoom_step, uint32_t zoom_speed, bool step_mode, bool in_out);

    /**
     * @brief  Stop zoom right now.
     * @category  tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetZoomStopR();

    enum ROIViewType
    {
        ROIViewDefault = 0,
        ROIViewTargetLargeScale,
        ROIViewTargetMediumScale,
        ROIViewTargetSmallScale,
        ROIViewTargetHand,
        ROIViewTargetAuto,
        ROIViewStdAuto,
        ROIViewStdGroup,
    };

    /**
     * @brief  Set the ROI view.
     * @category  tail air.
     * @param  [in] roi_type   0->Immediately switch to target view.
     *                         1->Smoothly switch to target view.
     * @param  [in] vid        The view type of ROI, refer to ROIViewType.
     * @param  [in] x_min      ROI x-coordinate min, 0.0~1.0.
     * @param  [in] x_max      ROI x-coordinate max, 0.0~1.0.
     * @param  [in] y_min      ROI y-coordinate min, 0.0~1.0.
     * @param  [in] y_max      ROI y-coordinate max, 0.0~1.0.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetRoiTarget(int32_t roi_type, int32_t vid, float x_min = 0.f, float y_min = 0.f, float x_max = 1.f,
                               float y_max = 1.f);

    enum HdmiOsdLanguage
    {
        HdmiOsdLanguageAuto = 0,
        HdmiOsdLanguageEnglish,
        HdmiOsdLanguageChineseSimple,
        HdmiOsdLanguageChineseTraditional,
        HdmiOsdLanguageSpanish,
        HdmiOsdLanguageGerman,
        HdmiOsdLanguageJapanese,
        HdmiOsdLanguageKorean,
        HdmiOsdLanguageFrench,
    };

    enum HdmiOutputContent
    {
        HdmiOutputContentProgramOutput = 0,
        HdmiOutputContentFullFrame,
    };

#pragma pack(1)
    typedef struct
    {
        HdmiOsdLanguage osd_language;
        HdmiOutputContent content;
        int32_t volume;
        DevVideoResType resolution;
        int32_t info_display;
    } HdmiInfo;
#pragma pack()

    /**
     * @brief  Get the info of hdmi.
     * @category  tail air.
     * @param  [out] hdmi_info   Refer to HdmiInfo.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetHdmiInfoR(HdmiInfo &hdmi_info);

    /**
     * @brief  Set the info of hdmi.
     * @category  tail air.
     * @param  [in] hdmi_info   Refer to HdmiInfo.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetHdmiInfoR(const HdmiInfo &hdmi_info);

    /**
     * @brief  Get the attribute of watermark.
     * @category  tail air.
     * @param  [out] enabled   true->The watermark is opened.
     *                         false->The watermark is closed.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetWatermarkAttributeR(bool &enabled);

    /**
     * @brief  Set the attribute of watermark.
     * @category  tail air.
     * @param  [in] enabled   true->The watermark is opened.
     *                        false->The watermark is closed.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetWatermarkAttributeR(bool enabled);

    ////////////////////////////////////////////////////////////////////////
    /// Only tail2 and later products, remo protocol v3
    /// Camera media parameter setting
    ////////////////////////////////////////////////////////////////////////

    enum DevMediaStreamId
    {
        DevMediaStreamIdAuto = 0,
        DevMediaStreamIdRecord,
        DevMediaStreamIdCapture,
        DevMediaStreamIdLive,
        DevMediaStreamIdRtsp,
        DevMediaStreamIdNdi,
        DevMediaStreamIdSrt,
        DevMediaStreamIdUvc,
        DevMediaStreamIdKcp,
        DevMediaStreamIdHdmi,
        DevMediaStreamIdSdi,
    };

    enum DevMediaParamOperation
    {
        DevMediaParamOperationAuto = 0,
        DevMediaParamOperationStart,
        DevMediaParamOperationStop,
        DevMediaParamOperationPause,
        DevMediaParamOperationResume,
    };

    /**
     * @brief  Set the operation parameters of the stream, such as start, stop, pause... .
     * @category  tail2.
     * @param  [in] stream_id  refer to DevMediaStreamId.
     * @param  [in] action  Refer to DevMediaParamOperation.
     *
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetMediaOperateParamR(DevMediaStreamId stream_id, DevMediaParamOperation action);

    /**
     * @brief  Get the operation parameters of the stream, such as start, stop, pause... .
     * @category  tail2.
     * @param  [in] stream_id  refer to DevMediaStreamId.
     * @param  [out] action  Refer to DevMediaParamOperation.
     *
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetMediaOperateParamR(DevMediaStreamId stream_id, DevMediaParamOperation &action);

    typedef struct
    {
        int32_t width;
        int32_t height;
    } DevMediaResolution;

    typedef struct
    {
        int32_t width;
        int32_t height;
        int32_t fps;
    } DevMediaResolutionFps;

    typedef struct
    {
        int32_t min;
        int32_t max;
        int32_t def;
        int32_t step;
    } DevMediaRange;

    /// Camera video config info
    typedef struct
    {
        DevMediaResolutionFps res_fps;
        DevVideoEncoderFormat enc_fmt;
        DevMediaRange bitrate_range;
    } DevVideoConfig;

    /// Camera media config info
    typedef struct
    {
        DevMediaStreamId stream_id;
        std::vector<DevMediaResolution> res_list;
        std::vector<int32_t> fps_list;
        std::vector<DevMediaResolutionFps> res_fps_list;
        std::vector<DevVideoEncoderFormat> enc_fmt_list;
        DevMediaRange bitrate_range;
        std::vector<DevVideoConfig> video_config_list;
    } DevMediaConfig;

    /// Camera config range info
    typedef struct
    {
        /// dev sn
        std::string dev_sn;
        /// Media info
        std::vector<DevMediaConfig> media_config;
        /// Support system information and device information when needed later
    } DevConfigRangeInfo;

    /**
     * @brief Get the range parameters of the device configuration.
     * @category  tail2.
     * @param  [in] callback   The callback function to register.
     * @param  [in] param      User-defined parameter.
     * @param  [in] data       In network mode, the device configuration information
     *                         obtained through http.
     * @param  [in] data_size  In network mode, the device configuration information size.
     *
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetConfigRange(const DevConfigRangeCallback &callback, void *param, const std::string &data = "",
                                    int data_size = 0);

    typedef struct DevMediaEncodeParam
    {
        int32_t width = -1;
        int32_t height = -1;
        int32_t fps = -1;
        int32_t bitrate = -1;
        DevVideoEncoderFormat encode_format = DevVideoEncoderAuto;
    } DevMediaEncodeParam;

    /**
     * @brief  Set recording encoding parameters.
     * @category  tail2.
     * @param  [in] encode_param  Refer to DevMediaEncodeParam.
     *
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetRecordEncodeParamR(const DevMediaEncodeParam &encode_param);

    /**
     * @brief  Get recording encoding parameters.
     * @category  tail2.
     * @param  [out] encode_param  Refer to DevMediaEncodeParam.
     *
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRecordEncodeParamR(DevMediaEncodeParam &encode_param);

    /**
     * @brief  Set output encoding parameters, such as rtsp, ndi, srt, etc.
     * @category  tail2.
     * @param  [in] encode_param  Refer to DevMediaEncodeParam.
     *
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetOutputEncodeParamR(const DevMediaEncodeParam &encode_param);

    /**
     * @brief  Get output encoding parameters, such as rtsp, ndi, srt, etc.
     * @category  tail2.
     * @param  [out] encode_param  Refer to DevMediaEncodeParam.
     *
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetOutputEncodeParamR(DevMediaEncodeParam &encode_param);

    /// tiny2, tail air, remo protocol v3 /// Camera media param setting ///////////////////////////////////////////////
    enum DevWhiteBalanceType
    {
        DevWhiteBalanceAuto = 0,                   /// auto mode
        DevWhiteBalanceDaylight = 1,               /// daylight
        DevWhiteBalanceFluorescent = 2,            /// fluorescent lamp
        DevWhiteBalanceTungsten = 3,               /// tungsten lamp
        DevWhiteBalanceFlash = 4,                  /// flash
        DevWhiteBalanceFine = 9,                   /// sunshine
        DevWhiteBalanceCloudy = 10,                /// cloudy
        DevWhiteBalanceShade = 11,
        DevWhiteBalanceDayLightFluorescent = 12,
        DevWhiteBalanceDayWhiteFluorescent = 13,
        DevWhiteBalanceCoolWhiteFluorescent = 14,
        DevWhiteBalanceWhiteFluorescent = 15,
        DevWhiteBalanceWarmWhiteFluorescent = 16,
        DevWhiteBalanceStandardLightA = 17,
        DevWhiteBalanceStandardLightB = 18,
        DevWhiteBalanceStandardLightC = 19,
        DevWhiteBalance55 = 20,
        DevWhiteBalance65 = 21,
        DevWhiteBalanceD75 = 22,
        DevWhiteBalanceD50 = 23,
        DevWhiteBalanceIsoStudioTungsten = 24,
        DevWhiteBalanceManual = 255,               /// manual white balance
    };

    /**
     * @brief  Set the white balance param.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  wb_type   White balance type, refer to DevWhiteBalanceType.
     * @param  param     Only valid when wb_type is ImgWhiteBalanceManual.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetWhiteBalanceR(DevWhiteBalanceType wb_type, int32_t param);

    /**
     * @brief  Get the white balance param.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [in] wb_type   White balance type, refer to DevWhiteBalanceType.
     *                        Only DevWhiteBalanceAuto and DevWhiteBalanceManual are supported except tail air.
     * @param  [in] param     Only valid when wb_type is ImgWhiteBalanceManual.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetWhiteBalanceR(DevWhiteBalanceType &wb_type, int32_t &param);

    /**
     * @brief  Get the white balance param.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] wb_type   White balance type, refer to DevWhiteBalanceType.
     *                         Only DevWhiteBalanceAuto and DevWhiteBalanceManual are supported except tail air.
     * @param  param           Only valid when wb_type is ImgWhiteBalanceManual.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetWhiteBalanceListR(std::vector<int32_t> &wb_list, int32_t &wb_min, int32_t &wb_max);

    /**
     * @brief  Get the white balance param range.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  range   Refer to UvcParamRange.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeWhiteBalanceR(UvcParamRange &range);

    /**
     * @brief  Set the maximum and minimum ISO.
     * @category  tail air.
     * @param  [in] min_iso   The minimum iso value.
     * @param  [in] max_iso   The maximum iso value.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetISOLimitR(uint32_t min_iso, uint32_t max_iso);

    /**
     * @brief  Get the maximum and minimum ISO.
     * @category  tail air.
     * @param  [out] min_iso   The minimum iso value。
     * @param  [out] max_iso   The maximum iso value.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetISOLimitR(uint32_t &min_iso, uint32_t &max_iso);

    /**
     * @brief  Enable the AE lock or not.
     * @category  tail air.
     * @param  [in] enabled   true->Enable the AE lock.
     *                        false->Disable the AE lock.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetAELockR(bool enabled);

    /**
     * @brief  Get the AE lock state.
     * @category  tail air.
     * @param  [out] enabled   true->AE lock is enabled.
     *                         false->AE lock is disabled.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetAELockR(bool &enabled);

    /**
     * @brief  Get the enable state of face AE.
     * @category  tail air.
     * @param  enabled   true->The face AE is enabled.
     *                   false->The face AE is disabled.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetFaceAER(bool &enabled);

    enum DevExposureModeType
    {
        DevExposureUnknown = 0,
        DevExposureManual = 1,
        DevExposureAllAuto,
        DevExposureAperturePriority,
        DevExposureShutterPriority,
    };

    /**
     * @brief  Set the shutter mode.
     * @category  tail air.
     * @param  [in] exposure_mode   Shutter mode, refer to DevExposureModeType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetExposureModeR(int32_t exposure_mode);

    /**
     * @brief  Get the shutter mode.
     * @category  tail air.
     * @param  [out] exposure_mode   Shutter mode, refer to DevExposureModeType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetExposureModeR(int32_t &exposure_mode);

    enum DevAEEvBiasType
    {
        DevAEEvBias_Auto = -1,          /// –
        DevAEEvBias_NEG_3_0 = 0,        /// -3.0
        DevAEEvBias_NEG_2_7 = 1,        /// -2.7
        DevAEEvBias_NEG_2_3 = 2,        /// -2.3
        DevAEEvBias_NEG_2_0 = 3,        /// -2.0
        DevAEEvBias_NEG_1_7 = 4,        /// -1.7
        DevAEEvBias_NEG_1_3 = 5,        /// -1.3
        DevAEEvBias_NEG_1_0 = 6,        /// -1.0
        DevAEEvBias_NEG_0_7 = 7,        /// -0.7
        DevAEEvBias_NEG_0_3 = 8,        /// -0.3
        DevAEEvBias_0 = 9,              /// 0
        DevAEEvBias_0_3 = 10,           /// +0.3
        DevAEEvBias_0_7 = 11,           /// +0.7
        DevAEEvBias_1_0 = 12,           /// +1.0
        DevAEEvBias_1_3 = 13,           /// +1.3
        DevAEEvBias_1_7 = 14,           /// +1.7
        DevAEEvBias_2_0 = 15,           /// +2.0
        DevAEEvBias_2_3 = 16,           /// +2.3
        DevAEEvBias_2_7 = 17,           /// +2.7
        DevAEEvBias_3_0 = 18,           /// +3.0
    };

    /**
     * @brief  Set the EVBIAS of auto exposure in P Gear. Backlight compensation Control in Uvc.
     * @category  tail air.
     * @param  [in] ev_bias   Refer to DevAEEvBiasType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetPAEEvBiasR(int32_t ev_bias);

    /**
     * @brief  Get the EVBIAS of auto exposure in P Gear. Backlight compensation Control in Uvc.
     * @category  tail air.
     * @param  [out] ev_bias   Refer to DevAEEvBiasType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetPAEEvBiasR(int32_t &ev_bias);

    /**
     * @brief  Get the EVBIAS range of auto exposure in P Gear. Backlight compensation Control in Uvc.
     * @category  tail air.
     * @param  [out] range   Refer to UvcParamRange and DevAEEvBiasType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangePAEEvBiasR(UvcParamRange &range);

    /**
     * @brief  Set the EVBIAS of auto exposure in S Gear.
     * @category  tail air.
     * @param  [in] ev_bias   Refer to DevAEEvBiasType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetSAEEvBiasR(DevAEEvBiasType ev_bias);

    enum DevShutterTimeType
    {
        DevShutterTime_Auto = 0,
        DevShutterTime_1_8000 = 9,     /// 1/8000
        DevShutterTime_1_6400,         /// 1/6400
        DevShutterTime_1_5000,         /// 1/5000
        DevShutterTime_1_4000,         /// 1/4000
        DevShutterTime_1_3200,         /// 1/3200
        DevShutterTime_1_2500,         /// 1/2500
        DevShutterTime_1_2000,         /// 1/2000
        DevShutterTime_1_1600,         /// 1/1600
        DevShutterTime_1_1250,         /// 1/1250
        DevShutterTime_1_1000,         /// 1/1000
        DevShutterTime_1_800,          /// 1/800
        DevShutterTime_1_640,          /// 1/640
        DevShutterTime_1_500,          /// 1/500
        DevShutterTime_1_400,          /// 1/400
        DevShutterTime_1_320,          /// 1/320
        DevShutterTime_1_240,          /// 1/240
        DevShutterTime_1_200,          /// 1/200
        DevShutterTime_1_160,          /// 1/160
        DevShutterTime_1_120,          /// 1/120
        DevShutterTime_1_100,          /// 1/100
        DevShutterTime_1_80,           /// 1/80
        DevShutterTime_1_60,           /// 1/60
        DevShutterTime_1_50,           /// 1/50
        DevShutterTime_1_40,           /// 1/40
        DevShutterTime_1_30,           /// 1/30
        DevShutterTime_1_25,           /// 1/25
        DevShutterTime_1_20,           /// 1/20
        DevShutterTime_1_15,           /// 1/15
        DevShutterTime_1_12D5,         /// 1/12.5
        DevShutterTime_1_10,           /// 1/10
        DevShutterTime_1_8,            /// 1/8
        DevShutterTime_1_6D25,         /// 1/6.25
        DevShutterTime_1_5,            /// 1/5
        DevShutterTime_1_4,            /// 1/4
        DevShutterTime_1_3,            /// 1/3
        DevShutterTime_1_2D5,          /// 1/2.5
        DevShutterTime_1_2,            /// 1/2
    };

    /**
     * @brief  Set the shutter time of auto exposure in S Gear.
     * @category  tail air.
     * @param  [in] shutter_time   Refer to DevShutterTimeType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetSAEShutterR(int32_t shutter_time);

    /**
     * @brief  Get the shutter time of auto exposure in S Gear.
     * @category  tail air.
     * @param  [out] shutter_time   Refer to DevShutterTimeType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetSAEShutterR(int32_t &shutter_time);

    /**
     * @brief  Set the EVBIAS of auto exposure in A Gear.
     * @category  tail air.
     * @param  [in] ev_bias   Refer to DevAEEvBiasType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetAAEEvBiasR(DevAEEvBiasType ev_bias);

    /**
     * @brief  Get the EVBIAS of auto exposure in A Gear.
     * @category  tail air.
     * @param  [out] ev_bias   Refer to DevAEEvBiasType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetAAEEvBiasR(DevAEEvBiasType &ev_bias);

    enum DevAEApertureType
    {
        DevAEApertureApex_Auto = 0,                /// —	—
        DevAEApertureApex_0_FNO_1_0 = 1,           /// 0	1.0
        DevAEApertureApex_0_3_FNO_1_1 = 2,         /// 0.3	1.1
        DevAEApertureApex_0_7_FNO_1_3 = 3,         /// 0.7	1.3
        DevAEApertureApex_1_0_FNO_1_4 = 8,         /// 1.0	1.4
        DevAEApertureApex_1_3_FNO_1_6 = 9,         /// 1.3	1.6
        DevAEApertureApex_1_7_FNO_1_8 = 10,        /// 1.7	1.8
        DevAEApertureApex_2_0_FNO_2_0 = 16,        /// 2.0	2.0
        DevAEApertureApex_2_3_FNO_2_2 = 17,        /// 2.3	2.2
        DevAEApertureApex_2_7_FNO_2_5 = 18,        /// 2.7	2.5
        DevAEApertureApex_3_0_FNO_2_8 = 24,        /// 3.0	2.8
        DevAEApertureApex_3_3_FNO_3_1 = 25,        /// 3.3	3.1
        DevAEApertureApex_3_7_FNO_3_6 = 26,        /// 3.7	3.6
        DevAEApertureApex_4_0_FNO_4_0 = 32,        /// 4.0	4.0
        DevAEApertureApex_4_3_FNO_4_4 = 33,        /// 4.3	4.4
        DevAEApertureApex_4_7_FNO_5_1 = 34,        /// 4.7	5.1
        DevAEApertureApex_5_0_FNO_5_6 = 40,        /// 5.0	5.6
        DevAEApertureApex_5_3_FNO_6_3 = 41,        /// 5.3	6.3
        DevAEApertureApex_5_7_FNO_7_2 = 42,        /// 5.7	7.2
        DevAEApertureApex_6_0_FNO_8_0 = 48,        /// 6.0	8.0
        DevAEApertureApex_6_3_FNO_8_9 = 49,        /// 6.3	8.9
        DevAEApertureApex_6_7_FNO_10_2 = 50,       /// 6.7	10.2
        DevAEApertureApex_7_0_FNO_11_3 = 56,       /// 7.0	11.3
        DevAEApertureApex_7_3_FNO_12_5 = 57,       /// 7.3	12.5
        DevAEApertureApex_7_7_FNO_14_4 = 58,       /// 7.7	14.4
        DevAEApertureApex_8_0_FNO_16_0 = 64,       /// 8.0	16.0
    };

    /**
     * @brief  Set the aperture of auto exposure in A Gear.
     * @category  tail air.
     * @param  [in] aperture   Refer to DevAEApertureType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetAAEApertureR(DevAEApertureType aperture);

    /**
     * @brief  Set the shutter time of auto exposure in M Gear.
     * @category  tail air.
     * @param  [in] shutter_time   Refer to DevShutterTimeType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetMAEShutterR(int32_t shutter_time);

    /**
     * @brief  Get the shutter time of auto exposure in M Gear.
     * @category  tail air.
     * @param  [out] shutter_time   Refer to DevShutterTimeType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetMAEShutterR(int32_t &shutter_time);

    /**
     * @brief  Set the aperture of auto exposure in M Gear.
     * @category  tail air.
     * @param  [in] aperture   Refer to DevAEApertureType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetMAEApertureR(DevAEApertureType aperture);

    /**
     * @brief  Set the ISO of auto exposure in M Gear.
     * @category  tail air.
     * @param  [in] iso   ISO.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetMAEIsoR(int32_t iso);

    /**
     * @brief  Get the ISO of auto exposure in M Gear.
     * @category  tail air.
     * @param  [out] iso   ISO.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetMAEIsoR(int32_t &iso);

    /**
     * @brief  Get the ISO range of auto exposure in M Gear.
     * @category  tail air.
     * @param  [out] range   Refer to UvcParamRange.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeMAEIsoR(UvcParamRange &range);

    /**
     * @brief  Set the anti-flicker mode.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [in] freq   Refer to PowerLineFreqType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetAntiFlickR(int32_t freq);

    /**
     * @brief  Get the anti-flicker mode.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] freq   Refer to PowerLineFreqType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetAntiFlickR(int32_t &freq);

    /**
     * @brief  Get the anti-flicker mode range.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] range   Refer to UvcParamRange.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeAntiFlickR(UvcParamRange &range);

    /**
     * @brief  Set the shutter value.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [in] shutter_time   Refer to DevShutterTimeType.
     * @param  [in] auto_enabled   True->auto exposure is enabled, and shutter_time is ignored in this case.
     *                             False->auto exposure is disable, and shutter_time is used to set the shutter time.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetExposureAbsolute(int32_t shutter_time, bool auto_enabled);

    /**
     * @brief  Get the shutter time and the auto exposure mode.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] shutter_time   Refer to DevShutterTimeType.
     * @param  [out] auto_enabled   True if auto exposure is enabled, shutter_time is ignored in this case.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetExposureAbsolute(int32_t &shutter_time, bool &auto_enabled);

    /**
     * @brief  Set the shutter value.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [in] range   Refer to UvcParamRange and DevShutterTimeType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeExposureAbsolute(UvcParamRange &range);

    enum DevImageStyle
    {
        DevImageStyleStandard = 0,        /// standard
        DevImageStyleText,                /// text
        DevImageStyleLandScape,           /// land scape
        DevImageStylePortrait,            /// portrait
        DevImageStyleNightScape,          /// night scape
        DevImageStyleFilm,                /// film
        DevImageStyleCustomer = 254,      /// customer
    };

    /**
     * @brief  Set the style of the image.
     * @category  tail air.
     * @param  [in] style   Refer to DevImageStyle.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetImageStyleR(DevImageStyle style);

    /**
     * @brief  Set the brightness value of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [in] brightness   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetImageBrightnessR(int32_t brightness);

    /**
     * @brief  Get the brightness value of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out]brightness   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetImageBrightnessR(int32_t &brightness);

    /**
     * @brief  Get the brightness range of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] range   Refer to UvcParamRange.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeImageBrightnessR(UvcParamRange &range);

    /**
     * @brief  Set the contrast value of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [in] contrast   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetImageContrastR(int32_t contrast);

    /**
     * @brief  Get the contrast value of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] contrast   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetImageContrastR(int32_t &contrast);

    /**
     * @brief  Get the contrast range of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] range   Refer to UvcParamRange.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeImageContrastR(UvcParamRange &range);

    /**
     * @brief  Set the hue value of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [in] hue   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetImageHueR(int32_t hue);

    /**
     * @brief  Get the hue value of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] hue   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetImageHueR(int32_t &hue);

    /**
     * @brief  Get the hue range of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] range   Refer to UvcParamRange.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeImageHueR(UvcParamRange &range);

    /**
     * @brief  Set the saturation value of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [in] saturation   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetImageSaturationR(int32_t saturation);

    /**
     * @brief  Get the saturation value of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] saturation   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetImageSaturationR(int32_t &saturation);

    /**
     * @brief  Get the saturation range of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] range   Refer to UvcParamRange.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeImageSaturationR(UvcParamRange &range);

    /**
     * @brief  Set the sharp of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [in] sharp   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetImageSharpR(int32_t sharp);

    /**
     * @brief  Get the sharp of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] sharp   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetImageSharpR(int32_t &sharp);

    /**
     * @brief  Get the sharp range of the image.
     * @category  tiny, tiny4k, tiny2, tail air, meet, meet4k.
     * @param  [out] range   Refer to UvcParamRange.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeImageSharpR(UvcParamRange &range);

    enum DevAutoFocusType
    {
        DevAutoFocusAutoSelect = 0,        /// device select
        DevAutoFocusAFC,                   /// continue auto focus
        DevAutoFocusAFS,                   /// single auto focus
        DevAutoFocusMF,                    /// manual focus
    };

    /**
     * @brief  Set the auto focus type of the camera.
     * @category  tail air.
     * @param  [in] focus_type   Refer to DevAutoFocusType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetAutoFocusModeR(DevAutoFocusType focus_type);

    /**
     * @brief  Get the auto focus type of the camera.
     * @category  tail air.
     * @param  [out] focus_type   Refer to DevAutoFocusType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetAutoFocusModeR(DevAutoFocusType &focus_type);

    /**
     * @brief  Set the focus motor position of the camera.
     * @category  tail air.
     * @param  [in] focus_pos   Valid range: 0~100.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetFocusPosR(int32_t focus_pos);

    /**
     * @brief  Get the focus motor position of the camera.
     * @category  tail air.
     * @param  [out] focus_pos   Focus motor position.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetFocusPosR(int32_t &focus_pos);

    enum DevAFCType
    {
        DevAFCCenter = 0,           /// center focus
        DevAFCFace,                 /// face focus
        DevAFCAiObject,             /// object focus
        DevAFCForeground, 	    /// foreground focus
    };

    /**
     * @brief  Set the continue auto focus type of camera.
     * @category  tail air.
     * @param  [in] afc_type   Refer to DevAFCType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetAFCTrackModeR(DevAFCType afc_type);

    /**
     * @brief  Get the continue auto focus type of camera.
     * @category  tail, air, tail2.
     * @param  [out] face_focus   Refer to DevAFCType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetAFCTrackModeR(DevAFCType &afc_type);

    /**
     * @brief  Enable the auto focus mode or not, and set the manual focus value if auto focus mode is disabled.
     * @category  tiny, tiny4k, tiny2, meet, meet4k, tail air.
     * @param  [in] focus        Focus motor position if auto_focus is false;
     * @param  [in] auto_focus   True->Enable the auto focus mode; False->Disable the auto focus mode.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetFocusAbsolute(int32_t focus, bool auto_focus);

    /**
     * @brief  Get the auto focus mode, and get the manual focus value if auto focus mode is disabled.
     * @category  tiny, tiny4k, tiny2, meet, meet4k, tail air, tail2.
     * @param  [out] focus        Focus motor position if auto focus mode is disabled.
     * @param  [out] auto_focus   The auto focus mode.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetFocusAbsolute(int32_t &focus, bool &auto_focus);

    /**
     * @brief  Get the manual focus position range.
     * @category  tiny, tiny4k, tiny2, meet, meet4k, tail air.
     * @param  [out] range   Refer to UvcParamRange.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraGetRangeFocusAbsolute(UvcParamRange &range);


    /// tiny2, tail air, remo protocol v3 /// Camera other setting /////////////////////////////////////////////////////

    enum DevPowerCtrlActionType
    {
        DevPowerCtrlResume = 0,
        DevPowerCtrlSuspend,
        DevPowerCtrlReboot,
        DevPowerCtrlPowerOff,
        DevPowerCtrlMediaExit,
    };

    /**
     * @brief  Resume the device, suspend the device, reboot the device, power off the device, and so on.
     * @category  tail air.
     * @param  [in] fs_type        Refer to DevSDCardFilesystemType.
     * @param  [in] cluster_size   Cluster size of the SD card must be power of 2.
     *                             0->auto select by the device.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t cameraSetPowerCtrlActionR(DevPowerCtrlActionType action);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// tiny2, tail air, remo protocol v3 /// System management setting ////////////////////////////////////////////////
    /**
     * @brief  Set indicator state
     * @category  tail air, tail2.
     * @param  [in] state_id.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t sysMgSetIndicatorStateR(uint8_t state_id);

    /**
     * @brief  Enable the buzzer.
     * @category  tail air, tail2.
     * @param  [in] state_id.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t sysMgClearIndicatorStateR(uint8_t state_id);

    /**
     * @brief  Enable the buzzer.
     * @category  tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t sysMgSetBuzzerEnabledR();

    /**
     * @brief  Disable the buzzer.
     * @category  tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t sysMgSetBuzzerDisabledR();

    enum DevBuzzerStatus
    {
        DevBuzzerStatusDisabled = 0,
        DevBuzzerStatusEnabled,
        DevBuzzerStatusError = 0xFF,
    };

    /**
     * @brief  Get the buzzer enabled or not.
     * @category  tail air.
     * @param  [in] buzzer_status  Buzzer status, refer to DevBuzzerStatus.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t sysMgGetBuzzerEnabledR(DevBuzzerStatus &buzzer_status);

    /**
     * @brief  Set the name of device.
     * @category  tail air.
     * @param  [in] name   The name of device.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t sysMgSetDeviceNameR(const std::string &name);

    /**
     * @brief  Get the name of device.
     * @category  tail air.
     * @param  [out] name  The name of device.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t sysMgGetDeviceNameR(std::string &name);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// tiny2, remo protocol v3 /// Ai setting ////////////////////////////////////////////////

    enum AiTargetType
    {
        AiTargetAuto = -1,          /// auto select
        AiTargetPerson = 0,
        AiTargetCat,
        AiTargetDog,
        AiTargetHorse,
        AiTargetAnimal = 100,             /// cat dog and cat
    };


    /**
     * @brief  Select the tracking target in the specified box.
     * @category  tail air.
     * @param  [in] x_min   x-coordinate min, 0.0~1.0.
     * @param  [in] x_max   x-coordinate max, 0.0~1.0.
     * @param  [in] y_min   y-coordinate min, 0.0~1.0.
     * @param  [in] y_max   y-coordinate max, 0.0~1.0.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetSelectTargetByBox(float x_min, float y_min, float x_max, float y_max);

    /**
     * @brief  Track the biggest target in the image. The target type can be specified.
     * @category  tail air.
     * @param  [in] target_type   Target type, refer to AiTargetType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetSelectBiggestTarget(int32_t target_type = AiTargetAuto);

    /**
     * @brief  Track the target in the center of image. The target type can be specified.
     * @category  tail air.
     * @param  [in] target_type   Target type, refer to AiTargetType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetSelectCentralTarget(int32_t target_type = AiTargetAuto);

    /**
     * @brief  Adjust the video image according to the specified position.
     * @category  tail air.
     * @param  [in] x   0~1.
     * @param  [in] y   0~1.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetVideoCenter(float x, float y);

    /**
     * @brief  Cancel the selected tracking target.
     * @category  tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiDelSelectedTargetR();

    /**
     * @brief  Cancel auto group mode.
     * @category  tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiDelAutoGroupModeR();

    /**
     * @brief  Set Auto offset, only for horizontal offset.
     * @category  tail air.
     * @param  [out] enabled    false->off.
     *                          true->on.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetAutoOffset(bool enabled);

    /**
     * @brief  get auto offset enable.
     * @category  tail air.
     *  @param  [out] enabled    false->off.
     *                           true->on.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetAutoOffsetEnable(bool &enabled);

    /**
     * @brief  set horizontal offset.
     * @category  tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetHorizontalOffset(float offset);

    /**
     * @brief  get horizontal offset.
     * @category  tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetHorizontalOffset(float &offset);

    /**
     * @brief  set vertical offset.
     * @category  tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetVerticalOffset(float offset);

    /**
     * @brief  get vertical offset.
     * @category  tail air.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetVerticalOffset(float &offset);

    /**
     * @brief  Set the AI tracking mode enabled.
     * @category  tiny2 series, tinySE, tail air, tail2 and later products.
     * @param  [in] ai_mode   AiTrackNormal -> cancel ai mode, refer to AiTrackModeType.
     * @param  [in] enabled   true->enabled ai mode.
     *                        false->disabled ai mode.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetAiTrackModeEnabledR(AiTrackModeType ai_mode, bool enabled);

    /**
     * @brief  Set the track speed type of AI.
     * @category  tail air.
     * @param  [in] track_type   Refer to AiTrackSpeedType.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetTrackSpeedTypeR(AiTrackSpeedType track_type);

    ////////////////////////////////////////////////////////////////////////
    /// Only tail2 and later products, remo protocol v3 ///
    ////////////////////////////////////////////////////////////////////////

    enum DevTargetSelectionType
    {
        DevTargetSelectionTypeDelete = -1,      /// Delete the current target
        DevTargetSelectionTypeCenter = 0,       /// Select target by the center-first strategy
        DevTargetSelectionTypeLargest,          /// Select target by the largest target first strategy
        DevTargetSelectionTypeClicked,          /// Select target by clicked
        DevTargetSelectionTypeBox,              /// Select target by the box
    };

    enum DevTargetClassType
    {
        DevTargetClassTypeIgnored = -1,       /// This value can be set only when selection_type is DevTargetSelectionTypeDelete
        DevTargetClassTypeHuman = 0,          /// Target is human
        DevTargetClassTypeAnimal,             /// Target is animal
        DevTargetClassTypeCommon,             /// Common target, this value can be set only when selection_type is DevTargetSelectionTypeBox
        DevTargetClassTypeHumanOrAnimal = 100,/// Target is human or animal
        DevTargetClassTypeAll = 101,          /// No target limit
    };

    enum DevTargetZoomType
    {
        DevTargetZoomTypeIgnored = -1,         /// At this time, view_type is used to determine the zoom method, and view_type must be valid and not DevTargetViewTypeIgnored
        DevTargetZoomTypeNormal = 0,           /// Close auto zoom
        DevTargetZoomTypeFullBody,             /// Open auto zoom, use full-body mode when class_type is DevTargetClassTypeHuman
        DevTargetZoomTypeHalfBody,             /// Open auto zoom, use half-body mode when class_type is DevTargetClassTypeHuman
        DevTargetZoomTypeCloseUp,              /// Open auto zoom, use close-up mode
        DevTargetZoomTypeCustomized,           /// Open auto zoom, use custom mode
        DevTargetZoomTypeGropHeadless,         /// Use headless cropping mode when class_type is DevTargetClassTypeHuman, for air and tiny series
        DevTargetZoomTypeGropLowerBody,        /// Use lower-body cropping mode when class_type is DevTargetClassTypeHuman, for air and tiny series
        DevTargetZoomTypeAdaptive = 99,        /// Automatically adapt the camera zoom type
    };

    enum DevCustomizedZoomType
    {
        DevCustomizedZoomTypeDisable = 0,
        DevCustomizedZoomTypeCloseUp,
        DevCustomizedZoomTypeHalf,
        DevCustomizedZoomTypeP7,
        DevCustomizedZoomTypeP9,
        DevCustomizedZoomTypeFull,
        DevCustomizedZoomTypeP16,
        DevCustomizedZoomTypeP24
    };

    /// only for air series
    enum DevTargetViewType
    {
        /// Ignore, when selection_type is not equal to DevTargetSelectionTypeDelete, one of zoom_type and view_type must be valid, and both cannot be ignore
        DevTargetViewTypeIgnored = -2,
        DevTargetViewTypeManual = -1,
        DevTargetViewTypeFull = 0,
        /// When class_type is DevTargetClassTypeHuman
        DevTargetViewTypeFullBody,
        /// When class_type is DevTargetClassTypeHuman
        DevTargetViewTypeHalfBody,
        DevTargetViewTypeClassUp,
        DevTargetViewTypeHeadless,
        DevTargetViewTypeLowerBody,
        DevTargetViewTypeTargetAuto,
        DevTargetViewTypeNotTargetAuto,
        DevTargetViewTypeGroup,
        DevTargetViewTypeTrace,
    };

    /// Device target selection definition
    typedef struct
    {
        int16_t selection_type; /// Refer to DevSelectionType
        int16_t class_type;     /// Refer to DevTargetClassType
        int16_t zoom_type;      /// Refer to DevTargetZoomType
        int16_t view_type;      /// Refer to DevTargetViewType
        union {
            struct
            {
                float x;
                float y;
            } point; /// range is 0-1, selection_type is DevTargetSelectionTypeClicked
            struct
            {
                float x_min;
                float y_min;
                float x_max;
                float y_max;
            } roi; /// x_min/y_min is top-left, x_max/y_max is bottom-right, range 0-1, selection_type is DevTargetSelectionTypeBox
        } location;
    } DevTargetSelection;

    /**
     * @brief  Unified target selection command.
     * @category  tail2 and later products.
     * @param  [in] target_selection   Refer to DevTargetSelection
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetSelectedTargetR(const DevTargetSelection &target_selection);

    /**
     * @brief  Set zoom type of the current target.
     * @category  tail2 and later products.
     * @param  [in] zoom_type   Refer to DevTargetZoomType
     * @param  [in] customized_zoom_type   Refer to DevCustomizedZoomType
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetTargetZoomTypeR(DevTargetZoomType zoom_type, DevCustomizedZoomType customized_zoom_type = DevCustomizedZoomTypeDisable);

    /**
     * @brief  Set view type of the current target.
     * @category  tail2 and later products.
     * @param  [in] view_type   Refer to DevTargetViewType
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetTargetViewTypeR(DevTargetViewType view_type);

    enum DevGestureParaType
    {
        DevGestureParaTypeGesture = 0,      /// gesture function, bool, true: open, false: close
        DevGestureParaTypeTargetSelection,  /// target selection gesture, bool, true: open, false: close
        DevGestureParaTypeZoom,             /// zoom gesture, bool, true: open, false: close
        DevGestureParaTypeDynamicZoom,      /// dynamic zoom gesture, bool, true: open, false: close
        DevGestureParaTypeRecord,           /// record gesture, bool, true: open, false: close
        DevGestureParaTypeSnapshot,         /// snapshot gesture, bool, true: open, false: close
        DevGestureParaTypeRolling,          /// dynamic rolling gesture, bool, true: open, false: close
        DevGestureParaTypeMirror,           /// dynamic zoom dir, bool, true: open, false: close
        DevGestureParaTypeZoomFactor,       /// hand pose zoom ratio, float, 100x
    };

    /**
     * @brief  Set gesture function parameters.
     * @category  tail2 and later products.
     * @param  [in] type           Refer to DevGestureParaType.
     * @param  [in] parameter      Determine the data type and value through type.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetGestureParaR(DevGestureParaType type, bool parameter);
    int32_t aiSetGestureParaR(DevGestureParaType type, float parameter);

    /**
     * @brief  Get gesture function parameters.
     * @category  tail2 and later products.
     * @param  [in] type           Refer to DevGestureParaType.
     * @param  [out] parameter     Determine the data type and value through type.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetGestureParaR(DevGestureParaType type, bool &parameter);
    int32_t aiGetGestureParaR(DevGestureParaType type, float &parameter);

    enum DevGestureTrackParaType
    {
        DevGestureTrackParaTypePanMin = 0,       /// pan limit min, float
        DevGestureTrackParaTypePanMax,           /// pan limit max, float
        DevGestureTrackParaTypePitchMin,         /// pitch limit min, float
        DevGestureTrackParaTypePitchMax,         /// pitch limit max, float
        DevGestureTrackParaTypeHandType,         /// left-hand tracking, bool, true: left-hand, false: right-hand
        DevGestureTrackParaTypeTrackSpeed,       /// track speed, int
        DevGestureTrackParaTypePanEnabled,       /// pan enabled, bool, true: enabled, false: disabled
        DevGestureTrackParaTypePitchEnabled,     /// pitch enabled, bool, true: enabled, false: disabled
        DevGestureTrackParaTypeRestSeconds,      /// How many seconds after reaching the boundary will it trigger back to the initial position, float
    };

    /**
     * @brief  Set gesture track function parameters.
     * @category  tail2 and later products.
     * @param  [in] type           Refer to DevGestureTrackParaType.
     * @param  [in] parameter      Determine the data type and value through type.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetGestureTrackParaR(DevGestureTrackParaType type, bool parameter);
    int32_t aiSetGestureTrackParaR(DevGestureTrackParaType type, float parameter);
    int32_t aiSetGestureTrackParaR(DevGestureTrackParaType type, int parameter);

    /**
     * @brief  Get gesture track function parameters.
     * @category  tail2 and later products.
     * @param  [in] type           Refer to DevGestureTrackParaType.
     * @param  [out] parameter     Determine the data type and value through type.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetGestureTrackParaR(DevGestureTrackParaType type, bool &parameter);
    int32_t aiGetGestureTrackParaR(DevGestureTrackParaType type, float &parameter);
    int32_t aiGetGestureTrackParaR(DevGestureTrackParaType type, int &parameter);

    enum DevGimbalParaType
    {
        DevGimbalParaTypePanMin = 0,       /// pan limit min, float
        DevGimbalParaTypePanMax,           /// pan limit max, float
        DevGimbalParaTypePitchMin,         /// pitch limit min, float
        DevGimbalParaTypePitchMax,         /// pitch limit max, float
        DevGimbalParaTypePanReverse,       /// pan control reverse, bool, true: reverse, false: positive
        DevGimbalParaTypePresetSpeed,      /// preset speed, float, [0.1, 0.2, 0.4, 0.6, 0.8]
        DevGimbalParaTypeRollBias,         /// roll bias, float
    };

    /**
     * @brief  Set gimbal function parameters.
     * @category  tail2 and later products.
     * @param  [in] type           Refer to DevGimbalParaType.
     * @param  [in] parameter      Determine the data type and value through type.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetGimbalParaR(DevGimbalParaType type, bool parameter);

    /**
     * @brief  Set gimbal function parameters.
     * @category  Tail2 and later products fully support,
     *            when type is DevGimbalParaTypePresetSpeed, Tail air supports
     * @param  [in] type           Refer to DevGimbalParaType.
     * @param  [in] parameter      Determine the data type and value through type.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiSetGimbalParaR(DevGimbalParaType type, float parameter);

    /**
     * @brief  Get gimbal function parameters.
     * @category  tail2 and later products.
     * @param  [in] type           Refer to DevGimbalParaType.
     * @param  [out] parameter     Determine the data type and value through type.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetGimbalParaR(DevGimbalParaType type, bool &parameter);

    /**
     * @brief  Set gimbal function parameters.
     * @category  Tail2 and later products fully support,
     *            when type is DevGimbalParaTypePresetSpeed, Tail air supports
     * @param  [in] type           Refer to DevGimbalParaType.
     * @param  [out] parameter      Determine the data type and value through type.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetGimbalParaR(DevGimbalParaType type, float &parameter);

    enum DevControlTargetType
    {
        DevControlTargetTypeHuman = 0, /// human parameter
        DevControlTargetTypeAnimal,    /// animal parameter
        DevControlTargetTypeObject,    /// general object parameter
    };

    enum DevTrackType
    {
        DevTrackTypeNormal = 0,
        DevTrackTypeLimitArea,
    };

    enum DevGimCtrlMode
    {
        DevGimCtrlModeNormal = 0,
        DevGimCtrlModePRO,
    };

    enum DevGimCtrlSpeedMode
    {
        DevGimCtrlSpeedModeSuperLazy = 0,
        DevGimCtrlSpeedModeLazy,
        DevGimCtrlSpeedModeSlow,
        DevGimCtrlSpeedModeFast,
        DevGimCtrlSpeedModeCrazy,
        DevGimCtrlSpeedModeCustom = 100
    };

    enum DevControlParaType
    {
        DevControlParaTypeMotion = 0,             /// bool, false: standard mode, true: motion track mode
        DevControlParaTypeForeTrack,              /// bool, false: normal-tracking, true: tracking fore-target if lost
        DevControlParaTypeComposition,            /// bool, false: normal-composition, true: auto adjust composition offset using nearby humans
        DevControlParaTypeTrackerType,            /// int, refer to DevTrackType
        DevControlParaTypeGimCtrlMode,            /// int, refer to DevGimCtrlMode
        DevControlParaTypeGimCtrlSpeedMode,       /// int, refer to DevGimCtrlSpeedMode
        DevControlParaTypePanGainAdaptive,        /// bool, false: pan gain is not adaptive, true: pan gain is adaptive
        DevControlParaTypePanGainValue,           /// float, customized value of pan gain
        DevControlParaTypePanLocked,              /// bool, false: pan is not locked, true: pan is locked
        DevControlParaTypePitchGainAdaptive,      /// bool, false: pitch gain is not adaptive, true: pitch gain is adaptive
        DevControlParaTypePitchGainValue,         /// float, customized value of pitch gain
        DevControlParaTypePitchLocked,            /// bool, false: pitch is not locked, true: pitch is locked
        DevControlParaTypeAutoZoomCustomized,     /// int, auto zoom scale customized
        DevControlParaTypeAutoZoomSpeed,          /// int, auto zoom speed
        DevControlParaTypeOffsetAdaptiveX,        /// bool, false: horizontal composition offset is not adaptive, true: horizontal composition offset is adaptive
        DevControlParaTypeOffsetX,                /// float, the value of horizontal composition offset
        DevControlParaTypeOffsetAdaptiveY,        /// bool, false: vertical composition offset is not adaptive, true: vertical composition offset is adaptive
        DevControlParaTypeOffsetY,                /// float, the value of vertical composition offset
        DevControlParaTypeLimitAutoSelection,     /// bool, when limit tracking started, false: don't automatically select people, true: automatically select people
        DevControlParaTypeLimitPanMin,            /// float, pan limited min value
        DevControlParaTypeLimitPanMax,            /// float, pan limited max value
        DevControlParaTypeLimitPitchMin,          /// float, pitch limited min value
        DevControlParaTypeLimitPitchMax,          /// float, pitch limited max value
    };

    /**
     * @brief  Set control function parameters.
     * @category  tail2 and later products.
     * @param  [in] target_type        Refer to DevControlTargetType.
     * @param  [in] para_type          Refer to DevControlParaType.
     * @param  [in] parameter          Determine the data type and value through type.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
    */
    int32_t aiSetControlParaR(DevControlTargetType target_type, DevControlParaType para_type, bool parameter);
    int32_t aiSetControlParaR(DevControlTargetType target_type, DevControlParaType para_type, float parameter);
    int32_t aiSetControlParaR(DevControlTargetType target_type, DevControlParaType para_type, int parameter);

    /**
     * @brief  Get control function parameters.
     * @category  tail2 and later products.
     * @param  [in] target_type        Refer to DevControlTargetType.
     * @param  [in] para_type          Refer to DevControlParaType.
     * @param  [out] parameter         Determine the data type and value through type.
     * @param  [in] fault              When para_type >= DevControlParaTypeLimitPanMin and para_type <= DevControlParaTypeLimitPitchMax,
     *                                 get default value.
     * @return  RM_RET_OK for success, RM_RET_ERR for failed.
     */
    int32_t aiGetControlParaR(DevControlTargetType target_type, DevControlParaType para_type, bool &parameter);
    int32_t aiGetControlParaR(DevControlTargetType target_type, DevControlParaType para_type, float &parameter, bool fault = false);
    int32_t aiGetControlParaR(DevControlTargetType target_type, DevControlParaType para_type, int &parameter);

private:

    DECLARE_PRIVATE(Device)

    DevicePrivate *d_ptr;

    friend class DevUpgradePrivate;

    friend class DevicesPrivate;
};

#endif // OBSBOT_DEV_HPP
