#include "CameraController.h"
#include "TrackingControlWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QSlider>
#include <QTimer>
#include <iostream>

struct TrackingControlWidgetTestAccess
{
    static void setTiny2Capabilities(TrackingControlWidget &widget, bool enabled)
    {
        widget.m_tiny2Capabilities = enabled;
        widget.updateTiny2Visibility();
    }

    static void selectMode(TrackingControlWidget &widget, int mode)
    {
        const int index = widget.m_modeCombo->findData(mode);
        if (index >= 0) {
            widget.m_modeCombo->setCurrentIndex(index);
        }
    }

    static void selectHumanSubMode(TrackingControlWidget &widget, int subMode)
    {
        const int index = widget.m_humanSubModeCombo->findData(subMode);
        if (index >= 0) {
            widget.m_humanSubModeCombo->setCurrentIndex(index);
        }
    }

    static void setPendingGeneration(TrackingControlWidget &widget,
                                     quint64 generation,
                                     bool resolved)
    {
        widget.m_pendingTrackingIntentGeneration = generation;
        widget.m_trackingIntentResolved = resolved;
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

QGroupBox *manualGroup(TrackingControlWidget &widget)
{
    return widget.findChild<QGroupBox *>(QStringLiteral("manualControlGroup"));
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication application(argc, argv);

    bool passed = true;
    CameraController controller;
    TrackingControlWidget widget(&controller);

    int trackingIntentCount = 0;
    bool lastTrackingIntent = true;
    bool lastModeProfileUpdate = false;
    TrackingModeProfile lastProfile;
    QObject::connect(
        &widget, &TrackingControlWidget::trackingIntentEdited,
        [&trackingIntentCount, &lastTrackingIntent,
         &lastModeProfileUpdate, &lastProfile](
            const TrackingControlWidget::TrackingState &state,
            bool updateModeProfile) {
            ++trackingIntentCount;
            lastTrackingIntent = state.enabled;
            lastModeProfileUpdate = updateModeProfile;
            lastProfile = state.profile;
        });

    auto *manual = widget.findChild<QCheckBox *>(QStringLiteral("manualControlCheckBox"));
    auto *tracking = widget.findChild<QCheckBox *>(QStringLiteral("trackingCheckBox"));
    auto *guard = widget.findChild<QTimer *>(QStringLiteral("trackingCommandTimer"));
    auto *controls = manualGroup(widget);
    passed &= check(manual && tracking && guard && controls,
                    "tracking controls expose stable test identities");
    if (!manual || !tracking || !guard || !controls) {
        return 1;
    }

    widget.setTrackingEnabled(true);
    manual->setChecked(true);
    passed &= check(trackingIntentCount >= 1 && !lastTrackingIntent,
                    "manual toggle emits exact tracking-off persistence intent");
    passed &= check(!widget.isManualControlEnabled() && widget.isTrackingEnabled(),
                    "failed tracking-off request rolls the UI back");
    passed &= check(!controls->isEnabled(),
                    "failed tracking-off request keeps manual controls disabled");

    // Configured/UI intent alone must not authorize movement. A fresh camera
    // confirmation is the only transition that unlocks manual controls.
    widget.setTrackingEnabled(false);
    passed &= check(!widget.isManualControlEnabled() && !widget.isTrackingEnabled(),
                    "unconfirmed manual intent keeps movement locked");
    passed &= check(!controls->isEnabled(),
                    "manual controls remain disabled before confirmation");

    emit controller.trackingIntentStarted(1);
    emit controller.trackingStateConfirmationPending(false, 1);
    emit controller.trackingStateConfirmed(false, 1);
    passed &= check(widget.isManualControlEnabled() && !widget.isTrackingEnabled(),
                    "fresh tracking-off confirmation enables manual positioning");
    passed &= check(controls->isEnabled(),
                    "confirmed off state authorizes PTZ, zoom, and focus");

    CameraController::CameraState staleTrackingState{};
    staleTrackingState.autoFramingEnabled = true;
    staleTrackingState.aiMode = Device::AiWorkModeHuman;
    staleTrackingState.aiSubMode = Device::AiSubModeUpperBody;
    staleTrackingState.zoom = 1.4;
    staleTrackingState.manualFocusValue = 55;
    staleTrackingState.pan = 0.25;
    staleTrackingState.tilt = -0.2;

    // Expire the UI guard, then deliver repeated stale tracking/PTZ emissions.
    // The first update clears m_userInitiated; the following updates exercise
    // the persistent manual-intent latch.
    guard->stop();
    widget.updateFromState(staleTrackingState);
    widget.updateFromState(staleTrackingState);
    staleTrackingState.zoom = 1.6;
    staleTrackingState.manualFocusValue = 60;
    staleTrackingState.pan = -0.3;
    widget.updateFromState(staleTrackingState);

    passed &= check(widget.isManualControlEnabled() && !widget.isTrackingEnabled(),
                    "stale tracking status cannot clear explicit manual intent");
    passed &= check(controls->isEnabled(),
                    "PTZ, zoom, and focus emissions leave manual controls enabled");

    emit controller.trackingIntentStarted(2);
    emit controller.trackingStateConfirmationPending(false, 2);
    emit controller.trackingStateConfirmationUncertain(2);
    passed &= check(!widget.isManualControlEnabled() && !widget.isTrackingEnabled(),
                    "temporary confirmation loss keeps manual intent but locks movement");
    passed &= check(!controls->isEnabled(),
                    "unknown AI ownership fails closed");
    emit controller.trackingOwnershipObserved(false);
    passed &= check(!widget.isManualControlEnabled() && !controls->isEnabled(),
                    "AI-off telemetry cannot clear current rollback uncertainty");
    emit controller.trackingStateConfirmed(false, 2);
    passed &= check(widget.isManualControlEnabled(),
                    "later fresh confirmation recovers the pending manual intent");

    emit controller.trackingIntentStarted(3);
    emit controller.trackingStateConfirmationPending(false, 3);
    emit controller.trackingStateConfirmationFailed(true, 3);
    passed &= check(!widget.isTrackingEnabled() && !widget.isManualControlEnabled(),
                    "failed manual confirmation keeps the manual latch fail-closed");
    passed &= check(!controls->isEnabled(),
                    "failed manual confirmation disables controls that AI still owns");

    emit controller.trackingIntentStarted(4);
    emit controller.trackingStateConfirmationPending(false, 4);
    emit controller.trackingStateConfirmed(false, 4);
    passed &= check(!widget.isTrackingEnabled() && widget.isManualControlEnabled(),
                    "a later fresh tracking-off confirmation safely restores manual control");
    passed &= check(controls->isEnabled(),
                    "fresh off confirmation authorizes PTZ, zoom, and focus");

    emit controller.trackingOwnershipObserved(true);
    passed &= check(!widget.isTrackingEnabled() && !widget.isManualControlEnabled(),
                    "fresh external tracking-on evidence revokes manual movement only");
    passed &= check(!controls->isEnabled(),
                    "external AI ownership locks PTZ without clearing manual intent");
    emit controller.trackingOwnershipObserved(false);
    passed &= check(!widget.isTrackingEnabled() && widget.isManualControlEnabled(),
                    "fresh external tracking-off evidence restores latched manual control");

    emit controller.trackingIntentStarted(5);
    emit controller.trackingStateConfirmationPending(true, 5);
    emit controller.trackingStateConfirmed(true, 5);
    passed &= check(widget.isTrackingEnabled() && !widget.isManualControlEnabled(),
                    "a newer confirmed tracking intent can end manual control");
    passed &= check(!controls->isEnabled(),
                    "confirmed tracking state disables manual controls");

    CameraController::CameraState staleOffState{};
    staleOffState.autoFramingEnabled = false;
    staleOffState.aiMode = Device::AiWorkModeNone;
    staleOffState.zoom = 1.0;
    guard->stop();
    widget.updateFromState(staleOffState);
    widget.updateFromState(staleOffState);
    passed &= check(widget.isTrackingEnabled() && !widget.isManualControlEnabled(),
                    "lagging tracking-off telemetry cannot replace a resolved tracking intent");
    passed &= check(!controls->isEnabled(),
                    "generic off telemetry never grants manual movement");

    // Older and duplicate terminal signals cannot rewrite the latest resolved
    // ownership decision.
    emit controller.trackingStateConfirmationPending(false, 4);
    emit controller.trackingStateConfirmationUncertain(4);
    emit controller.trackingStateConfirmationFailed(false, 4);
    emit controller.trackingStateConfirmed(false, 4);
    emit controller.trackingStateConfirmed(false, 5);
    emit controller.trackingIntentStarted(4);
    emit controller.trackingStateConfirmed(false, 4);
    passed &= check(widget.isTrackingEnabled() && !widget.isManualControlEnabled(),
                    "stale starts or replayed confirmations cannot re-authorize movement");
    passed &= check(!controls->isEnabled(),
                    "stale generations keep manual controls locked");

    // Even if a failed command tries to restore an older pending token, the
    // strict generation high-water mark prevents it from becoming current.
    emit controller.trackingIntentStarted(6);
    TrackingControlWidgetTestAccess::setPendingGeneration(widget, 5, false);
    emit controller.trackingStateConfirmed(false, 5);
    passed &= check(widget.isTrackingEnabled() && !widget.isManualControlEnabled(),
                    "a superseded generation cannot become current after rollback");
    TrackingControlWidgetTestAccess::setPendingGeneration(widget, 6, false);
    emit controller.trackingStateConfirmed(true, 6);

    // Original Tiny and Meet-off protocols confirm synchronously without a
    // separate pending phase; the exact started generation must still unlock.
    emit controller.trackingIntentStarted(7);
    emit controller.trackingStateConfirmed(false, 7);
    passed &= check(!widget.isTrackingEnabled() && widget.isManualControlEnabled(),
                    "matching synchronous tracking-off confirmation unlocks manual control");

    // A rejected Tiny 2 mode or sub-mode command must not leave a selection
    // visible that the disconnected controller did not accept.
    TrackingControlWidgetTestAccess::setTiny2Capabilities(widget, true);
    auto modeProfiles = defaultTiny2TrackingModeProfiles();
    modeProfiles[2] = {
        TrackingFocusPolicy::Manual, 77, false,
        Device::AiTrackSpeedFast};
    widget.setModeProfiles(modeProfiles);
    const auto handState =
        widget.trackingStateForMode(Device::AiWorkModeHand);
    passed &= check(
        handState.enabled
        && handState.aiMode == Device::AiWorkModeHand
        && handState.profile == modeProfiles[2],
        "each Tiny 2 mode exposes its independent complete profile");

    widget.setActiveTrackingProfile(modeProfiles[1]);
    widget.setAiMode(Device::AiWorkModeHuman);
    widget.setHumanSubMode(Device::AiSubModeNormal);
    widget.setTrackingEnabled(true);
    TrackingControlWidgetTestAccess::selectMode(widget, Device::AiWorkModeDesk);
    passed &= check(widget.currentAiMode() == Device::AiWorkModeHuman,
                    "failed AI-mode command restores the last accepted mode");
    TrackingControlWidgetTestAccess::selectHumanSubMode(
        widget, Device::AiSubModeUpperBody);
    passed &= check(widget.currentHumanSubMode() == Device::AiSubModeNormal,
                    "failed human sub-mode command restores the last accepted value");

    auto *focusPolicy = widget.findChild<QComboBox *>(
        QStringLiteral("trackingFocusPolicyCombo"));
    auto *profileFocus = widget.findChild<QSlider *>(
        QStringLiteral("trackingProfileFocusSlider"));
    passed &= check(focusPolicy && profileFocus,
                    "profile focus controls expose stable test identities");
    if (focusPolicy && profileFocus) {
        focusPolicy->setCurrentIndex(focusPolicy->findData(
            static_cast<int>(TrackingFocusPolicy::Manual)));
        profileFocus->setValue(82);
        passed &= check(
            lastModeProfileUpdate
            && lastProfile.focusPolicy == TrackingFocusPolicy::Manual
            && lastProfile.manualFocusPosition == 82
            && widget.modeProfile(Device::AiWorkModeHuman)
                .manualFocusPosition == 82,
            "focus edits update and emit only the active mode profile");
        passed &= check(
            widget.modeProfile(Device::AiWorkModeHand) == modeProfiles[2],
            "editing Human focus leaves Hand profile unchanged");
    }

    modeProfiles[1] = {
        TrackingFocusPolicy::Face, 82, false,
        Device::AiTrackSpeedStandard};
    widget.setModeProfiles(modeProfiles);
    widget.setTrackingStatePresentation({
        false,
        Device::AiWorkModeHuman,
        Device::AiSubModeNormal,
        {
            TrackingFocusPolicy::Manual, 17, false,
            Device::AiTrackSpeedStandard
        }
    });
    auto *autoZoom = widget.findChild<QCheckBox *>(
        QStringLiteral("trackingAutoZoomCheckBox"));
    passed &= check(autoZoom != nullptr,
                    "auto zoom exposes a stable profile test identity");
    if (autoZoom) {
        const int emissionsBeforeOffEdit = trackingIntentCount;
        autoZoom->setChecked(true);
        const auto activeOff = widget.trackingState();
        passed &= check(
            trackingIntentCount == emissionsBeforeOffEdit + 1
            && lastModeProfileUpdate
            && lastProfile.focusPolicy == TrackingFocusPolicy::Face
            && lastProfile.manualFocusPosition == 82
            && lastProfile.autoZoom
            && widget.modeProfile(Device::AiWorkModeHuman).autoZoom,
            "tracking-off profile edit persists the retained Human profile");
        passed &= check(
            !activeOff.enabled
            && activeOff.profile.focusPolicy
                == TrackingFocusPolicy::Manual
            && activeOff.profile.manualFocusPosition == 17
            && !activeOff.profile.autoZoom,
            "tracking-off camera intent remains separate and manual");
    }

    widget.setTrackingStatePresentation({
        false,
        Device::AiWorkModeNone,
        0,
        {
            TrackingFocusPolicy::Manual, 17, false,
            Device::AiTrackSpeedStandard
        }
    });
    passed &= check(
        autoZoom && !autoZoom->isEnabled()
        && focusPolicy && !focusPolicy->isEnabled()
        && profileFocus && !profileFocus->isEnabled(),
        "profile controls are disabled when no AI mode owns their edits");
    if (autoZoom) {
        const int emissionsBeforeInvalidOffEdit = trackingIntentCount;
        autoZoom->setChecked(true);
        passed &= check(
            !autoZoom->isChecked()
            && trackingIntentCount == emissionsBeforeInvalidOffEdit,
            "programmatic Off-mode profile edit is rolled back without persistence");
    }

    widget.setV4l2Mode(true);
    passed &= check(!widget.isManualControlEnabled() && !controls->isEnabled(),
                    "V4L2 fallback cannot authorize movement without tracking confirmation");
    passed &= check(!manual->isEnabled(),
                    "V4L2 fallback disables the unsupported ownership toggle");
    widget.setV4l2Mode(false);
    passed &= check(manual->isEnabled() && !widget.isManualControlEnabled(),
                    "returning to SDK mode requires a fresh ownership confirmation");

    return passed ? 0 : 1;
}
