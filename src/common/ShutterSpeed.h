#ifndef SHUTTERSPEED_H
#define SHUTTERSPEED_H

#include <algorithm>
#include <cmath>
#include <string>

/**
 * @brief Backend-neutral manual shutter speed.
 *
 * Shutter speeds are stored as the SDK's DevShutterTimeType index (e.g. 33 = 1/30s), which
 * is what cameraSetExposureAbsolute() takes. The V4L2 fallback's V4L2_CID_EXPOSURE_ABSOLUTE
 * uses 100 µs units instead, so convert at that boundary. That way the UI and settings.conf
 * always mean the same shutter speed, whichever path is connected.
 */
namespace ShutterSpeed {

constexpr int kFirstIndex = 9;    // DevShutterTime_1_8000
constexpr int kLastIndex = 45;    // DevShutterTime_1_2
constexpr int kDefaultIndex = 33; // DevShutterTime_1_30

struct Speed {
    double denominator; // Shutter speed is 1/denominator seconds
    const char *label;
};

// One entry per index, starting at kFirstIndex
constexpr Speed kSpeeds[] = {
    {8000, "1/8000s"}, {6400, "1/6400s"}, {5000, "1/5000s"}, {4000, "1/4000s"},
    {3200, "1/3200s"}, {2500, "1/2500s"}, {2000, "1/2000s"}, {1600, "1/1600s"},
    {1250, "1/1250s"}, {1000, "1/1000s"}, {800, "1/800s"}, {640, "1/640s"},
    {500, "1/500s"}, {400, "1/400s"}, {320, "1/320s"}, {240, "1/240s"},
    {200, "1/200s"}, {160, "1/160s"}, {120, "1/120s"}, {100, "1/100s"},
    {80, "1/80s"}, {60, "1/60s"}, {50, "1/50s"}, {40, "1/40s"},
    {30, "1/30s"}, {25, "1/25s"}, {20, "1/20s"}, {15, "1/15s"},
    {12.5, "1/12.5s"}, {10, "1/10s"}, {8, "1/8s"}, {6.25, "1/6.25s"},
    {5, "1/5s"}, {4, "1/4s"}, {3, "1/3s"}, {2.5, "1/2.5s"},
    {2, "1/2s"}
};
static_assert(sizeof(kSpeeds) / sizeof(kSpeeds[0]) == kLastIndex - kFirstIndex + 1,
              "One entry per shutter index");

inline bool isValidIndex(int index)
{
    return index >= kFirstIndex && index <= kLastIndex;
}

// Human-readable shutter speed, e.g. "1/30s"
inline std::string label(int index)
{
    if (index == 0) {
        return "Auto";
    }
    if (!isValidIndex(index)) {
        return std::to_string(index);
    }
    return kSpeeds[index - kFirstIndex].label;
}

// Shutter index -> V4L2_CID_EXPOSURE_ABSOLUTE value (100 µs units)
inline int toV4l2Exposure(int index)
{
    index = std::clamp(index, kFirstIndex, kLastIndex);
    return std::max(1, static_cast<int>(std::lround(10000.0 / kSpeeds[index - kFirstIndex].denominator)));
}

// V4L2_CID_EXPOSURE_ABSOLUTE value (100 µs units) -> nearest shutter index
inline int fromV4l2Exposure(int exposure)
{
    exposure = std::max(1, exposure);
    int nearest = kFirstIndex;
    double nearestDistance = HUGE_VAL;
    for (int index = kFirstIndex; index <= kLastIndex; ++index) {
        // Shutter speeds are spaced logarithmically, so compare ratios rather than differences
        const double distance = std::fabs(std::log(exposure * kSpeeds[index - kFirstIndex].denominator / 10000.0));
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = index;
        }
    }
    return nearest;
}

// Same, but keeps the selected index while it still maps to the exposure: the fastest shutter
// speeds share 100 µs steps, so reading back must not replace the speed the user picked
inline int fromV4l2Exposure(int exposure, int selectedIndex)
{
    if (isValidIndex(selectedIndex) && toV4l2Exposure(selectedIndex) == exposure) {
        return selectedIndex;
    }
    return fromV4l2Exposure(exposure);
}

} // namespace ShutterSpeed

#endif // SHUTTERSPEED_H
