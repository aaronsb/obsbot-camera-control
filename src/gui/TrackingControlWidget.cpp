#include "TrackingControlWidget.h"
#include <QLabel>
#include <dev/dev.hpp>

namespace {
constexpr int kCommandStateGuardMs = 3500;
}

TrackingControlWidget::TrackingControlWidget(CameraController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_userInitiated(false)
    , m_manualOverrideActive(true)
    , m_manualControlAuthorized(false)
    , m_manualConfirmationPending(false)
    , m_trackingConfirmationPending(false)
    , m_pendingTrackingIntentGeneration(0)
    , m_highestTrackingIntentGeneration(0)
    , m_trackingIntentResolved(false)
    , m_v4l2Only(false)
{
    // Create debounce timer for command completion
    m_commandTimer = new QTimer(this);
    m_commandTimer->setObjectName(QStringLiteral("trackingCommandTimer"));
    m_commandTimer->setSingleShot(true);

    m_profileFocusTimer = new QTimer(this);
    m_profileFocusTimer->setSingleShot(true);
    m_profileFocusTimer->setInterval(150);
    connect(m_profileFocusTimer, &QTimer::timeout,
            this, &TrackingControlWidget::submitCurrentTrackingProfile);

    m_activeTrackingProfile = {
        TrackingFocusPolicy::Manual, 50, false,
        Device::AiTrackSpeedStandard};

    // Unified throttle for all manual controls (pan/tilt, zoom, focus)
    m_controlThrottle = new QTimer(this);
    m_controlThrottle->setSingleShot(true);
    m_controlThrottle->setInterval(100);
    connect(m_controlThrottle, &QTimer::timeout, this, &TrackingControlWidget::flushPendingCommands);

    m_tiny2Capabilities = m_controller->hasTiny2Capabilities();
    connect(m_controller, &CameraController::trackingIntentStarted,
            this, &TrackingControlWidget::onTrackingIntentStarted);
    connect(m_controller, &CameraController::trackingStateConfirmationPending,
            this, &TrackingControlWidget::onTrackingStateConfirmationPending);
    connect(m_controller, &CameraController::trackingStateConfirmationFailed,
            this, &TrackingControlWidget::onTrackingStateConfirmationFailed);
    connect(m_controller, &CameraController::trackingStateConfirmationUncertain,
            this, &TrackingControlWidget::onTrackingStateConfirmationUncertain);
    connect(m_controller, &CameraController::trackingStateConfirmed,
            this, &TrackingControlWidget::onTrackingStateConfirmed);
    connect(m_controller, &CameraController::trackingOwnershipObserved,
            this, &TrackingControlWidget::onTrackingOwnershipObserved);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 14, 8, 14);
    layout->setSpacing(14);

    m_trackingGroupBox = new QGroupBox("Face Tracking", this);
    m_trackingGroupBox->setFlat(true);
    QVBoxLayout *groupLayout = new QVBoxLayout(m_trackingGroupBox);
    groupLayout->setContentsMargins(16, 16, 16, 16);
    groupLayout->setSpacing(12);

    m_trackingCheckBox = new QCheckBox("Enable Auto-Framing", this);
    m_trackingCheckBox->setObjectName(QStringLiteral("trackingCheckBox"));
    m_trackingCheckBox->setStyleSheet("font-weight: 600; font-size: 14px;");
    connect(m_trackingCheckBox, &QCheckBox::toggled, this, &TrackingControlWidget::onTrackingToggled);
    groupLayout->addWidget(m_trackingCheckBox);

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

    m_deskModeCheckBox = new QCheckBox(tr("Use camera Desk mode for automatic paper framing (direct feed)"), this);
    m_deskModeCheckBox->setStyleSheet("font-size: 11px;");
    connect(m_deskModeCheckBox, &QCheckBox::toggled,
            this, &TrackingControlWidget::onDeskModeToggled);
    advancedLayout->addWidget(m_deskModeCheckBox);

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
    m_autoZoomCheckBox->setObjectName(
        QStringLiteral("trackingAutoZoomCheckBox"));
    connect(m_autoZoomCheckBox, &QCheckBox::toggled, this, &TrackingControlWidget::onAutoZoomToggled);
    m_autoZoomCheckBox->setStyleSheet("font-size: 11px;");
    advancedLayout->addWidget(m_autoZoomCheckBox);

    QHBoxLayout *focusPolicyLayout = new QHBoxLayout();
    QLabel *focusPolicyLabel = new QLabel(tr("Focus Policy"), this);
    focusPolicyLabel->setStyleSheet("font-size: 11px; font-weight: 600;");
    m_profileFocusPolicyCombo = new QComboBox(this);
    m_profileFocusPolicyCombo->setObjectName(
        QStringLiteral("trackingFocusPolicyCombo"));
    m_profileFocusPolicyCombo->addItem(
        tr("Face autofocus"),
        static_cast<int>(TrackingFocusPolicy::Face));
    m_profileFocusPolicyCombo->addItem(
        tr("Continuous scene autofocus"),
        static_cast<int>(TrackingFocusPolicy::Continuous));
    m_profileFocusPolicyCombo->addItem(
        tr("Manual fixed position"),
        static_cast<int>(TrackingFocusPolicy::Manual));
    m_profileFocusPolicyCombo->setToolTip(tr(
        "Saved independently for each tracking mode. Scene autofocus is used "
        "for Hand, Whiteboard, and Desk by default."));
    connect(m_profileFocusPolicyCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrackingControlWidget::onProfileFocusPolicyChanged);
    focusPolicyLayout->addWidget(focusPolicyLabel);
    focusPolicyLayout->addWidget(m_profileFocusPolicyCombo, 1);
    advancedLayout->addLayout(focusPolicyLayout);

    QHBoxLayout *profileFocusLayout = new QHBoxLayout();
    QLabel *profileFocusTitle = new QLabel(tr("Saved Manual Focus"), this);
    profileFocusTitle->setStyleSheet("font-size: 11px; font-weight: 600;");
    m_profileFocusSlider = new QSlider(Qt::Horizontal, this);
    m_profileFocusSlider->setObjectName(
        QStringLiteral("trackingProfileFocusSlider"));
    m_profileFocusSlider->setRange(0, 100);
    m_profileFocusSlider->setValue(50);
    m_profileFocusSlider->setToolTip(tr(
        "Retained as this mode's manual fallback even while autofocus is active."));
    connect(m_profileFocusSlider, &QSlider::valueChanged,
            this, &TrackingControlWidget::onProfileFocusPositionChanged);
    m_profileFocusLabel = new QLabel(QStringLiteral("50"), this);
    m_profileFocusLabel->setMinimumWidth(28);
    profileFocusLayout->addWidget(profileFocusTitle);
    profileFocusLayout->addWidget(m_profileFocusSlider, 1);
    profileFocusLayout->addWidget(m_profileFocusLabel);
    advancedLayout->addLayout(profileFocusLayout);

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

    // Manual PTZ Controls (disabled when auto-framing is enabled)
    m_ptzContainer = new QWidget(this);
    QVBoxLayout *ptzContainerLayout = new QVBoxLayout(m_ptzContainer);
    ptzContainerLayout->setContentsMargins(0, 0, 0, 0);
    ptzContainerLayout->setSpacing(0);

    m_manualControlCheckBox = new QCheckBox(tr("Enable manual positioning (turns tracking off)"), m_ptzContainer);
    m_manualControlCheckBox->setObjectName(QStringLiteral("manualControlCheckBox"));
    m_manualControlCheckBox->setChecked(true);
    connect(m_manualControlCheckBox, &QCheckBox::toggled,
            this, &TrackingControlWidget::onManualControlToggled);
    ptzContainerLayout->addWidget(m_manualControlCheckBox);

    m_ptzGroupBox = new QGroupBox("Manual Camera Control", this);
    m_ptzGroupBox->setObjectName(QStringLiteral("manualControlGroup"));
    m_ptzGroupBox->setFlat(true);
    QVBoxLayout *ptzGroupLayout = new QVBoxLayout(m_ptzGroupBox);
    ptzGroupLayout->setContentsMargins(16, 16, 16, 16);
    ptzGroupLayout->setSpacing(12);

    // Two-column layout: sliders left, XY pad right
    QHBoxLayout *columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(16);

    // Left column: sliders and checkboxes
    QVBoxLayout *leftColumn = new QVBoxLayout();
    leftColumn->addStretch();

    // Zoom slider
    QHBoxLayout *zoomLayout = new QHBoxLayout();
    QLabel *zoomLabel = new QLabel("Zoom:", this);
    zoomLabel->setFixedWidth(40);
    zoomLayout->addWidget(zoomLabel);
    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setMinimum(100);  // 1.00x
    m_zoomSlider->setMaximum(200);  // 2.00x
    m_zoomSlider->setValue(100);
    connect(m_zoomSlider, &QSlider::valueChanged, this, &TrackingControlWidget::onZoomChanged);
    zoomLayout->addWidget(m_zoomSlider);
    m_zoomLabel = new QLabel("1.0x", this);
    m_zoomLabel->setMinimumWidth(40);
    zoomLayout->addWidget(m_zoomLabel);
    leftColumn->addLayout(zoomLayout);

    // Focus slider
    QHBoxLayout *focusLayout = new QHBoxLayout();
    QLabel *focusLabel = new QLabel("Focus:", this);
    focusLabel->setFixedWidth(40);
    focusLayout->addWidget(focusLabel);
    m_focusSlider = new QSlider(Qt::Horizontal, this);
    m_focusSlider->setMinimum(0);
    m_focusSlider->setMaximum(100);
    m_focusSlider->setValue(50);
    connect(m_focusSlider, &QSlider::valueChanged, this, &TrackingControlWidget::onFocusChanged);
    focusLayout->addWidget(m_focusSlider);
    m_focusLabel = new QLabel("50", this);
    m_focusLabel->setMinimumWidth(40);
    focusLayout->addWidget(m_focusLabel);
    leftColumn->addLayout(focusLayout);

    // Invert controls checkbox
    m_invertControlsCheckBox = new QCheckBox(tr("Invert controls"), this);
    m_invertControlsCheckBox->setStyleSheet("font-size: 10px;");
    leftColumn->addWidget(m_invertControlsCheckBox);

    // Mirror checkbox — syncs with Creative FX horizontal flip
    m_mirrorCheckBox = new QCheckBox(tr("Mirror view"), this);
    m_mirrorCheckBox->setStyleSheet("font-size: 10px;");
    connect(m_mirrorCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        emit mirrorToggled(checked);
    });
    leftColumn->addWidget(m_mirrorCheckBox);

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

    ptzContainerLayout->addWidget(m_ptzGroupBox);
    layout->addWidget(m_ptzContainer);

    setActiveTrackingProfile(m_activeTrackingProfile);
    setManualFocusPosition(m_activeTrackingProfile.manualFocusPosition);

    // Update PTZ controls state based on auto-framing
    updatePTZControlsState();

    layout->addStretch();
}

