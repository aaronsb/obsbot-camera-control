#include "TrackingControlWidget.h"
#include <QLabel>
#include <dev/dev.hpp>

TrackingControlWidget::TrackingControlWidget(CameraController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_userInitiated(false)
{
    // Create debounce timer for command completion
    m_commandTimer = new QTimer(this);
    m_commandTimer->setSingleShot(true);
    connect(m_commandTimer, &QTimer::timeout, this, [this]() {
        if (m_controller->isConnected())
            m_controller->saveConfig();
    });

    // Unified throttle for all manual controls (pan/tilt, zoom, focus)
    m_controlThrottle = new QTimer(this);
    m_controlThrottle->setSingleShot(true);
    m_controlThrottle->setInterval(100);
    connect(m_controlThrottle, &QTimer::timeout, this, &TrackingControlWidget::flushPendingCommands);

    m_tiny2Capabilities = m_controller->hasTiny2Capabilities();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 14, 8, 14);
    layout->setSpacing(14);

    m_trackingGroupBox = new QGroupBox("Face Tracking", this);
    m_trackingGroupBox->setFlat(true);
    QVBoxLayout *groupLayout = new QVBoxLayout(m_trackingGroupBox);
    groupLayout->setContentsMargins(16, 16, 16, 16);
    groupLayout->setSpacing(12);

    m_trackingCheckBox = new QPushButton("Enable", this);
    m_trackingCheckBox->setCheckable(true);
    m_trackingCheckBox->setStyleSheet("font-weight: 600; font-size: 14px;");
    connect(m_trackingCheckBox, &QPushButton::toggled,
            this, &TrackingControlWidget::onTrackingToggled);
    groupLayout->addWidget(m_trackingCheckBox);

    m_originalTinyContainer = new QWidget(this);
    QHBoxLayout *originalTinyLayout = new QHBoxLayout(m_originalTinyContainer);
    originalTinyLayout->setContentsMargins(0, 8, 0, 0);
    static const char *trackingStyleLabels[] = {
        "Standard", "Headroom", "Motion"
    };
    for (int style = 0; style < 3; ++style) {
        m_trackingStyleButtons[style] =
            new QPushButton(trackingStyleLabels[style], this);
        m_trackingStyleButtons[style]->setCheckable(true);
        connect(m_trackingStyleButtons[style], &QPushButton::clicked,
                this, [this, style]() { onTrackingStyleChanged(style); });
        originalTinyLayout->addWidget(m_trackingStyleButtons[style], 1);
    }
    setTrackingStyle(Device::AiVTrackStandard);
    groupLayout->addWidget(m_originalTinyContainer);

    // Advanced controls container (Tiny 2 family)
    m_advancedContainer = new QWidget(this);
    QVBoxLayout *advancedLayout = new QVBoxLayout(m_advancedContainer);
    advancedLayout->setContentsMargins(0, 12, 0, 0);
    advancedLayout->setSpacing(8);

    QHBoxLayout *modeLayout = new QHBoxLayout();
    QLabel *modeLabel = new QLabel("Tracking Mode", this);
    modeLabel->setStyleSheet("font-size: 11px; font-weight: 600;");
    m_modeCombo = new QComboBox(this);
    m_modeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_modeCombo->addItem("Off", Device::AiWorkModeNone);
    m_modeCombo->addItem("Group", Device::AiWorkModeGroup);
    m_modeCombo->addItem("Human (Auto)", Device::AiWorkModeHuman);
    m_modeCombo->addItem("Hand Tracking", Device::AiWorkModeHand);
    m_modeCombo->addItem("Whiteboard", Device::AiWorkModeWhiteBoard);
    m_modeCombo->addItem("Desk", Device::AiWorkModeDesk);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrackingControlWidget::onModeChanged);
    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(m_modeCombo, 1);
    advancedLayout->addLayout(modeLayout);

    QHBoxLayout *subModeLayout = new QHBoxLayout();
    QLabel *subModeLabel = new QLabel("Human Sub-Mode", this);
    subModeLabel->setStyleSheet("font-size: 11px; font-weight: 600;");
    m_humanSubModeCombo = new QComboBox(this);
    m_humanSubModeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_humanSubModeCombo->addItem("Normal", Device::AiSubModeNormal);
    m_humanSubModeCombo->addItem("Upper Body", Device::AiSubModeUpperBody);
    m_humanSubModeCombo->addItem("Close Up", Device::AiSubModeCloseUp);
    m_humanSubModeCombo->addItem("Headless", Device::AiSubModeHeadHide);
    m_humanSubModeCombo->addItem("Lower Body", Device::AiSubModeLowerBody);
    connect(m_humanSubModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrackingControlWidget::onHumanSubModeChanged);
    subModeLayout->addWidget(subModeLabel);
    subModeLayout->addWidget(m_humanSubModeCombo, 1);
    advancedLayout->addLayout(subModeLayout);

    m_autoZoomCheckBox = new QCheckBox("Enable AI Auto Zoom", this);
    connect(m_autoZoomCheckBox, &QCheckBox::toggled, this, &TrackingControlWidget::onAutoZoomToggled);
    m_autoZoomCheckBox->setStyleSheet("font-size: 11px;");
    advancedLayout->addWidget(m_autoZoomCheckBox);

    QHBoxLayout *speedLayout = new QHBoxLayout();
    QLabel *speedLabel = new QLabel("Tracking Speed", this);
    speedLabel->setStyleSheet("font-size: 11px; font-weight: 600;");
    m_speedCombo = new QComboBox(this);
    m_speedCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_speedCombo->addItem("Lazy", Device::AiTrackSpeedLazy);
    m_speedCombo->addItem("Slow", Device::AiTrackSpeedSlow);
    m_speedCombo->addItem("Standard", Device::AiTrackSpeedStandard);
    m_speedCombo->addItem("Fast", Device::AiTrackSpeedFast);
    m_speedCombo->addItem("Crazy", Device::AiTrackSpeedCrazy);
    m_speedCombo->addItem("Auto", Device::AiTrackSpeedAuto);
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrackingControlWidget::onSpeedChanged);
    speedLayout->addWidget(speedLabel);
    speedLayout->addWidget(m_speedCombo, 1);
    advancedLayout->addLayout(speedLayout);

    m_audioGainCheckBox = new QCheckBox("Enable Audio Auto Gain", this);
    m_audioGainCheckBox->setStyleSheet("font-size: 11px;");
    connect(m_audioGainCheckBox, &QCheckBox::toggled, this, &TrackingControlWidget::onAudioGainToggled);
    advancedLayout->addWidget(m_audioGainCheckBox);

    groupLayout->addWidget(m_advancedContainer);
    updateTiny2Visibility();

    layout->addWidget(m_trackingGroupBox);

    // Zoom remains available while auto-framing is enabled.
    QGroupBox *zoomGroupBox = new QGroupBox("Zoom", this);
    zoomGroupBox->setFlat(true);
    QVBoxLayout *zoomGroupLayout = new QVBoxLayout(zoomGroupBox);
    zoomGroupLayout->setContentsMargins(16, 16, 16, 16);
    zoomGroupLayout->setSpacing(8);

    QHBoxLayout *zoomLayout = new QHBoxLayout();
    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setMinimum(100);  // 1.00x
    m_zoomSlider->setMaximum(200);  // 2.00x
    m_zoomSlider->setValue(100);
    connect(m_zoomSlider, &QSlider::valueChanged,
            this, &TrackingControlWidget::onZoomChanged);
    zoomLayout->addWidget(m_zoomSlider, 1);
    m_zoomLabel = new QLabel("1.00x", this);
    m_zoomLabel->setMinimumWidth(40);
    zoomLayout->addWidget(m_zoomLabel);
    zoomGroupLayout->addLayout(zoomLayout);

    QHBoxLayout *fovLayout = new QHBoxLayout();
    static const char *fovLabels[] = {
        "Wide (86°)", "Medium (78°)", "Narrow (65°)"
    };
    for (int mode = 0; mode < 3; ++mode) {
        m_fovButtons[mode] = new QPushButton(fovLabels[mode], this);
        m_fovButtons[mode]->setCheckable(true);
        connect(m_fovButtons[mode], &QPushButton::clicked, this, [this, mode]() {
            m_userInitiated = true;
            m_controller->setFOV(mode);
            m_commandTimer->start(1000);
        });
        fovLayout->addWidget(m_fovButtons[mode]);
    }
    zoomGroupLayout->addLayout(fovLayout);
    layout->addWidget(zoomGroupBox);

    // Focus remains available while auto-framing is enabled.
    QGroupBox *focusGroupBox = new QGroupBox("Focus", this);
    focusGroupBox->setFlat(true);
    QVBoxLayout *focusGroupLayout = new QVBoxLayout(focusGroupBox);
    focusGroupLayout->setContentsMargins(16, 16, 16, 16);
    focusGroupLayout->setSpacing(8);

    m_faceFocusCheckBox = new QCheckBox("Face-based Auto Focus", this);
    m_faceFocusCheckBox->setToolTip("Optimize focus for faces");
    connect(m_faceFocusCheckBox, &QCheckBox::toggled,
            this, &TrackingControlWidget::onFaceFocusToggled);
    focusGroupLayout->addWidget(m_faceFocusCheckBox);

    QHBoxLayout *focusLayout = new QHBoxLayout();
    m_focusSlider = new QSlider(Qt::Horizontal, this);
    m_focusSlider->setMinimum(0);
    m_focusSlider->setMaximum(100);
    m_focusSlider->setValue(50);
    connect(m_focusSlider, &QSlider::valueChanged,
            this, &TrackingControlWidget::onFocusChanged);
    focusLayout->addWidget(m_focusSlider, 1);
    m_focusLabel = new QLabel("50", this);
    m_focusLabel->setMinimumWidth(40);
    focusLayout->addWidget(m_focusLabel);
    focusGroupLayout->addLayout(focusLayout);
    layout->addWidget(focusGroupBox);

    // Manual pan/tilt controls (disabled when auto-framing is enabled)
    m_ptzContainer = new QWidget(this);
    QVBoxLayout *ptzContainerLayout = new QVBoxLayout(m_ptzContainer);
    ptzContainerLayout->setContentsMargins(0, 0, 0, 0);
    ptzContainerLayout->setSpacing(0);

    QGroupBox *ptzGroupBox = new QGroupBox("Manual Control", this);
    ptzGroupBox->setFlat(true);
    QVBoxLayout *ptzGroupLayout = new QVBoxLayout(ptzGroupBox);
    ptzGroupLayout->setContentsMargins(16, 16, 16, 16);
    ptzGroupLayout->setSpacing(12);

    // Two-column layout: sliders left, XY pad right
    QHBoxLayout *columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(16);

    // Left column: manual-control options
    QVBoxLayout *leftColumn = new QVBoxLayout();
    leftColumn->addStretch();

    // Invert controls checkbox
    m_invertControlsCheckBox = new QCheckBox(tr("Invert controls"), this);
    m_invertControlsCheckBox->setStyleSheet("font-size: 10px;");
    leftColumn->addWidget(m_invertControlsCheckBox);

    leftColumn->addStretch();
    columnsLayout->addLayout(leftColumn, 1);

    // Right column: XY pad for pan/tilt
    m_xyPad = new XYPad(this);
    connect(m_xyPad, &XYPad::positionChanged, this, &TrackingControlWidget::onXYPadChanged);
    columnsLayout->addWidget(m_xyPad);

    ptzGroupLayout->addLayout(columnsLayout);

    // Position label (full width below both columns)
    m_positionLabel = new QLabel("Position: Pan 0.00, Tilt 0.00", this);
    m_positionLabel->setAlignment(Qt::AlignCenter);
    m_positionLabel->setStyleSheet("color: palette(mid); font-size: 11px;");
    ptzGroupLayout->addWidget(m_positionLabel);

    ptzContainerLayout->addWidget(ptzGroupBox);
    layout->addWidget(m_ptzContainer);

    // Update PTZ controls state based on auto-framing
    updatePTZControlsState();

    layout->addStretch();
}

