#include "V4l2Backend.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <cstring>
#include <linux/videodev2.h>

V4l2Backend::V4l2Backend() = default;

V4l2Backend::~V4l2Backend()
{
    close();
}

bool V4l2Backend::open(const std::string &devicePath)
{
    close();
    if (!isObsbotCaptureDevice(devicePath))
        return false;
    int fd = ::open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0)
        return false;
    ::close(fd);
    m_devicePath = devicePath;
    m_configured = true;
    return true;
}

void V4l2Backend::close()
{
    m_configured = false;
    m_devicePath.clear();
}

int V4l2Backend::openDevice() const
{
    if (m_devicePath.empty())
        return -1;
    return ::open(m_devicePath.c_str(), O_RDWR | O_NONBLOCK);
}

bool V4l2Backend::isObsbotCaptureDevice(const std::string &devicePath)
{
    int fd = ::open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0)
        return false;

    struct v4l2_capability cap{};
    const bool queried = ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
    ::close(fd);
    if (!queried)
        return false;

    const std::string card(reinterpret_cast<const char *>(cap.card));
    const uint32_t capabilities = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
        ? cap.device_caps : cap.capabilities;
    const bool isVideoCapture =
        (capabilities & (V4L2_CAP_VIDEO_CAPTURE
                         | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) != 0;
    return isVideoCapture
        && card.find("OBSBOT") != std::string::npos
        && card.find("Virtual Camera") == std::string::npos;
}

std::string V4l2Backend::findObsbotDevice()
{
    DIR *dir = opendir("/dev");
    if (!dir)
        return {};

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        if (name.find("video") != 0)
            continue;

        const std::string path = "/dev/" + name;
        if (isObsbotCaptureDevice(path)) {
            closedir(dir);
            return path;
        }
    }
    closedir(dir);
    return {};
}

std::string V4l2Backend::cardName() const
{
    int fd = openDevice();
    if (fd < 0)
        return {};
    struct v4l2_capability cap{};
    std::string name;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
        name = reinterpret_cast<const char *>(cap.card);
    ::close(fd);
    return name;
}

int V4l2Backend::getControl(uint32_t id)
{
    int fd = openDevice();
    if (fd < 0)
        return -1;
    struct v4l2_control ctrl{};
    ctrl.id = id;
    int result = -1;
    if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) == 0)
        result = ctrl.value;
    ::close(fd);
    return result;
}

bool V4l2Backend::setControl(uint32_t id, int value)
{
    int fd = openDevice();
    if (fd < 0)
        return false;
    struct v4l2_control ctrl{};
    ctrl.id = id;
    ctrl.value = value;
    bool ok = ioctl(fd, VIDIOC_S_CTRL, &ctrl) == 0;
    ::close(fd);
    return ok;
}

V4l2Backend::ControlRange V4l2Backend::queryRange(uint32_t id)
{
    ControlRange range{};
    int fd = openDevice();
    if (fd < 0)
        return range;

    struct v4l2_queryctrl qctrl{};
    qctrl.id = id;
    if (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
        range.min = qctrl.minimum;
        range.max = qctrl.maximum;
        range.step = qctrl.step == 0 ? 1 : qctrl.step;
        range.defaultValue = qctrl.default_value;
        range.valid = true;
    }
    ::close(fd);
    return range;
}

bool V4l2Backend::setBrightness(int value) { return setControl(V4L2_CID_BRIGHTNESS, value); }
int V4l2Backend::getBrightness() { return getControl(V4L2_CID_BRIGHTNESS); }
V4l2Backend::ControlRange V4l2Backend::getBrightnessRange() { return queryRange(V4L2_CID_BRIGHTNESS); }

bool V4l2Backend::setContrast(int value) { return setControl(V4L2_CID_CONTRAST, value); }
int V4l2Backend::getContrast() { return getControl(V4L2_CID_CONTRAST); }
V4l2Backend::ControlRange V4l2Backend::getContrastRange() { return queryRange(V4L2_CID_CONTRAST); }

bool V4l2Backend::setSaturation(int value) { return setControl(V4L2_CID_SATURATION, value); }
int V4l2Backend::getSaturation() { return getControl(V4L2_CID_SATURATION); }
V4l2Backend::ControlRange V4l2Backend::getSaturationRange() { return queryRange(V4L2_CID_SATURATION); }

bool V4l2Backend::setWhiteBalanceAuto(bool enabled) { return setControl(V4L2_CID_AUTO_WHITE_BALANCE, enabled ? 1 : 0); }
bool V4l2Backend::getWhiteBalanceAuto() { return getControl(V4L2_CID_AUTO_WHITE_BALANCE) == 1; }
bool V4l2Backend::setWhiteBalanceTemperature(int kelvin) { return setControl(V4L2_CID_WHITE_BALANCE_TEMPERATURE, kelvin); }
int V4l2Backend::getWhiteBalanceTemperature() { return getControl(V4L2_CID_WHITE_BALANCE_TEMPERATURE); }
V4l2Backend::ControlRange V4l2Backend::getWhiteBalanceTemperatureRange() { return queryRange(V4L2_CID_WHITE_BALANCE_TEMPERATURE); }

bool V4l2Backend::setPanAbsolute(int value) { return setControl(V4L2_CID_PAN_ABSOLUTE, value); }
int V4l2Backend::getPanAbsolute() { return getControl(V4L2_CID_PAN_ABSOLUTE); }
V4l2Backend::ControlRange V4l2Backend::getPanRange() { return queryRange(V4L2_CID_PAN_ABSOLUTE); }

bool V4l2Backend::setTiltAbsolute(int value) { return setControl(V4L2_CID_TILT_ABSOLUTE, value); }
int V4l2Backend::getTiltAbsolute() { return getControl(V4L2_CID_TILT_ABSOLUTE); }
V4l2Backend::ControlRange V4l2Backend::getTiltRange() { return queryRange(V4L2_CID_TILT_ABSOLUTE); }

bool V4l2Backend::setZoomAbsolute(int value) { return setControl(V4L2_CID_ZOOM_ABSOLUTE, value); }
int V4l2Backend::getZoomAbsolute() { return getControl(V4L2_CID_ZOOM_ABSOLUTE); }
V4l2Backend::ControlRange V4l2Backend::getZoomRange() { return queryRange(V4L2_CID_ZOOM_ABSOLUTE); }

bool V4l2Backend::setAutoExposure(bool automatic)
{
    return setControl(V4L2_CID_EXPOSURE_AUTO, automatic ? V4L2_EXPOSURE_AUTO : V4L2_EXPOSURE_MANUAL);
}

bool V4l2Backend::setExposureAbsolute(int value) { return setControl(V4L2_CID_EXPOSURE_ABSOLUTE, value); }

bool V4l2Backend::setAutoFocus(bool enabled) { return setControl(V4L2_CID_FOCUS_AUTO, enabled ? 1 : 0); }
bool V4l2Backend::getAutoFocus() { return getControl(V4L2_CID_FOCUS_AUTO) == 1; }
bool V4l2Backend::setFocusAbsolute(int value) { return setControl(V4L2_CID_FOCUS_ABSOLUTE, value); }
int V4l2Backend::getFocusAbsolute() { return getControl(V4L2_CID_FOCUS_ABSOLUTE); }