bool TrackingControlWidget::isManualControlEnabled() const
{
    return m_manualOverrideActive && m_manualControlAuthorized;
}

TrackingModeProfile TrackingControlWidget::activeTrackingProfile() const
{
    TrackingModeProfile profile = m_activeTrackingProfile;
    if (m_profileFocusPolicyCombo) {
        profile.focusPolicy = static_cast<TrackingFocusPolicy>(
            m_profileFocusPolicyCombo->currentData().toInt());
    }
    if (m_profileFocusSlider) {
        profile.manualFocusPosition = m_profileFocusSlider->value();
    }
    if (m_autoZoomCheckBox) {
        profile.autoZoom = m_autoZoomCheckBox->isChecked();
    }
    if (m_speedCombo) {
        profile.trackSpeed = m_speedCombo->currentData().toInt();
    }
    return profile;
}

TrackingControlWidget::TrackingState TrackingControlWidget::trackingState() const
{
    TrackingState state{
        isTrackingEnabled(),
        currentAiMode(),
        currentHumanSubMode(),
        activeTrackingProfile()
    };
    if (!state.enabled) {
        state.profile.focusPolicy = TrackingFocusPolicy::Manual;
        state.profile.manualFocusPosition = m_focusSlider->value();
        state.profile.autoZoom = false;
    }
    if (state.aiMode != Device::AiWorkModeHuman) {
        state.aiSubMode = 0;
    }
    return state;
}

bool TrackingControlWidget::matchesTrackingState(
    const TrackingState &expected) const
{
    if (isTrackingEnabled() != expected.enabled) {
        return false;
    }
    if (!expected.enabled) {
        return isManualControlEnabled()
            && trackingState().profile == expected.profile;
    }

    const int expectedMode = expected.aiMode == Device::AiWorkModeNone
        ? Device::AiWorkModeHuman : expected.aiMode;
    if (currentAiMode() != expectedMode) {
        return false;
    }
    if (expectedMode == Device::AiWorkModeHuman
        && currentHumanSubMode() != expected.aiSubMode) {
        return false;
    }
    return activeTrackingProfile() == expected.profile;
}

void TrackingControlWidget::setModeProfiles(
    const Tiny2TrackingModeProfiles &profiles)
{
    for (const auto &profile : profiles) {
        if (!isValidTrackingModeProfile(profile)) {
            return;
        }
    }
    m_modeProfiles = profiles;
}

TrackingModeProfile TrackingControlWidget::modeProfile(int aiMode) const
{
    const int index = tiny2TrackingModeProfileIndex(aiMode);
    return index >= 0
        ? m_modeProfiles[static_cast<std::size_t>(index)]
        : activeTrackingProfile();
}