void TrackingControlWidget::setAiMode(int mode)
{
    int idx = m_modeCombo->findData(mode);
    if (idx >= 0) {
        QSignalBlocker blocker(m_modeCombo);
        m_modeCombo->setCurrentIndex(idx);
    }
    updateTiny2Visibility();
}

void TrackingControlWidget::setHumanSubMode(int subMode)
{
    int idx = m_humanSubModeCombo->findData(subMode);
    if (idx >= 0) {
        QSignalBlocker blocker(m_humanSubModeCombo);
        m_humanSubModeCombo->setCurrentIndex(idx);
    }
}

void TrackingControlWidget::setAutoZoomEnabled(bool enabled)
{
    QSignalBlocker blocker(m_autoZoomCheckBox);
    m_autoZoomCheckBox->setChecked(enabled);
}

void TrackingControlWidget::setFaceFocusEnabled(bool enabled)
{
    QSignalBlocker blocker(m_faceFocusCheckBox);
    m_faceFocusCheckBox->setChecked(enabled);
    m_focusSlider->setEnabled(!enabled);
}

void TrackingControlWidget::setTrackSpeed(int speedMode)
{
    int idx = m_speedCombo->findData(speedMode);
    if (idx >= 0) {
        QSignalBlocker blocker(m_speedCombo);
        m_speedCombo->setCurrentIndex(idx);
    }
}

