#include "CameraController.h"
#include "CameraSettingsWidget.h"
#include "PTZControlWidget.h"
#include "TrackingControlWidget.h"

#include <QApplication>
#include <QCoreApplication>
#include <iostream>
#include <vector>

struct PTZControlWidgetTestAccess
{
    static PTZControlWidget::PresetState trackingPreset(bool autoZoom = false)
    {
        return {
            true,
            0.0,
            0.0,
            1.0,
            true,
            true,
            Device::AiWorkModeHuman,
            Device::AiSubModeNormal,
            autoZoom,
            TrackingFocusPolicy::Continuous,
            50,
            Device::AiTrackSpeedStandard,
            {}
        };
    }

    static void arm(PTZControlWidget &widget,
                    quint64 recallGeneration,
                    quint64 trackingIntentGeneration)
    {
        widget.m_recallGeneration = recallGeneration;
        widget.m_completionQueued = false;
        widget.m_pendingRecall = PTZControlWidget::PendingRecall{
            trackingPreset(), recallGeneration, trackingIntentGeneration};
    }

    static bool completionQueued(const PTZControlWidget &widget)
    {
        return widget.m_completionQueued;
    }

    static bool finish(PTZControlWidget &widget,
                       const PTZControlWidget::PresetState &preset)
    {
        return widget.finishPresetRecall(preset);
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
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication application(argc, argv);

    bool passed = true;
    CameraController controller;
    PTZControlWidget widget(&controller);

    CameraController::CameraInfo info{};
    emit controller.cameraConnected(info);

    std::vector<bool> completions;
    QObject::connect(&widget, &PTZControlWidget::presetRecallFinished,
                     [&completions](bool success) {
                         completions.push_back(success);
                     });

    PTZControlWidgetTestAccess::arm(widget, 10, 100);
    emit controller.trackingStateConfirmed(true, 100);
    passed &= check(PTZControlWidgetTestAccess::completionQueued(widget),
                    "matching confirmation queues scene continuation");
    QCoreApplication::processEvents();
    passed &= check(completions.size() == 1 && completions.back(),
                    "uncancelled continuation completes once");
    passed &= check(!widget.hasPendingRecall(),
                    "completed continuation clears pending state");

    PTZControlWidgetTestAccess::arm(widget, 20, 200);
    emit controller.trackingStateConfirmed(true, 200);
    passed &= check(PTZControlWidgetTestAccess::completionQueued(widget),
                    "second matching confirmation queues continuation");
    widget.cancelPendingRecall();
    QCoreApplication::processEvents();
    passed &= check(completions.size() == 1,
                    "explicit cancellation invalidates queued continuation");
    passed &= check(!widget.hasPendingRecall(),
                    "cancellation clears all pending phases");

    PTZControlWidgetTestAccess::arm(widget, 30, 300);
    emit controller.trackingStateConfirmed(true, 300);
    emit controller.trackingIntentStarted(301);
    passed &= check(completions.size() == 2 && !completions.back(),
                    "newer tracking intent fails the superseded recall");
    QCoreApplication::processEvents();
    passed &= check(completions.size() == 2,
                    "superseded queued callback cannot complete later");

    PTZControlWidgetTestAccess::arm(widget, 40, 350);
    emit controller.trackingStateConfirmationUncertain(350);
    passed &= check(completions.size() == 3 && !completions.back(),
                    "uncertain ownership terminates a pending recall fail-closed");
    passed &= check(!widget.hasPendingRecall(),
                    "uncertain confirmation cannot leave D-Bus waiting on a recall");

    PTZControlWidgetTestAccess::arm(widget, 45, 500);
    emit controller.trackingIntentStarted(499);
    passed &= check(widget.hasPendingRecall() && completions.size() == 3,
                    "older tracking start cannot cancel a newer pending recall");
    emit controller.trackingStateConfirmed(true, 500);
    QCoreApplication::processEvents();
    passed &= check(completions.size() == 4 && completions.back(),
                    "pending recall still completes under its matching generation");

    TrackingControlWidget tracking(&controller);
    widget.setTrackingControlWidget(&tracking);
    tracking.setAiMode(Device::AiWorkModeHuman);
    tracking.setHumanSubMode(Device::AiSubModeNormal);
    tracking.setAutoZoomEnabled(false);
    tracking.setActiveTrackingProfile({
        TrackingFocusPolicy::Continuous,
        50,
        false,
        Device::AiTrackSpeedStandard
    });
    tracking.setTrackingEnabled(true);
    emit controller.trackingIntentStarted(400);
    emit controller.trackingStateConfirmed(true, 400);

    int sceneIntentCount = 0;
    bool sceneTracking = false;
    bool scenePositionApplied = true;
    TrackingModeProfile sceneProfile;
    PaperCropSettings sceneCrop;
    QObject::connect(
        &widget, &PTZControlWidget::sceneIntentApplied,
        [&sceneIntentCount, &sceneTracking, &scenePositionApplied,
         &sceneProfile, &sceneCrop](
            const TrackingIntentState &trackingState,
            bool positionApplied, double, double, double,
            const PaperCropSettings &paperCrop) {
            ++sceneIntentCount;
            sceneTracking = trackingState.enabled;
            scenePositionApplied = positionApplied;
            sceneProfile = trackingState.profile;
            sceneCrop = paperCrop;
        });

    auto matching = PTZControlWidgetTestAccess::trackingPreset(false);
    matching.paperCrop.mode = PaperCropMode::Manual;
    matching.paperCrop.left = 0.1f;
    passed &= check(PTZControlWidgetTestAccess::finish(widget, matching),
                    "exact confirmed tracking scene can finish");
    passed &= check(sceneIntentCount == 1 && sceneTracking
                        && !scenePositionApplied
                        && sceneProfile == tracking.activeTrackingProfile()
                        && sceneCrop == matching.paperCrop,
                    "successful scene emits one complete profile+crop intent without unapplied PTZ");

    auto wrongAutoZoom = matching;
    wrongAutoZoom.autoZoom = true;
    passed &= check(!PTZControlWidgetTestAccess::finish(widget, wrongAutoZoom),
                    "auto-zoom mismatch blocks scene completion");

    auto wrongFocusPolicy = matching;
    wrongFocusPolicy.focusPolicy = TrackingFocusPolicy::Face;
    passed &= check(
        !PTZControlWidgetTestAccess::finish(widget, wrongFocusPolicy),
        "focus-policy mismatch blocks scene completion");

    auto wrongMode = matching;
    wrongMode.aiMode = Device::AiWorkModeDesk;
    passed &= check(!PTZControlWidgetTestAccess::finish(widget, wrongMode),
                    "AI-mode mismatch blocks scene completion");
    passed &= check(sceneIntentCount == 1,
                    "failed scene checks emit no persisted intent");

    CameraSettingsWidget cameraSettings(&controller);
    bool hdrIntent = false;
    int hdrIntentCount = 0;
    QObject::connect(&cameraSettings, &CameraSettingsWidget::hdrIntentEdited,
                     [&hdrIntent, &hdrIntentCount](bool enabled) {
                         hdrIntent = enabled;
                         ++hdrIntentCount;
                     });
    const bool invoked = QMetaObject::invokeMethod(
        &cameraSettings, "onHDRToggled", Q_ARG(bool, true));
    passed &= check(invoked && hdrIntentCount == 1 && hdrIntent,
                    "camera settings expose exact requested values for persistence");

    return passed ? 0 : 1;
}