TrackingControlWidget::TrackingState
TrackingControlWidget::trackingStateForMode(int aiMode) const
{
    TrackingState state = trackingState();
    state.enabled = aiMode != Device::AiWorkModeNone;
    state.aiMode = aiMode;
    state.aiSubMode = aiMode == Device::AiWorkModeHuman
        ? currentHumanSubMode() : 0;
    state.profile = modeProfile(aiMode);
    if (!state.enabled) {
        state.profile.focusPolicy = TrackingFocusPolicy::Manual;
        state.profile.manualFocusPosition = m_focusSlider->value();
        state.profile.autoZoom = false;
    }
    return state;
}

void TrackingControlWidget::setActiveTrackingProfile(
    const TrackingModeProfile &profile)
{
    if (!isValidTrackingModeProfile(profile)) {
        return;
    }
    m_activeTrackingProfile = profile;

    const int policyIndex = m_profileFocusPolicyCombo->findData(
        static_cast<int>(profile.focusPolicy));
    if (policyIndex >= 0) {
        QSignalBlocker blocker(m_profileFocusPolicyCombo);
        m_profileFocusPolicyCombo->setCurrentIndex(policyIndex);
    }
    {
        QSignalBlocker blocker(m_profileFocusSlider);
        m_profileFocusSlider->setValue(profile.manualFocusPosition);
    }
    m_profileFocusLabel->setText(
        QString::number(profile.manualFocusPosition));
    setAutoZoomEnabled(profile.autoZoom);
    setTrackSpeed(profile.trackSpeed);
}

void TrackingControlWidget::setManualFocusPosition(int position)
{
    position = qBound(0, position, 100);
    QSignalBlocker blocker(m_focusSlider);
    m_focusSlider->setValue(position);
    m_focusLabel->setText(QString::number(position));
    m_pendingFocus = position;
}

void TrackingControlWidget::setTrackingStatePresentation(
    const TrackingState &state)
{
    TrackingState normalized = state;
    if (normalized.enabled
        && normalized.aiMode == Device::AiWorkModeNone) {
        normalized.aiMode = Device::AiWorkModeHuman;
    }
    if (normalized.aiMode != Device::AiWorkModeHuman) {
        normalized.aiSubMode = 0;
    }
    setAiMode(normalized.aiMode);
    setHumanSubMode(normalized.aiSubMode);
    if (normalized.enabled) {
        setActiveTrackingProfile(normalized.profile);
    } else {
        normalized.profile.focusPolicy = TrackingFocusPolicy::Manual;
        normalized.profile.autoZoom = false;
        setManualFocusPosition(normalized.profile.manualFocusPosition);
        const int profileIndex =
            tiny2TrackingModeProfileIndex(normalized.aiMode);
        if (profileIndex >= 0) {
            setActiveTrackingProfile(
                m_modeProfiles[static_cast<std::size_t>(profileIndex)]);
        }
        else {
            setActiveTrackingProfile(normalized.profile);
        }
    }
    setTrackingEnabled(normalized.enabled);
}

void TrackingControlWidget::setTrackingEnabled(bool enabled)
{
    m_manualOverrideActive = !enabled;
    m_manualControlAuthorized = false;
    m_manualConfirmationPending = false;
    m_trackingConfirmationPending = false;
    m_pendingTrackingIntentGeneration = 0;
    m_trackingIntentResolved = false;
    QSignalBlocker trackingBlocker(m_trackingCheckBox);
    QSignalBlocker manualBlocker(m_manualControlCheckBox);
    m_trackingCheckBox->setChecked(enabled);
    m_manualControlCheckBox->setChecked(m_manualOverrideActive);
    m_userInitiated = true;
    m_commandTimer->start(kCommandStateGuardMs);
    updatePTZControlsState();
}

TrackingControlWidget::TrackingApplyResult
TrackingControlWidget::applyTrackingState(const TrackingState &state)
{
    TrackingState requestedState = state;
    if (requestedState.enabled
        && requestedState.aiMode == Device::AiWorkModeNone) {
        requestedState.aiMode = Device::AiWorkModeHuman;
    }
    if (requestedState.aiMode != Device::AiWorkModeHuman) {
        requestedState.aiSubMode = 0;
    }

    const TrackingState previousState = trackingState();
    const TrackingModeProfile previousEditorProfile =
        activeTrackingProfile();
    const bool previousManualAuthorized = m_manualControlAuthorized;
    const bool previousConfirmationPending = m_manualConfirmationPending;
    const bool previousTrackingConfirmationPending =
        m_trackingConfirmationPending;
    const quint64 previousTrackingIntentGeneration =
        m_pendingTrackingIntentGeneration;
    const bool previousTrackingIntentResolved = m_trackingIntentResolved;
    if (!isValidTrackingModeProfile(requestedState.profile)) {
        return {};
    }
    if (!requestedState.enabled) {
        requestedState.profile.focusPolicy = TrackingFocusPolicy::Manual;
        requestedState.profile.autoZoom = false;
    }
    setAiMode(requestedState.aiMode);
    setHumanSubMode(requestedState.aiSubMode);
    if (requestedState.enabled) {
        setActiveTrackingProfile(requestedState.profile);
    } else {
        setManualFocusPosition(requestedState.profile.manualFocusPosition);
    }
    setTrackingEnabled(requestedState.enabled);

    // Start the stale-state guard before the controller emits its optimistic
    // stateChanged signal.
    m_userInitiated = true;
    m_commandTimer->start(kCommandStateGuardMs);
    const auto result = m_controller->setTrackingState(
        requestedState.enabled, requestedState.aiMode,
        requestedState.aiSubMode, requestedState.profile);
    if (!result.complete()) {
        setAiMode(previousState.aiMode);
        setHumanSubMode(previousState.aiSubMode);
        setActiveTrackingProfile(previousEditorProfile);
        if (!previousState.enabled) {
            setManualFocusPosition(
                previousState.profile.manualFocusPosition);
        }
        setTrackingEnabled(previousState.enabled);
        if (result.confirmationPending) {
            // Either the prior camera target is still being confirmed under
            // this generation or rollback itself is uncertain. Restore the
            // visible intent, but keep all manual movement fail-closed.
            m_manualControlAuthorized = false;
            m_manualConfirmationPending = !previousState.enabled;
            m_trackingConfirmationPending = true;
            m_pendingTrackingIntentGeneration = result.intentGeneration;
            m_trackingIntentResolved = false;
        } else {
            m_manualControlAuthorized = previousManualAuthorized;
            m_manualConfirmationPending = previousConfirmationPending;
            if (m_highestTrackingIntentGeneration
                > previousTrackingIntentGeneration) {
                m_trackingConfirmationPending = false;
                m_pendingTrackingIntentGeneration =
                    m_highestTrackingIntentGeneration;
                m_trackingIntentResolved = true;
            } else {
                m_trackingConfirmationPending =
                    previousTrackingConfirmationPending;
                m_pendingTrackingIntentGeneration =
                    previousTrackingIntentGeneration;
                m_trackingIntentResolved = previousTrackingIntentResolved;
            }
        }
        m_userInitiated = false;
        m_commandTimer->stop();
    } else {
        m_manualConfirmationPending =
            !requestedState.enabled && result.confirmationPending;
        m_trackingConfirmationPending = result.confirmationPending;
        m_pendingTrackingIntentGeneration = result.intentGeneration;
        m_trackingIntentResolved = !result.confirmationPending;
        m_manualControlAuthorized =
            !requestedState.enabled && !result.confirmationPending;
    }
    updatePTZControlsState();
    return {
        result.complete(), result.confirmationPending,
        result.intentGeneration
    };
}