void TrackingControlWidget::setAudioAutoGain(bool enabled)
{
    QSignalBlocker blocker(m_audioGainCheckBox);
    m_audioGainCheckBox->setChecked(enabled);
}

void TrackingControlWidget::onTrackingToggled(bool checked)
{
    m_userInitiated = true;
    m_trackingCheckBox->setText(checked ? "Disable" : "Enable");

    if (m_tiny2Capabilities) {
        int modeValue = m_modeCombo->currentData().toInt();
        if (checked && modeValue == Device::AiWorkModeNone) {
            int humanIndex = m_modeCombo->findData(Device::AiWorkModeHuman);
            if (humanIndex >= 0) {
                m_modeCombo->blockSignals(true);
                m_modeCombo->setCurrentIndex(humanIndex);
                m_modeCombo->blockSignals(false);
                modeValue = Device::AiWorkModeHuman;
            }
        }

        int subMode = m_humanSubModeCombo->currentData().toInt();
        if (modeValue != Device::AiWorkModeHuman) {
            subMode = 0;
        }

        m_controller->setAiMode(checked ? modeValue : Device::AiWorkModeNone, subMode);
    }

    m_controller->enableAutoFraming(checked);
    setFaceFocusEnabled(checked);
    m_controller->setFaceFocus(checked);
    m_controller->setFaceAE(checked);

    // Update PTZ controls state (disable when auto-framing is on)
    updatePTZControlsState();

    m_commandTimer->start(1000);
}

