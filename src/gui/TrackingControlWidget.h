#ifndef TRACKINGCONTROLWIDGET_H
#define TRACKINGCONTROLWIDGET_H

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QTimer>
#include <QSignalBlocker>
#include "CameraController.h"
#include "XYPad.h"

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
    bool isTrackingEnabled() const { return m_trackingCheckBox->isChecked(); }
    void setTrackingEnabled(bool enabled) {
        m_trackingCheckBox->blockSignals(true);
        m_trackingCheckBox->setChecked(enabled);
        m_trackingCheckBox->setText(enabled
            ? "Disable Auto-Framing" : "Enable Auto-Framing");
        m_trackingCheckBox->blockSignals(false);
    }
    void setAiMode(int mode);
    void setHumanSubMode(int subMode);
    void setAutoZoomEnabled(bool enabled);
    void setTrackSpeed(int speedMode);
    void setTrackingStyle(int style);
    void setAudioAutoGain(bool enabled);
    int currentAiMode() const { return m_modeCombo->currentData().toInt(); }
    int currentHumanSubMode() const { return m_humanSubModeCombo->currentData().toInt(); }
    bool isAutoZoomEnabled() const { return m_autoZoomCheckBox->isChecked(); }
    int currentTrackSpeed() const { return m_speedCombo->currentData().toInt(); }
    int currentTrackingStyle() const { return m_trackingStyleCombo->currentData().toInt(); }
    bool isAudioAutoGainEnabled() const { return m_audioGainCheckBox->isChecked(); }
    bool isFaceFocusEnabled() const { return m_faceFocusCheckBox->isChecked(); }
    void setFaceFocusEnabled(bool enabled);
    bool isInvertControls() const { return m_invertControlsCheckBox->isChecked(); }

private slots:
    void onTrackingToggled(bool checked);
    void onModeChanged(int index);
    void onHumanSubModeChanged(int index);
    void onAutoZoomToggled(bool checked);
    void onSpeedChanged(int index);
    void onAudioGainToggled(bool checked);
    void onTrackingStyleChanged(int index);
    void onFaceFocusToggled(bool checked);

    // Manual PTZ control slots
    void onXYPadChanged(float x, float y);
    void onZoomChanged(int value);
    void onFocusChanged(int value);

private:
    CameraController *m_controller;
    QPushButton *m_trackingCheckBox;
    QComboBox *m_modeCombo;
    QComboBox *m_humanSubModeCombo;
    QCheckBox *m_autoZoomCheckBox;
    QComboBox *m_speedCombo;
    QCheckBox *m_audioGainCheckBox;
    QWidget *m_advancedContainer;
    QWidget *m_originalTinyContainer;
    QComboBox *m_trackingStyleCombo;
    bool m_userInitiated;  // Track if change was user-initiated
    QTimer *m_commandTimer;  // Debounce timer for command completion
    bool m_tiny2Capabilities; // flag for advanced tracking features

    // Manual PTZ controls
    XYPad *m_xyPad;
    QCheckBox *m_invertControlsCheckBox;
    QSlider *m_zoomSlider;
    QLabel *m_zoomLabel;
    QPushButton *m_fovButtons[3];
    QSlider *m_focusSlider;
    QLabel *m_focusLabel;
    QCheckBox *m_faceFocusCheckBox;
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

    void updateTiny2Visibility();
    void updatePTZControlsState();

    QGroupBox *m_trackingGroupBox;
};

#endif // TRACKINGCONTROLWIDGET_H