void TrackingControlWidget::setAiMode(int mode)
{
    int idx = m_modeCombo->findData(mode);
    if (idx >= 0) {
        QSignalBlocker blocker(m_modeCombo);
        m_modeCombo->setCurrentIndex(idx);
        m_lastAcceptedAiMode = mode;
    }
    updateTiny2Visibility();
}

void TrackingControlWidget::setHumanSubMode(int subMode)
{
    int idx = m_humanSubModeCombo->findData(subMode);
    if (idx >= 0) {
        QSignalBlocker blocker(m_humanSubModeCombo);
        m_humanSubModeCombo->setCurrentIndex(idx);
        m_lastAcceptedHumanSubMode = subMode;
    }
}

void TrackingControlWidget::setAutoZoomEnabled(bool enabled)
{
    QSignalBlocker blocker(m_autoZoomCheckBox);
    m_autoZoomCheckBox->setChecked(enabled);
    m_activeTrackingProfile.autoZoom = enabled;
}

void TrackingControlWidget::setTrackSpeed(int speedMode)
{
    int idx = m_speedCombo->findData(speedMode);
    if (idx >= 0) {
        QSignalBlocker blocker(m_speedCombo);
        m_speedCombo->setCurrentIndex(idx);
        m_activeTrackingProfile.trackSpeed = speedMode;
    }
}

void TrackingControlWidget::setAudioAutoGain(bool enabled)
{
    QSignalBlocker blocker(m_audioGainCheckBox);
    m_audioGainCheckBox->setChecked(enabled);
}