void TrackingControlWidget::updateFromState(const CameraController::CameraState &state)
{
    // Only update if:
    // 1. State differs from what's shown AND
    // 2. Not during user action AND
    // 3. Command completion timer has expired AND
    // 4. Camera is not settling
    // For Tiny2: aiMode determines tracking state
    // For non-Tiny2 (original Tiny): use autoFramingEnabled flag
    bool shouldBeChecked = m_tiny2Capabilities
        ? (state.aiMode != Device::AiWorkModeNone)
        : state.autoFramingEnabled;
    bool commandInFlight = m_commandTimer->isActive();
    bool isSettling = m_controller->isSettling();

    if (m_trackingCheckBox->isChecked() != shouldBeChecked && !m_userInitiated && !commandInFlight && !isSettling) {
        m_trackingCheckBox->blockSignals(true);
        m_trackingCheckBox->setChecked(shouldBeChecked);
        m_trackingCheckBox->blockSignals(false);
    }
    m_trackingCheckBox->setText(
        m_trackingCheckBox->isChecked() ? "Disable" : "Enable");

    if (!m_userInitiated && !commandInFlight && !isSettling) {
        updatePTZControlsState();
    }

    // Connection capabilities are unknown when this widget is constructed.
    // Refresh both Tiny2 and original-Tiny containers after every state
    // update so a late SDK connection can reveal the correct controls.
    updateTiny2Visibility();

    if (m_controller->hasOriginalTinyCapabilities()
            && !m_userInitiated && !commandInFlight && !isSettling) {
        setTrackingStyle(state.trackingStyle);
    }

    if (m_tiny2Capabilities && !m_userInitiated && !commandInFlight && !isSettling) {
        int modeIdx = m_modeCombo->findData(state.aiMode);
        if (modeIdx >= 0 && m_modeCombo->currentIndex() != modeIdx) {
            m_modeCombo->blockSignals(true);
            m_modeCombo->setCurrentIndex(modeIdx);
            m_modeCombo->blockSignals(false);
        }

        updateTiny2Visibility();

        if (state.aiMode == Device::AiWorkModeHuman) {
            int subIdx = m_humanSubModeCombo->findData(state.aiSubMode);
            if (subIdx >= 0 && m_humanSubModeCombo->currentIndex() != subIdx) {
                m_humanSubModeCombo->blockSignals(true);
                m_humanSubModeCombo->setCurrentIndex(subIdx);
                m_humanSubModeCombo->blockSignals(false);
            }
        }

        if (m_autoZoomCheckBox->isChecked() != state.autoZoomEnabled) {
            m_autoZoomCheckBox->blockSignals(true);
            m_autoZoomCheckBox->setChecked(state.autoZoomEnabled);
            m_autoZoomCheckBox->blockSignals(false);
        }

        int speedIdx = m_speedCombo->findData(state.trackSpeedMode);
        if (speedIdx >= 0 && m_speedCombo->currentIndex() != speedIdx) {
            m_speedCombo->blockSignals(true);
            m_speedCombo->setCurrentIndex(speedIdx);
            m_speedCombo->blockSignals(false);
        }

        if (m_audioGainCheckBox->isChecked() != state.audioAutoGainEnabled) {
            m_audioGainCheckBox->blockSignals(true);
            m_audioGainCheckBox->setChecked(state.audioAutoGainEnabled);
            m_audioGainCheckBox->blockSignals(false);
        }
    }

    if (!commandInFlight && !isSettling
            && m_faceFocusCheckBox->isChecked() != state.faceFocusEnabled) {
        QSignalBlocker blocker(m_faceFocusCheckBox);
        m_faceFocusCheckBox->setChecked(state.faceFocusEnabled);
    }
    m_focusSlider->setEnabled(!state.faceFocusEnabled);

    // Face autofocus keeps reporting the current motor position even though
    // the slider is read-only.
    if (!m_focusSlider->isSliderDown()
            && (!commandInFlight || state.faceFocusEnabled) && !isSettling) {
        if (m_focusSlider->value() != state.manualFocusValue) {
            m_focusSlider->blockSignals(true);
            m_focusSlider->setValue(state.manualFocusValue);
            m_focusSlider->blockSignals(false);
            m_focusLabel->setText(QString::number(state.manualFocusValue));
        }
    }

    // Sync zoom slider from camera state (only when user isn't dragging)
    if (!m_zoomSlider->isSliderDown() && !commandInFlight && !isSettling) {
        int zoomSliderVal = qRound(state.zoom * 100.0);
        if (m_zoomSlider->value() != zoomSliderVal) {
            m_zoomSlider->blockSignals(true);
            m_zoomSlider->setValue(zoomSliderVal);
            m_zoomSlider->blockSignals(false);
            m_zoomLabel->setText(QString("%1x").arg(state.zoom, 0, 'f', 2));
        }
    }

    if (!commandInFlight && !isSettling) {
        static constexpr int presetZoom[] = {100, 105, 115};
        const int currentZoom = qRound(state.zoom * 100.0);
        for (int mode = 0; mode < 3; ++mode) {
            m_fovButtons[mode]->blockSignals(true);
            m_fovButtons[mode]->setChecked(currentZoom == presetZoom[mode]);
            m_fovButtons[mode]->blockSignals(false);
        }
    }

    // Sync XY pad and position label from camera state (skip during active drag)
    if (!m_xyPad->isDragging() && !commandInFlight && !isSettling) {
        float padX = state.pan;
        float padY = state.tilt;
        // XY pad shows control position, so invert back when invert is active
        if (m_invertControlsCheckBox->isChecked()) {
            padX = -padX;
            padY = -padY;
        }
        m_xyPad->setPosition(padX, padY);
        m_positionLabel->setText(QString("Position: Pan %1, Tilt %2")
            .arg(state.pan, 0, 'f', 2)
            .arg(state.tilt, 0, 'f', 2));
    }

    // If command completed and timer expired, we can now accept state updates
    if (!commandInFlight && m_userInitiated) {
        m_userInitiated = false;
    }
}

