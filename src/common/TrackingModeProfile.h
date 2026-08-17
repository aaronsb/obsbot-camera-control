#ifndef TRACKINGMODEPROFILE_H
#define TRACKINGMODEPROFILE_H

#include <array>
#include <cstddef>
#include <string_view>

/**
 * Tiny 2 focus behavior attached to an AI tracking mode.
 *
 * The retained manual position remains available when Face or Continuous
 * focus is selected, so switching to Manual does not jump to an arbitrary
 * motor position.
 */
enum class TrackingFocusPolicy {
    Face,
    Continuous,
    Manual
};

struct TrackingModeProfile {
    TrackingFocusPolicy focusPolicy = TrackingFocusPolicy::Continuous;
    int manualFocusPosition = 50;
    bool autoZoom = false;
    int trackSpeed = 2;
};

inline bool operator==(const TrackingModeProfile &left,
                       const TrackingModeProfile &right)
{
    return left.focusPolicy == right.focusPolicy
        && left.manualFocusPosition == right.manualFocusPosition
        && left.autoZoom == right.autoZoom
        && left.trackSpeed == right.trackSpeed;
}

inline bool operator!=(const TrackingModeProfile &left,
                       const TrackingModeProfile &right)
{
    return !(left == right);
}

using Tiny2TrackingModeProfiles = std::array<TrackingModeProfile, 5>;

struct TrackingIntentState {
    bool enabled = false;
    int aiMode = 0;
    int aiSubMode = 0;
    TrackingModeProfile profile{};
};

inline bool operator==(const TrackingIntentState &left,
                       const TrackingIntentState &right)
{
    return left.enabled == right.enabled
        && left.aiMode == right.aiMode
        && left.aiSubMode == right.aiSubMode
        && left.profile == right.profile;
}

inline bool operator!=(const TrackingIntentState &left,
                       const TrackingIntentState &right)
{
    return !(left == right);
}

inline int tiny2TrackingModeProfileIndex(int aiMode)
{
    // Persisted/SDK Tiny 2 mode values are Group=1 through Desk=5.
    switch (aiMode) {
    case 1: return 0;
    case 2: return 1;
    case 3: return 2;
    case 4: return 3;
    case 5: return 4;
    default: return -1;
    }
}

inline const char *tiny2TrackingModeName(std::size_t index)
{
    static constexpr std::array<const char *, 5> names{
        "group", "human", "hand", "whiteboard", "desk"
    };
    return index < names.size() ? names[index] : "";
}

inline Tiny2TrackingModeProfiles defaultTiny2TrackingModeProfiles()
{
    // Group starts face-oriented. Human is migrated from legacy settings when
    // a configuration is loaded. Hand, Whiteboard, and Desk deliberately use
    // scene autofocus so they do not search for a face that may not exist.
    return {{
        {TrackingFocusPolicy::Face, 50, false, 2},
        {TrackingFocusPolicy::Continuous, 50, false, 2},
        {TrackingFocusPolicy::Continuous, 50, false, 2},
        {TrackingFocusPolicy::Continuous, 50, false, 2},
        {TrackingFocusPolicy::Continuous, 50, false, 2}
    }};
}

inline bool isValidTrackingModeProfile(const TrackingModeProfile &profile)
{
    const bool focusPolicyValid =
        profile.focusPolicy == TrackingFocusPolicy::Face
        || profile.focusPolicy == TrackingFocusPolicy::Continuous
        || profile.focusPolicy == TrackingFocusPolicy::Manual;
    return focusPolicyValid
        && profile.manualFocusPosition >= 0
        && profile.manualFocusPosition <= 100
        && profile.trackSpeed >= 0
        && profile.trackSpeed <= 5;
}

inline const char *trackingFocusPolicyToken(TrackingFocusPolicy policy)
{
    switch (policy) {
    case TrackingFocusPolicy::Face: return "face";
    case TrackingFocusPolicy::Continuous: return "continuous";
    case TrackingFocusPolicy::Manual: return "manual";
    }
    return "continuous";
}

inline bool parseTrackingFocusPolicy(std::string_view token,
                                     TrackingFocusPolicy &policy)
{
    if (token == "face") {
        policy = TrackingFocusPolicy::Face;
        return true;
    }
    if (token == "continuous") {
        policy = TrackingFocusPolicy::Continuous;
        return true;
    }
    if (token == "manual") {
        policy = TrackingFocusPolicy::Manual;
        return true;
    }
    return false;
}

inline TrackingModeProfile legacyTrackingModeProfile(
    bool faceFocus, int focus, bool autoZoom, int trackSpeed)
{
    TrackingModeProfile profile;
    profile.focusPolicy = faceFocus
        ? TrackingFocusPolicy::Face
        : (focus >= 0 ? TrackingFocusPolicy::Manual
                      : TrackingFocusPolicy::Continuous);
    profile.manualFocusPosition = focus >= 0 ? focus : 50;
    profile.autoZoom = autoZoom;
    profile.trackSpeed = trackSpeed;
    return profile;
}

inline TrackingIntentState applyAutomaticPaperCropTrackingPolicy(
    int paperCropMode,
    bool tiny2Capabilities,
    TrackingIntentState current,
    const Tiny2TrackingModeProfiles &modeProfiles)
{
    constexpr int kAutomaticPaperCropMode = 2;
    constexpr int kDeskAiMode = 5;
    if (!tiny2Capabilities || paperCropMode != kAutomaticPaperCropMode) {
        return current;
    }

    current.enabled = true;
    current.aiMode = kDeskAiMode;
    current.aiSubMode = 0;
    current.profile = modeProfiles[4];
    return current;
}

#endif // TRACKINGMODEPROFILE_H