void TrackingControlWidget::onTrackingToggled(bool checked)
{
    const TrackingModeProfile previousEditorProfile =
        activeTrackingProfile();
    TrackingState previousState{
        !checked,
        m_modeCombo->currentData().toInt(),
        m_humanSubModeCombo->currentData().toInt(),
        activeTrackingProfile()
    };
    if (!previousState.enabled) {
        previousState.profile.focusPolicy = TrackingFocusPolicy::Manual;
        previousState.profile.manualFocusPosition = m_focusSlider->value();
        previousState.profile.autoZoom = false;
    }
    const bool previousManualOverride = m_manualOverrideActive;
    const bool previousManualAuthorized = m_manualControlAuthorized;
    const bool previousConfirmationPending = m_manualConfirmationPending;
    const bool previousTrackingConfirmationPending =
        m_trackingConfirmationPending;
    const quint64 previousTrackingIntentGeneration =
        m_pendingTrackingIntentGeneration;
    const bool previousTrackingIntentResolved = m_trackingIntentResolved;
    m_manualOverrideActive = !checked;
    m_manualControlAuthorized = false;
    m_manualConfirmationPending = !checked;
    m_trackingConfirmationPending = false;
    m_trackingIntentResolved = false;

    int modeValue = m_modeCombo->currentData().toInt();
    if (checked && modeValue == Device::AiWorkModeNone) {
        const int humanIndex = m_modeCombo->findData(Device::AiWorkModeHuman);
        if (humanIndex >= 0) {
            QSignalBlocker blocker(m_modeCombo);
            m_modeCombo->setCurrentIndex(humanIndex);
            modeValue = Device::AiWorkModeHuman;
        }
    }

    if (checked) {
        // Tracking-off uses a temporary manual active profile; re-enabling
        // always restores the selected mode's independent saved profile.
        setActiveTrackingProfile(modeProfile(modeValue));
    }

    int subMode = m_humanSubModeCombo->currentData().toInt();
    if (modeValue != Device::AiWorkModeHuman) {
        subMode = 0;
    }

    if (checked) {
        m_controlThrottle->stop();
        m_dirtyPanTilt = false;
        m_dirtyZoom = false;
        m_dirtyFocus = false;
    }

    // Guard before the controller's synchronous stateChanged emissions.
    m_userInitiated = true;
    m_commandTimer->start(kCommandStateGuardMs);
    updatePTZControlsState();
    TrackingState requestedState{
        checked,
        modeValue,
        subMode,
        activeTrackingProfile()
    };
    if (!checked) {
        requestedState.profile.focusPolicy = TrackingFocusPolicy::Manual;
        requestedState.profile.manualFocusPosition = m_focusSlider->value();
        requestedState.profile.autoZoom = false;
    }
    const auto result = m_controller->setTrackingState(
        requestedState.enabled, requestedState.aiMode,
        requestedState.aiSubMode, requestedState.profile);
    emit trackingIntentEdited(requestedState, false);
    if (!result.complete()) {
        setAiMode(previousState.aiMode);
        setHumanSubMode(previousState.aiSubMode);
        setActiveTrackingProfile(previousEditorProfile);
        {
            QSignalBlocker blocker(m_trackingCheckBox);
            m_trackingCheckBox->setChecked(previousState.enabled);
        }
        m_manualOverrideActive = previousManualOverride;
        if (result.confirmationPending) {
            m_manualControlAuthorized = false;
            m_manualConfirmationPending = !previousState.enabled;
            m_trackingConfirmationPending = true;
            m_pendingTrackingIntentGeneration = result.intentGeneration;
            m_trackingIntentResolved = false;
        } else {
            m_manualControlAuthorized = previousManualAuthorized;
            m_manualConfirmationPending = previousConfirmationPending;
            if (m_highestTrackingIntentGeneration
                > previousTrackingIntentGeneration) {
                m_trackingConfirmationPending = false;
                m_pendingTrackingIntentGeneration =
                    m_highestTrackingIntentGeneration;
                m_trackingIntentResolved = true;
            } else {
                m_trackingConfirmationPending =
                    previousTrackingConfirmationPending;
                m_pendingTrackingIntentGeneration =
                    previousTrackingIntentGeneration;
                m_trackingIntentResolved = previousTrackingIntentResolved;
            }
        }
        m_userInitiated = false;
        m_commandTimer->stop();
    } else {
        // The command was accepted even when its fresh camera confirmation is
        // still pending; use that accepted selection as the rollback baseline
        // for any immediately following mode edit.
        m_lastAcceptedAiMode = modeValue;
        if (modeValue == Device::AiWorkModeHuman) {
            m_lastAcceptedHumanSubMode = subMode;
        }
        m_manualConfirmationPending = !checked && result.confirmationPending;
        m_trackingConfirmationPending = result.confirmationPending;
        m_pendingTrackingIntentGeneration = result.intentGeneration;
        m_trackingIntentResolved = !result.confirmationPending;
        m_manualControlAuthorized = !checked && !result.confirmationPending;
    }
    updatePTZControlsState();
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
    const bool cameraReportsTracking = m_tiny2Capabilities
        ? (state.aiMode != Device::AiWorkModeNone)
        : state.autoFramingEnabled;
    const bool commandInFlight = m_commandTimer->isActive();
    const bool isSettling = m_controller->isSettling();
    const bool hasCurrentExplicitIntent =
        m_pendingTrackingIntentGeneration != 0
        && m_pendingTrackingIntentGeneration
            == m_highestTrackingIntentGeneration;

    if (!m_userInitiated && !commandInFlight && !isSettling) {
        // Generic cameraStatus() telemetry may lag. It can orient an
        // uninitialized widget, but it cannot replace a resolved explicit
        // tracking intent, create a manual latch, or authorize movement.
        const bool hasResolvedExplicitIntent =
            hasCurrentExplicitIntent && m_trackingIntentResolved;
        const bool shouldBeChecked = m_manualOverrideActive
            ? false
            : (hasResolvedExplicitIntent
                   ? m_trackingCheckBox->isChecked()
                   : cameraReportsTracking);
        if (m_trackingCheckBox->isChecked() != shouldBeChecked) {
            QSignalBlocker blocker(m_trackingCheckBox);
            m_trackingCheckBox->setChecked(shouldBeChecked);
        }
        updatePTZControlsState();
    }

    bool tiny2 = m_controller->hasTiny2Capabilities();
    if (tiny2 != m_tiny2Capabilities) {
        m_tiny2Capabilities = tiny2;
        updateTiny2Visibility();
    }

    if (m_tiny2Capabilities && !m_manualOverrideActive
        && !m_userInitiated && !commandInFlight && !isSettling) {
        // AI mode, sub-mode, and auto-zoom are one exact operator intent.
        // Ungenerated SDK telemetry may initialize them, but cannot rewrite an
        // explicit pending or resolved selection (or its rollback baseline).
        if (!hasCurrentExplicitIntent) {
            int modeIdx = m_modeCombo->findData(state.aiMode);
            if (modeIdx >= 0) {
                if (m_modeCombo->currentIndex() != modeIdx) {
                    m_modeCombo->blockSignals(true);
                    m_modeCombo->setCurrentIndex(modeIdx);
                    m_modeCombo->blockSignals(false);
                }
                m_lastAcceptedAiMode = state.aiMode;
            }

            updateTiny2Visibility();

            if (state.aiMode == Device::AiWorkModeHuman) {
                int subIdx = m_humanSubModeCombo->findData(state.aiSubMode);
                if (subIdx >= 0) {
                    if (m_humanSubModeCombo->currentIndex() != subIdx) {
                        m_humanSubModeCombo->blockSignals(true);
                        m_humanSubModeCombo->setCurrentIndex(subIdx);
                        m_humanSubModeCombo->blockSignals(false);
                    }
                    m_lastAcceptedHumanSubMode = state.aiSubMode;
                }
            }

            if (m_autoZoomCheckBox->isChecked() != state.autoZoomEnabled) {
                m_autoZoomCheckBox->blockSignals(true);
                m_autoZoomCheckBox->setChecked(state.autoZoomEnabled);
                m_autoZoomCheckBox->blockSignals(false);
            }
        }

        if (m_audioGainCheckBox->isChecked() != state.audioAutoGainEnabled) {
            m_audioGainCheckBox->blockSignals(true);
            m_audioGainCheckBox->setChecked(state.audioAutoGainEnabled);
            m_audioGainCheckBox->blockSignals(false);
        }
    }

    // Sync focus slider from camera state (only when user isn't dragging)
    if (!m_focusSlider->isSliderDown() && !commandInFlight && !isSettling) {
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

    const int modeValue = m_modeCombo->currentData().toInt();
    const int previousModeValue = m_lastAcceptedAiMode;
    const int previousSubMode = m_lastAcceptedHumanSubMode;
    const TrackingModeProfile previousProfile = activeTrackingProfile();
    const bool previousTracking = m_trackingCheckBox->isChecked();
    const bool previousManualOverride = m_manualOverrideActive;
    const bool previousManualAuthorized = m_manualControlAuthorized;
    const bool previousConfirmationPending = m_manualConfirmationPending;
    const bool previousTrackingConfirmationPending =
        m_trackingConfirmationPending;
    const quint64 previousTrackingIntentGeneration =
        m_pendingTrackingIntentGeneration;
    const bool previousTrackingIntentResolved = m_trackingIntentResolved;
    m_humanSubModeCombo->setEnabled(modeValue == Device::AiWorkModeHuman);

    int subMode = m_humanSubModeCombo->currentData().toInt();
    if (modeValue != Device::AiWorkModeHuman) {
        subMode = 0;
    }

    const bool shouldCheck = modeValue != Device::AiWorkModeNone;
    TrackingModeProfile requestedProfile = shouldCheck
        ? modeProfile(modeValue) : previousProfile;
    if (!shouldCheck) {
        requestedProfile.focusPolicy = TrackingFocusPolicy::Manual;
        requestedProfile.manualFocusPosition = m_focusSlider->value();
        requestedProfile.autoZoom = false;
    }
    setActiveTrackingProfile(requestedProfile);

    m_manualOverrideActive = !shouldCheck;
    m_manualControlAuthorized = false;
    m_manualConfirmationPending = !shouldCheck;
    m_trackingConfirmationPending = false;
    m_trackingIntentResolved = false;
    if (m_trackingCheckBox->isChecked() != shouldCheck) {
        QSignalBlocker blocker(m_trackingCheckBox);
        m_trackingCheckBox->setChecked(shouldCheck);
    }
    updatePTZControlsState();
    updateTiny2Visibility();

    const TrackingState requestedState{
        shouldCheck, modeValue, subMode, requestedProfile};
    m_userInitiated = true;
    m_commandTimer->start(kCommandStateGuardMs);
    const auto result = m_controller->setTrackingState(
        requestedState.enabled, requestedState.aiMode,
        requestedState.aiSubMode, requestedState.profile);
    emit trackingIntentEdited(requestedState, false);
    if (!result.complete()) {
        setAiMode(previousModeValue);
        setHumanSubMode(previousSubMode);
        setActiveTrackingProfile(previousProfile);
        QSignalBlocker blocker(m_trackingCheckBox);
        m_trackingCheckBox->setChecked(previousTracking);
        m_manualOverrideActive = previousManualOverride;
        if (result.confirmationPending) {
            m_manualControlAuthorized = false;
            m_manualConfirmationPending = previousManualOverride;
            m_trackingConfirmationPending = true;
            m_pendingTrackingIntentGeneration = result.intentGeneration;
            m_trackingIntentResolved = false;
        } else {
            m_manualControlAuthorized = previousManualAuthorized;
            m_manualConfirmationPending = previousConfirmationPending;
            if (m_highestTrackingIntentGeneration
                > previousTrackingIntentGeneration) {
                m_trackingConfirmationPending = false;
                m_pendingTrackingIntentGeneration =
                    m_highestTrackingIntentGeneration;
                m_trackingIntentResolved = true;
            } else {
                m_trackingConfirmationPending =
                    previousTrackingConfirmationPending;
                m_pendingTrackingIntentGeneration =
                    previousTrackingIntentGeneration;
                m_trackingIntentResolved = previousTrackingIntentResolved;
            }
        }
        m_userInitiated = false;
        m_commandTimer->stop();
    } else {
        m_lastAcceptedAiMode = modeValue;
        if (modeValue == Device::AiWorkModeHuman) {
            m_lastAcceptedHumanSubMode = subMode;
        }
        m_manualConfirmationPending = !shouldCheck && result.confirmationPending;
        m_trackingConfirmationPending = result.confirmationPending;
        m_pendingTrackingIntentGeneration = result.intentGeneration;
        m_trackingIntentResolved = !result.confirmationPending;
        m_manualControlAuthorized = !shouldCheck && !result.confirmationPending;
    }
    updatePTZControlsState();
}

void TrackingControlWidget::onHumanSubModeChanged(int index)
{
    Q_UNUSED(index);
    if (!m_tiny2Capabilities) return;
    if (currentAiMode() != Device::AiWorkModeHuman) return;

    TrackingState requestedState = trackingState();
    if (m_manualOverrideActive) {
        requestedState.aiSubMode = currentHumanSubMode();
        m_lastAcceptedHumanSubMode = requestedState.aiSubMode;
        emit trackingIntentEdited(requestedState, false);
        return;
    }
    requestedState.enabled = true;
    requestedState.aiMode = Device::AiWorkModeHuman;
    requestedState.aiSubMode = currentHumanSubMode();
    emit trackingIntentEdited(requestedState, false);

    const quint64 previousGeneration = m_pendingTrackingIntentGeneration;
    const bool previousPending = m_trackingConfirmationPending;
    const bool previousResolved = m_trackingIntentResolved;
    const int previousSubMode = m_lastAcceptedHumanSubMode;
    m_userInitiated = true;
    m_commandTimer->start(kCommandStateGuardMs);
    const auto result = m_controller->setTrackingState(
        requestedState.enabled, requestedState.aiMode,
        requestedState.aiSubMode, requestedState.profile);
    if (result.complete()) {
        m_lastAcceptedHumanSubMode = requestedState.aiSubMode;
        m_trackingConfirmationPending = result.confirmationPending;
        m_pendingTrackingIntentGeneration = result.intentGeneration;
        m_trackingIntentResolved = !result.confirmationPending;
    } else {
        setHumanSubMode(previousSubMode);
        if (result.confirmationPending) {
            m_trackingConfirmationPending = true;
            m_pendingTrackingIntentGeneration = result.intentGeneration;
            m_trackingIntentResolved = false;
            return;
        }
        if (m_highestTrackingIntentGeneration > previousGeneration) {
            m_trackingConfirmationPending = false;
            m_pendingTrackingIntentGeneration =
                m_highestTrackingIntentGeneration;
            m_trackingIntentResolved = true;
        } else {
            m_trackingConfirmationPending = previousPending;
            m_pendingTrackingIntentGeneration = previousGeneration;
            m_trackingIntentResolved = previousResolved;
        }
        m_userInitiated = false;
        m_commandTimer->stop();
    }
}

void TrackingControlWidget::onAutoZoomToggled(bool checked)
{
    if (!m_tiny2Capabilities) return;
    if (tiny2TrackingModeProfileIndex(currentAiMode()) < 0) {
        setAutoZoomEnabled(m_activeTrackingProfile.autoZoom);
        return;
    }
    m_activeTrackingProfile.autoZoom = checked;
    storeActiveProfileForCurrentMode();
    persistActiveTrackingIntent(true);
    submitCurrentTrackingProfile();
}

void TrackingControlWidget::onSpeedChanged(int index)
{
    Q_UNUSED(index);
    if (!m_tiny2Capabilities) return;
    if (tiny2TrackingModeProfileIndex(currentAiMode()) < 0) {
        setTrackSpeed(m_activeTrackingProfile.trackSpeed);
        return;
    }
    m_activeTrackingProfile.trackSpeed = currentTrackSpeed();
    storeActiveProfileForCurrentMode();
    persistActiveTrackingIntent(true);
    submitCurrentTrackingProfile();
}

void TrackingControlWidget::onProfileFocusPolicyChanged(int index)
{
    Q_UNUSED(index);
    if (!m_tiny2Capabilities) return;
    if (tiny2TrackingModeProfileIndex(currentAiMode()) < 0) {
        setActiveTrackingProfile(m_activeTrackingProfile);
        return;
    }
    m_activeTrackingProfile.focusPolicy = static_cast<TrackingFocusPolicy>(
        m_profileFocusPolicyCombo->currentData().toInt());
    storeActiveProfileForCurrentMode();
    persistActiveTrackingIntent(true);
    submitCurrentTrackingProfile();
}

void TrackingControlWidget::onProfileFocusPositionChanged(int value)
{
    if (!m_tiny2Capabilities) return;
    if (tiny2TrackingModeProfileIndex(currentAiMode()) < 0) {
        setActiveTrackingProfile(m_activeTrackingProfile);
        return;
    }
    m_profileFocusLabel->setText(QString::number(value));
    m_activeTrackingProfile.manualFocusPosition = value;
    storeActiveProfileForCurrentMode();
    persistActiveTrackingIntent(true);
    if (isTrackingEnabled()
        && m_activeTrackingProfile.focusPolicy
            == TrackingFocusPolicy::Manual) {
        m_profileFocusTimer->start();
    }
}

void TrackingControlWidget::storeActiveProfileForCurrentMode()
{
    m_activeTrackingProfile = activeTrackingProfile();
    const int profileIndex = tiny2TrackingModeProfileIndex(currentAiMode());
    if (profileIndex >= 0) {
        m_modeProfiles[static_cast<std::size_t>(profileIndex)] =
            m_activeTrackingProfile;
    }
}

void TrackingControlWidget::persistActiveTrackingIntent(
    bool updateModeProfile)
{
    TrackingState intent = trackingState();
    if (updateModeProfile && !intent.enabled) {
        // The profile editor remains bound to the retained AI mode while the
        // active camera intent is temporary manual/off.
        intent.profile = activeTrackingProfile();
    }
    emit trackingIntentEdited(intent, updateModeProfile);
}

void TrackingControlWidget::submitCurrentTrackingProfile()
{
    if (!m_tiny2Capabilities || !isTrackingEnabled()) {
        return;
    }

    const TrackingState requestedState = trackingState();
    m_userInitiated = true;
    m_commandTimer->start(kCommandStateGuardMs);
    const auto result = m_controller->setTrackingState(
        requestedState.enabled, requestedState.aiMode,
        requestedState.aiSubMode, requestedState.profile);
    if (result.complete()) {
        m_trackingConfirmationPending = result.confirmationPending;
        m_pendingTrackingIntentGeneration = result.intentGeneration;
        m_trackingIntentResolved = !result.confirmationPending;
    } else if (result.confirmationPending) {
        m_trackingConfirmationPending = true;
        m_pendingTrackingIntentGeneration = result.intentGeneration;
        m_trackingIntentResolved = false;
    } else {
        m_trackingConfirmationPending = false;
        m_pendingTrackingIntentGeneration =
            m_highestTrackingIntentGeneration;
        m_trackingIntentResolved = true;
        m_userInitiated = false;
        m_commandTimer->stop();
    }
}

void TrackingControlWidget::onAudioGainToggled(bool checked)
{
    if (!m_tiny2Capabilities) return;
    emit audioAutoGainIntentEdited(checked);
    m_userInitiated = true;
    m_commandTimer->start(kCommandStateGuardMs);
    m_controller->setAudioAutoGain(checked);
}

void TrackingControlWidget::onManualControlToggled(bool checked)
{
    const bool trackingShouldBeEnabled = !checked;
    if (m_trackingCheckBox->isChecked() != trackingShouldBeEnabled) {
        // The tracking handler owns the product-specific camera transition and
        // rolls the checkbox back if the essential camera command fails.
        m_trackingCheckBox->setChecked(trackingShouldBeEnabled);
    } else if (checked
               && (!m_manualOverrideActive || !m_manualControlAuthorized)) {
        // Generic status may already display tracking as off, but it is never
        // sufficient movement authorization. Explicitly submit tracking-off so
        // this manual request receives a generation-bound confirmation.
        onTrackingToggled(false);
    } else {
        m_manualOverrideActive = checked;
        if (!checked) {
            m_manualControlAuthorized = false;
        }
        updatePTZControlsState();
    }
}

void TrackingControlWidget::onTrackingIntentStarted(quint64 intentGeneration)
{
    if (intentGeneration == 0
        || intentGeneration <= m_highestTrackingIntentGeneration) {
        return;
    }
    m_highestTrackingIntentGeneration = intentGeneration;
    m_pendingTrackingIntentGeneration = intentGeneration;
    m_trackingIntentResolved = false;
    m_trackingConfirmationPending = false;
    // Any new ownership intent immediately revokes movement authorization.
    m_manualControlAuthorized = false;
    m_controlThrottle->stop();
    m_dirtyPanTilt = false;
    m_dirtyZoom = false;
    m_dirtyFocus = false;
    updatePTZControlsState();
}

void TrackingControlWidget::onTrackingStateConfirmationPending(
    bool trackingEnabled, quint64 intentGeneration)
{
    if (intentGeneration != m_pendingTrackingIntentGeneration
        || intentGeneration != m_highestTrackingIntentGeneration
        || m_trackingIntentResolved) {
        return;
    }
    m_userInitiated = true;
    m_commandTimer->start(kCommandStateGuardMs);
    m_trackingConfirmationPending = true;
    m_manualOverrideActive = !trackingEnabled;
    m_manualControlAuthorized = false;
    m_manualConfirmationPending = !trackingEnabled;
    {
        QSignalBlocker blocker(m_trackingCheckBox);
        m_trackingCheckBox->setChecked(trackingEnabled);
    }
    m_controlThrottle->stop();
    m_dirtyPanTilt = false;
    m_dirtyZoom = false;
    m_dirtyFocus = false;
    updatePTZControlsState();
}

void TrackingControlWidget::onTrackingStateConfirmationFailed(
    bool trackingEnabled, quint64 intentGeneration)
{
    if (intentGeneration != m_pendingTrackingIntentGeneration
        || intentGeneration != m_highestTrackingIntentGeneration
        || m_trackingIntentResolved) {
        return;
    }
    Q_UNUSED(trackingEnabled);
    m_userInitiated = false;
    m_commandTimer->stop();
    // A terminal mismatch reports camera reality but does not replace the
    // operator's requested mode. In particular, failed tracking-off keeps the
    // manual latch visible while movement remains fail-closed.
    m_manualControlAuthorized = false;
    m_manualConfirmationPending = m_manualOverrideActive;
    m_trackingConfirmationPending = false;
    m_trackingIntentResolved = true;
    {
        QSignalBlocker blocker(m_trackingCheckBox);
        m_trackingCheckBox->setChecked(!m_manualOverrideActive);
    }
    m_controlThrottle->stop();
    m_dirtyPanTilt = false;
    m_dirtyZoom = false;
    m_dirtyFocus = false;
    updatePTZControlsState();
}

void TrackingControlWidget::onTrackingStateConfirmationUncertain(
    quint64 intentGeneration)
{
    if (intentGeneration != m_pendingTrackingIntentGeneration
        || intentGeneration != m_highestTrackingIntentGeneration
        || m_trackingIntentResolved) {
        return;
    }
    m_userInitiated = false;
    m_commandTimer->stop();
    m_trackingConfirmationPending = true;
    m_manualControlAuthorized = false;
    m_manualConfirmationPending = m_manualOverrideActive;
    if (!m_manualOverrideActive) {
        QSignalBlocker blocker(m_trackingCheckBox);
        m_trackingCheckBox->setChecked(true);
    }
    m_controlThrottle->stop();
    m_dirtyPanTilt = false;
    m_dirtyZoom = false;
    m_dirtyFocus = false;
    updatePTZControlsState();
}

void TrackingControlWidget::onTrackingStateConfirmed(
    bool trackingEnabled, quint64 intentGeneration)
{
    if (intentGeneration != m_pendingTrackingIntentGeneration
        || intentGeneration != m_highestTrackingIntentGeneration
        || m_trackingIntentResolved) {
        return;
    }

    m_userInitiated = false;
    m_commandTimer->stop();
    m_manualOverrideActive = !trackingEnabled;
    m_manualControlAuthorized = !trackingEnabled;
    m_manualConfirmationPending = false;
    m_trackingConfirmationPending = false;
    m_trackingIntentResolved = true;
    {
        QSignalBlocker blocker(m_trackingCheckBox);
        m_trackingCheckBox->setChecked(trackingEnabled);
    }
    if (trackingEnabled) {
        m_controlThrottle->stop();
        m_dirtyPanTilt = false;
        m_dirtyZoom = false;
        m_dirtyFocus = false;
    }
    updatePTZControlsState();
}

void TrackingControlWidget::onTrackingOwnershipObserved(bool trackingEnabled)
{
    const bool currentIntentUnresolved =
        !m_trackingIntentResolved
        && m_pendingTrackingIntentGeneration != 0
        && m_pendingTrackingIntentGeneration
            == m_highestTrackingIntentGeneration;
    if (!m_manualOverrideActive || currentIntentUnresolved) {
        // AI-off alone cannot resolve an uncertain auto-zoom rollback. Only a
        // generation-bound terminal result (or a new explicit intent) may clear
        // that fail-closed ownership state.
        return;
    }

    // This signal is emitted only from a successful fresh Tiny 2 status read.
    // It can revoke or restore movement authority without changing the
    // operator's explicit manual-mode latch.
    m_manualControlAuthorized = !trackingEnabled;
    m_manualConfirmationPending = trackingEnabled;
    {
        QSignalBlocker blocker(m_trackingCheckBox);
        m_trackingCheckBox->setChecked(false);
    }
    if (trackingEnabled) {
        m_controlThrottle->stop();
        m_dirtyPanTilt = false;
        m_dirtyZoom = false;
        m_dirtyFocus = false;
    }
    updatePTZControlsState();
}

void TrackingControlWidget::onDeskModeToggled(bool checked)
{
    const int targetMode = checked ? Device::AiWorkModeDesk : Device::AiWorkModeHuman;
    const int index = m_modeCombo->findData(targetMode);
    if (index >= 0 && m_modeCombo->currentIndex() != index) {
        m_modeCombo->setCurrentIndex(index);
    }
}

void TrackingControlWidget::updateTiny2Visibility()
{
    if (!m_advancedContainer) {
        return;
    }

    m_advancedContainer->setVisible(m_tiny2Capabilities);
    const int selectedMode = m_modeCombo->currentData().toInt();
    const bool profileModeSelected = m_tiny2Capabilities
        && tiny2TrackingModeProfileIndex(selectedMode) >= 0;
    m_humanSubModeCombo->setEnabled(
        m_tiny2Capabilities && selectedMode == Device::AiWorkModeHuman);
    m_autoZoomCheckBox->setEnabled(profileModeSelected);
    m_speedCombo->setEnabled(profileModeSelected);
    m_profileFocusPolicyCombo->setEnabled(profileModeSelected);
    m_profileFocusSlider->setEnabled(profileModeSelected);
    m_profileFocusLabel->setEnabled(profileModeSelected);
    if (m_deskModeCheckBox) {
        QSignalBlocker blocker(m_deskModeCheckBox);
        m_deskModeCheckBox->setChecked(
            m_tiny2Capabilities && m_modeCombo->currentData().toInt() == Device::AiWorkModeDesk);
    }
}

void TrackingControlWidget::updatePTZControlsState()
{
    if (m_manualControlCheckBox) {
        QSignalBlocker blocker(m_manualControlCheckBox);
        m_manualControlCheckBox->setChecked(m_manualOverrideActive);
        m_manualControlCheckBox->setEnabled(!m_v4l2Only);
    }
    if (m_ptzGroupBox) {
        if (m_v4l2Only) {
            m_ptzGroupBox->setTitle(
                tr("Manual Camera Control (SDK tracking confirmation unavailable)"));
        } else if (m_manualOverrideActive && !m_manualControlAuthorized) {
            m_ptzGroupBox->setTitle(
                tr("Manual Camera Control (waiting for tracking to stop)"));
        } else {
            m_ptzGroupBox->setTitle(tr("Manual Camera Control"));
        }
        m_ptzGroupBox->setEnabled(
            !m_v4l2Only
            && m_manualOverrideActive
            && m_manualControlAuthorized);
    }
}

void TrackingControlWidget::flushPendingCommands()
{
    if (!isManualControlEnabled()) {
        m_dirtyPanTilt = false;
        m_dirtyZoom = false;
        m_dirtyFocus = false;
        return;
    }
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
    if (!isManualControlEnabled()) {
        return;
    }
    // First change fires immediately, subsequent ones coalesce at 100ms
    if (!m_controlThrottle->isActive()) {
        flushPendingCommands();
        m_controlThrottle->start();
    }
}

void TrackingControlWidget::onXYPadChanged(float x, float y)
{
    if (!isManualControlEnabled()) {
        return;
    }
    if (m_invertControlsCheckBox->isChecked()) {
        x = -x;
        y = -y;
    }
    emit panTiltIntentEdited(x, y);

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
    if (!isManualControlEnabled()) {
        return;
    }
    emit zoomIntentEdited(value / 100.0);
    m_pendingZoom = value;
    m_dirtyZoom = true;
    m_zoomLabel->setText(QString("%1x").arg(value / 100.0, 0, 'f', 2));
    scheduleFlush();
}

void TrackingControlWidget::onFocusChanged(int value)
{
    if (!isManualControlEnabled()) {
        return;
    }
    emit focusIntentEdited(value);
    m_activeTrackingProfile.focusPolicy = TrackingFocusPolicy::Manual;
    m_activeTrackingProfile.manualFocusPosition = value;
    m_activeTrackingProfile.autoZoom = false;
    emit trackingIntentEdited(trackingState(), false);
    m_pendingFocus = value;
    m_dirtyFocus = true;
    m_focusLabel->setText(QString::number(value));
    scheduleFlush();
}

void TrackingControlWidget::setV4l2Mode(bool v4l2Only)
{
    m_v4l2Only = v4l2Only;
    m_trackingGroupBox->setVisible(!v4l2Only);
    if (v4l2Only) {
        m_manualOverrideActive = true;
        // V4L2 exposes camera controls but cannot positively confirm that an
        // SDK tracking mode released gimbal ownership. Keep every movement
        // control fail-closed rather than treating backend choice as proof.
        m_manualControlAuthorized = false;
        m_manualConfirmationPending = false;
        m_trackingConfirmationPending = false;
        m_pendingTrackingIntentGeneration = 0;
        m_trackingIntentResolved = false;
        QSignalBlocker trackingBlocker(m_trackingCheckBox);
        m_trackingCheckBox->setChecked(false);
    } else {
        // SDK cameras must re-establish AI ownership before manual movement.
        m_manualControlAuthorized = false;
        m_trackingIntentResolved = false;
    }
    updatePTZControlsState();
}

void TrackingControlWidget::setMirrored(bool mirrored)
{
    // Sync checkbox without re-emitting mirrorToggled
    if (m_mirrorCheckBox->isChecked() != mirrored) {
        m_mirrorCheckBox->blockSignals(true);
        m_mirrorCheckBox->setChecked(mirrored);
        m_mirrorCheckBox->blockSignals(false);
    }
}