void TrackingControlWidget::onModeChanged(int index)
{
    Q_UNUSED(index);
    if (!m_tiny2Capabilities) return;

    int modeValue = m_modeCombo->currentData().toInt();
    m_humanSubModeCombo->setEnabled(modeValue == Device::AiWorkModeHuman);

    m_userInitiated = true;
    int subMode = m_humanSubModeCombo->currentData().toInt();
    if (modeValue != Device::AiWorkModeHuman) {
        subMode = 0;
    }

    m_controller->setAiMode(modeValue, subMode);

    bool shouldCheck = modeValue != Device::AiWorkModeNone;
    if (m_trackingCheckBox->isChecked() != shouldCheck) {
        m_trackingCheckBox->blockSignals(true);
        m_trackingCheckBox->setChecked(shouldCheck);
        m_trackingCheckBox->blockSignals(false);
    }

    m_commandTimer->start(1000);
}

void TrackingControlWidget::onHumanSubModeChanged(int index)
{
    Q_UNUSED(index);
    if (!m_tiny2Capabilities) return;

    if (m_modeCombo->currentData().toInt() != Device::AiWorkModeHuman) {
        return;
    }

    m_userInitiated = true;
    m_controller->setAiMode(Device::AiWorkModeHuman, m_humanSubModeCombo->currentData().toInt());
    m_commandTimer->start(1000);
}

