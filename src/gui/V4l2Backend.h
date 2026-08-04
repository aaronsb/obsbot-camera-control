#ifndef V4L2BACKEND_H
#define V4L2BACKEND_H

#include <cstdint>
#include <string>
#include <linux/videodev2.h>

class V4l2Backend
{
public:
    struct ControlRange {
        int min = 0;
        int max = 0;
        int step = 1;
        int defaultValue = 0;
        bool valid = false;
    };

    V4l2Backend();
    ~V4l2Backend();

    bool open(const std::string &devicePath);
    void close();
    bool isOpen() const { return m_configured; }
    std::string devicePath() const { return m_devicePath; }

    static bool isObsbotCaptureDevice(const std::string &devicePath);
    static std::string findObsbotDevice();
    std::string cardName() const;

    int getControl(uint32_t id);
    bool setControl(uint32_t id, int value);
    ControlRange queryRange(uint32_t id);

    bool setBrightness(int value);
    int getBrightness();
    ControlRange getBrightnessRange();

    bool setContrast(int value);
    int getContrast();
    ControlRange getContrastRange();

    bool setSaturation(int value);
    int getSaturation();
    ControlRange getSaturationRange();

    bool setWhiteBalanceAuto(bool enabled);
    bool getWhiteBalanceAuto();
    bool setWhiteBalanceTemperature(int kelvin);
    int getWhiteBalanceTemperature();
    ControlRange getWhiteBalanceTemperatureRange();

    bool setPanAbsolute(int value);
    int getPanAbsolute();
    ControlRange getPanRange();

    bool setTiltAbsolute(int value);
    int getTiltAbsolute();
    ControlRange getTiltRange();

    bool setZoomAbsolute(int value);
    int getZoomAbsolute();
    ControlRange getZoomRange();

    bool setAutoExposure(bool automatic);
    bool setExposureAbsolute(int value);

    bool setAutoFocus(bool enabled);
    bool getAutoFocus();
    bool setFocusAbsolute(int value);
    int getFocusAbsolute();

private:
    bool m_configured = false;
    std::string m_devicePath;
    int openDevice() const;
};

#endif // V4L2BACKEND_H
