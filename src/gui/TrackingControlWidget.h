#ifndef TRACKINGCONTROLWIDGET_H
#define TRACKINGCONTROLWIDGET_H

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QTimer>
#include <QSignalBlocker>
#include "CameraController.h"
#include "XYPad.h"

struct TrackingControlWidgetTestAccess;

/**
 * @brief Widget for camera tracking control (automatic or manual PTZ)
 * Contains auto-framing toggle and manual PTZ controls (mutually exclusive)
 */
class TrackingControlWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrackingControlWidget(CameraController *controller, QWidget *parent = nullptr);

    void updateFromState(const CameraController::CameraState &state);
    void setV4l2Mode(bool v4l2Only);
    using TrackingState = TrackingIntentState;
    struct TrackingApplyResult {
        bool accepted = false;
        bool confirmationPending = false;
        quint64 intentGeneration = 0;
    };

    bool isTrackingEnabled() const { return m_trackingCheckBox->isChecked(); }
    bool isManualControlEnabled() const;
    TrackingState trackingState() const;
    bool matchesTrackingState(const TrackingState &expected) const;
    void setTrackingEnabled(bool enabled);
    TrackingApplyResult applyTrackingState(const TrackingState &state);
    void setTrackingStatePresentation(const TrackingState &state);
    void setModeProfiles(const Tiny2TrackingModeProfiles &profiles);
    const Tiny2TrackingModeProfiles &modeProfiles() const {
        return m_modeProfiles;
    }
    TrackingModeProfile modeProfile(int aiMode) const;
    TrackingState trackingStateForMode(int aiMode) const;
    void setActiveTrackingProfile(const TrackingModeProfile &profile);
    TrackingModeProfile activeTrackingProfile() const;
    void setManualFocusPosition(int position);
    bool hasTiny2Capabilities() const { return m_tiny2Capabilities; }
    void setAiMode(int mode);
    void setHumanSubMode(int subMode);
    void setAutoZoomEnabled(bool enabled);
    void setTrackSpeed(int speedMode);
    void setAudioAutoGain(bool enabled);
    int currentAiMode() const { return m_modeCombo->currentData().toInt(); }
    int currentHumanSubMode() const { return m_humanSubModeCombo->currentData().toInt(); }
    bool isAutoZoomEnabled() const { return m_autoZoomCheckBox->isChecked(); }
    int currentTrackSpeed() const { return m_speedCombo->currentData().toInt(); }
    bool isAudioAutoGainEnabled() const { return m_audioGainCheckBox->isChecked(); }
    void setMirrored(bool mirrored);
    bool isInvertControls() const { return m_invertControlsCheckBox->isChecked(); }

signals:
    void mirrorToggled(bool mirrored);
    void trackingIntentEdited(
        const TrackingState &state, bool updateModeProfile);
    void audioAutoGainIntentEdited(bool enabled);
    void panTiltIntentEdited(double pan, double tilt);
    void zoomIntentEdited(double zoom);
    void focusIntentEdited(int focus);

private slots:
    void onTrackingToggled(bool checked);
    void onModeChanged(int index);
    void onHumanSubModeChanged(int index);
    void onAutoZoomToggled(bool checked);
    void onSpeedChanged(int index);
    void onProfileFocusPolicyChanged(int index);
    void onProfileFocusPositionChanged(int value);
    void onAudioGainToggled(bool checked);
    void onManualControlToggled(bool checked);
    void onDeskModeToggled(bool checked);
    void onTrackingIntentStarted(quint64 intentGeneration);
    void onTrackingStateConfirmationPending(
        bool trackingEnabled, quint64 intentGeneration);
    void onTrackingStateConfirmationFailed(
        bool trackingEnabled, quint64 intentGeneration);
    void onTrackingStateConfirmationUncertain(quint64 intentGeneration);
    void onTrackingStateConfirmed(
        bool trackingEnabled, quint64 intentGeneration);
    void onTrackingOwnershipObserved(bool trackingEnabled);

    // Manual PTZ control slots
    void onXYPadChanged(float x, float y);
    void onZoomChanged(int value);
    void onFocusChanged(int value);

private:
    CameraController *m_controller;
    friend struct TrackingControlWidgetTestAccess;
    QCheckBox *m_trackingCheckBox;
    QComboBox *m_modeCombo;
    QComboBox *m_humanSubModeCombo;
    QCheckBox *m_autoZoomCheckBox;
    QCheckBox *m_deskModeCheckBox;
    QComboBox *m_speedCombo;
    QComboBox *m_profileFocusPolicyCombo;
    QSlider *m_profileFocusSlider;
    QLabel *m_profileFocusLabel;
    QCheckBox *m_audioGainCheckBox;
    QWidget *m_advancedContainer;
    bool m_userInitiated;  // Track if change was user-initiated
    bool m_manualOverrideActive;  // Explicit operator intent; camera status cannot clear it
    bool m_manualControlAuthorized;  // True only after AI ownership is known to be off
    bool m_manualConfirmationPending;
    bool m_trackingConfirmationPending;
    // Retained after completion so delayed signals from older intents cannot
    // mutate the current ownership decision.
    quint64 m_pendingTrackingIntentGeneration;
    quint64 m_highestTrackingIntentGeneration;
    bool m_trackingIntentResolved;
    QTimer *m_commandTimer;  // Debounce timer for command completion
    QTimer *m_profileFocusTimer;  // Coalesces profile focus slider edits
    bool m_v4l2Only;  // No SDK ownership confirmation is available
    bool m_tiny2Capabilities; // flag for advanced tracking features
    int m_lastAcceptedAiMode = Device::AiWorkModeNone;
    int m_lastAcceptedHumanSubMode = Device::AiSubModeNormal;
    Tiny2TrackingModeProfiles m_modeProfiles =
        defaultTiny2TrackingModeProfiles();
    TrackingModeProfile m_activeTrackingProfile{};

    // Manual PTZ controls
    XYPad *m_xyPad;
    QCheckBox *m_invertControlsCheckBox;
    QCheckBox *m_mirrorCheckBox;
    QCheckBox *m_manualControlCheckBox;
    QGroupBox *m_ptzGroupBox;
    QSlider *m_zoomSlider;
    QLabel *m_zoomLabel;
    QSlider *m_focusSlider;
    QLabel *m_focusLabel;
    QLabel *m_positionLabel;
    QWidget *m_ptzContainer;

    // Unified command throttle — coalesces all manual control changes
    QTimer *m_controlThrottle;
    float m_pendingPan = 0;
    float m_pendingTilt = 0;
    int m_pendingZoom = 100;
    int m_pendingFocus = 50;
    bool m_dirtyPanTilt = false;
    bool m_dirtyZoom = false;
    bool m_dirtyFocus = false;
    void flushPendingCommands();
    void scheduleFlush();
    void submitCurrentTrackingProfile();
    void persistActiveTrackingIntent(bool updateModeProfile);
    void storeActiveProfileForCurrentMode();

    void updateTiny2Visibility();
    void updatePTZControlsState();

    QGroupBox *m_trackingGroupBox;
};

#endif // TRACKINGCONTROLWIDGET_H