void TrackingControlWidget::onAutoZoomToggled(bool checked)
{
    if (!m_tiny2Capabilities) return;
    m_userInitiated = true;
    m_controller->setAutoZoom(checked);
    m_commandTimer->start(1000);
}

void TrackingControlWidget::onSpeedChanged(int index)
{
    Q_UNUSED(index);
    if (!m_tiny2Capabilities) return;
    m_userInitiated = true;
    m_controller->setTrackSpeed(m_speedCombo->currentData().toInt());
    m_commandTimer->start(1000);
}

void TrackingControlWidget::onAudioGainToggled(bool checked)
{
    if (!m_tiny2Capabilities) return;
    m_userInitiated = true;
    m_controller->setAudioAutoGain(checked);
    m_commandTimer->start(1000);
}

void TrackingControlWidget::setTrackingStyle(int style)
{
    if (style < Device::AiVTrackStandard || style > Device::AiVTrackMotion) {
        return;
    }
    m_trackingStyle = style;
    for (int buttonStyle = 0; buttonStyle < 3; ++buttonStyle) {
        QSignalBlocker blocker(m_trackingStyleButtons[buttonStyle]);
        m_trackingStyleButtons[buttonStyle]->setChecked(buttonStyle == style);
    }
}

void TrackingControlWidget::onTrackingStyleChanged(int style)
{
    if (style < Device::AiVTrackStandard || style > Device::AiVTrackMotion
            || !m_controller->hasOriginalTinyCapabilities()) {
        return;
    }
    m_userInitiated = true;
    setTrackingStyle(style);
    m_controller->setTrackingStyle(style);
    m_commandTimer->start(1000);
}

