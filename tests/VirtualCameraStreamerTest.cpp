#include "VirtualCameraStreamer.h"

#include <QCoreApplication>
#include <QSize>
#include <iostream>

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
    bool passed = true;

    passed &= check(
        !VirtualCameraStreamer::normalizedOutputSize(QSize()).isValid(),
        "match-mode sentinel remains invalid");
    passed &= check(
        VirtualCameraStreamer::normalizedOutputSize(QSize(640, 481))
            == QSize(640, 481),
        "even YUYV widths remain unchanged");
    passed &= check(
        VirtualCameraStreamer::normalizedOutputSize(QSize(641, 481))
            == QSize(642, 481),
        "odd match-preview widths are normalized to an even YUYV width");

    QImage oddMatchFrame(641, 481, QImage::Format_RGBA8888);
    oddMatchFrame.fill(Qt::red);
    const QImage preparedMatch =
        VirtualCameraStreamer::prepareFrameForOutput(oddMatchFrame);
    passed &= check(preparedMatch.size() == QSize(642, 481),
                    "match-mode frame preparation normalizes an odd input width");
    passed &= check(preparedMatch.format() == QImage::Format_RGB888,
                    "prepared match-mode frame is ready for YUYV conversion");

    const QImage preparedForced = VirtualCameraStreamer::prepareFrameForOutput(
        oddMatchFrame, QSize(321, 241));
    passed &= check(preparedForced.size() == QSize(322, 241),
                    "forced odd width is normalized in the actual frame path");

    VirtualCameraStreamer streamer;
    streamer.setForcedResolution(QSize(641, 481));
    passed &= check(streamer.forcedResolution() == QSize(642, 481),
                    "public forced resolution reports the normalized size");
    streamer.setForcedResolution(QSize());
    passed &= check(!streamer.forcedResolution().isValid(),
                    "match-preview mode clears the forced resolution");

    return passed ? 0 : 1;
}
