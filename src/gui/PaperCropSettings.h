#ifndef PAPERCROPSETTINGS_H
#define PAPERCROPSETTINGS_H

#include <algorithm>
#include <cmath>

enum class PaperCropMode {
    Off = 0,
    Manual = 1,
    Automatic = 2
};

struct PaperCropSettings {
    PaperCropMode mode = PaperCropMode::Off;
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    bool isValid() const
    {
        const auto validMargin = [](float value) {
            return std::isfinite(value) && value >= 0.0f && value <= 0.45f;
        };
        return validMargin(left) && validMargin(top)
            && validMargin(right) && validMargin(bottom)
            && left + right <= 0.9f
            && top + bottom <= 0.9f;
    }

    PaperCropSettings normalized() const
    {
        PaperCropSettings result = *this;
        result.left = std::clamp(result.left, 0.0f, 0.45f);
        result.top = std::clamp(result.top, 0.0f, 0.45f);
        result.right = std::clamp(result.right, 0.0f, 0.45f);
        result.bottom = std::clamp(result.bottom, 0.0f, 0.45f);
        if (result.left + result.right > 0.9f) {
            result.right = 0.9f - result.left;
        }
        if (result.top + result.bottom > 0.9f) {
            result.bottom = 0.9f - result.top;
        }
        return result;
    }

    bool operator==(const PaperCropSettings &other) const
    {
        constexpr float epsilon = 0.0001f;
        return mode == other.mode
            && std::abs(left - other.left) < epsilon
            && std::abs(top - other.top) < epsilon
            && std::abs(right - other.right) < epsilon
            && std::abs(bottom - other.bottom) < epsilon;
    }

    bool operator!=(const PaperCropSettings &other) const { return !(*this == other); }
};

#endif // PAPERCROPSETTINGS_H