void TrackingControlWidget::updateTiny2Visibility()
{
    if (!m_advancedContainer) {
        return;
    }

    // Re-read on every pass: the constructor runs before a camera is
    // connected, so a value captured once there can never become true.
    m_tiny2Capabilities = m_controller->hasTiny2Capabilities();

    m_advancedContainer->setVisible(m_tiny2Capabilities);
    m_originalTinyContainer->setVisible(m_controller->hasOriginalTinyCapabilities());
    m_humanSubModeCombo->setEnabled(m_tiny2Capabilities && m_modeCombo->currentData().toInt() == Device::AiWorkModeHuman);
}

void TrackingControlWidget::updatePTZControlsState()
{
    // Disable PTZ controls when auto-framing is enabled
    bool enabled = !m_trackingCheckBox->isChecked();
    m_ptzContainer->setEnabled(enabled);
}

void TrackingControlWidget::flushPendingCommands()
{
    if (m_dirtyPanTilt) {
        m_controller->setPanTilt(m_pendingPan, m_pendingTilt);
        m_dirtyPanTilt = false;
    }
    if (m_dirtyZoom) {
        m_controller->setZoom(m_pendingZoom / 100.0);
        m_dirtyZoom = false;
    }
    if (m_dirtyFocus) {
        m_controller->setFocusAbsolute(m_pendingFocus, false);
        m_dirtyFocus = false;
    }
}

void TrackingControlWidget::scheduleFlush()
{
    m_commandTimer->start(1000);
    // First change fires immediately, subsequent ones coalesce at 100ms
    if (!m_controlThrottle->isActive()) {
        flushPendingCommands();
        m_controlThrottle->start();
    }
}

void TrackingControlWidget::onXYPadChanged(float x, float y)
{
    if (m_invertControlsCheckBox->isChecked()) {
        x = -x;
        y = -y;
    }

    m_pendingPan = x;
    m_pendingTilt = y;
    m_dirtyPanTilt = true;

    m_positionLabel->setText(QString("Position: Pan %1, Tilt %2")
        .arg(x, 0, 'f', 2)
        .arg(y, 0, 'f', 2));

    scheduleFlush();
}

void TrackingControlWidget::onZoomChanged(int value)
{
    m_pendingZoom = value;
    m_dirtyZoom = true;
    m_zoomLabel->setText(QString("%1x").arg(value / 100.0, 0, 'f', 2));
    scheduleFlush();
}

void TrackingControlWidget::onFocusChanged(int value)
{
    m_pendingFocus = value;
    m_dirtyFocus = true;
    m_focusLabel->setText(QString::number(value));
    scheduleFlush();
}

void TrackingControlWidget::onFaceFocusToggled(bool checked)
{
    m_userInitiated = true;
    m_focusSlider->setEnabled(!checked);
    m_controller->setFaceFocus(checked);
    m_commandTimer->start(1000);
}

void TrackingControlWidget::setV4l2Mode(bool v4l2Only)
{
    m_trackingGroupBox->setVisible(!v4l2Only);
}
