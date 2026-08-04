#include "CameraController.h"

#include <QCoreApplication>
#include <iostream>

struct CameraControllerTestAccess
{
    static void forceUnconfirmedV4l2Connection(CameraController &controller)
    {
        controller.m_connected = true;
        controller.m_v4l2Only = true;
    }

    static void forceUnconfirmedSdkConnection(CameraController &controller)
    {
        controller.m_connected = true;
        controller.m_v4l2Only = false;
        controller.m_manualMovementAuthorized = false;
        controller.m_device.reset();
    }
};

namespace {

bool check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    CameraController controller;
    CameraControllerTestAccess::forceUnconfirmedV4l2Connection(controller);

    bool passed = true;
    passed &= check(!controller.setPanTilt(0.25, -0.25),
                    "V4L2 fallback rejects pan/tilt without ownership proof");
    passed &= check(!controller.adjustPan(0.1),
                    "V4L2 fallback rejects relative pan without ownership proof");
    passed &= check(!controller.adjustTilt(0.1),
                    "V4L2 fallback rejects relative tilt without ownership proof");
    passed &= check(!controller.centerView(),
                    "V4L2 fallback rejects centering without ownership proof");
    passed &= check(!controller.setZoom(1.5),
                    "V4L2 fallback rejects zoom without ownership proof");
    passed &= check(!controller.setFocusAbsolute(40, false),
                    "V4L2 fallback rejects manual focus without ownership proof");
    passed &= check(!controller.setFocusAbsolute(0, true),
                    "V4L2 fallback rejects autofocus changes without ownership proof");

    CameraControllerTestAccess::forceUnconfirmedSdkConnection(controller);
    passed &= check(!controller.setPanTilt(-0.4, 0.2),
                    "SDK pan/tilt rejects unconfirmed tracking ownership");
    passed &= check(!controller.adjustPan(0.1),
                    "SDK relative movement rejects unconfirmed tracking ownership");
    passed &= check(!controller.centerView(),
                    "SDK centering rejects unconfirmed tracking ownership");
    passed &= check(!controller.setZoom(1.25),
                    "SDK optical zoom rejects unconfirmed tracking ownership");
    passed &= check(!controller.setFocusAbsolute(55, false),
                    "SDK manual focus rejects unconfirmed tracking ownership");

    return passed ? 0 : 1;
}
