#include "PTZControlWidget.h"
#include "CameraSettingsWidget.h"
#include "TrackingControlWidget.h"
#include "VideoEffectsWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QShortcut>
#include <QTimer>
#include <cmath>

namespace {

TrackingModeProfile trackingProfileFromPreset(
    const PTZControlWidget::PresetState &preset)
{
    return {
        preset.focusPolicy,
        preset.manualFocusPosition,
        preset.autoZoom,
        preset.trackSpeed
    };
}

TrackingIntentState trackingIntentFromPreset(
    const PTZControlWidget::PresetState &preset)
{
    return {
        preset.trackingEnabled,
        preset.aiMode,
        preset.aiSubMode,
        trackingProfileFromPreset(preset)
    };
}

void updatePresetTracking(PTZControlWidget::PresetState &preset,
                          const TrackingIntentState &tracking)
{
    preset.trackingEnabled = tracking.enabled;
    preset.aiMode = tracking.aiMode;
    preset.aiSubMode = tracking.aiSubMode;
    preset.autoZoom = tracking.profile.autoZoom;
    preset.focusPolicy = tracking.profile.focusPolicy;
    preset.manualFocusPosition = tracking.profile.manualFocusPosition;
    preset.trackSpeed = tracking.profile.trackSpeed;
}

} // namespace

PTZControlWidget::PTZControlWidget(CameraController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_settingsWidget(nullptr)
    , m_trackingWidget(nullptr)
    , m_effectsWidget(nullptr)
    , m_cameraAvailable(controller->isConnected())
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 14, 8, 14);
    layout->setSpacing(14);

    connect(m_controller, &CameraController::cameraConnected, this,
            [this](const CameraController::CameraInfo &) {
                m_cameraAvailable = true;
                for (int i = 0; i < static_cast<int>(m_presets.size()); ++i) {
                    updatePresetLabel(i);
                }
            });
    connect(m_controller, &CameraController::cameraDisconnected, this, [this]() {
        m_cameraAvailable = false;
        const bool recallWasPending = hasPendingRecall();
        cancelPendingRecall();
        if (recallWasPending) {
            emit presetRecallFinished(false);
        }
        for (int i = 0; i < static_cast<int>(m_presets.size()); ++i) {
            updatePresetLabel(i);
        }
    });
    connect(m_controller, &CameraController::trackingIntentStarted,
            this, [this](quint64 intentGeneration) {
                if (m_pendingRecall
                    && intentGeneration
                        > m_pendingRecall->trackingIntentGeneration) {
                    cancelPendingRecall();
                    emit presetRecallFinished(false);
                }
            });
    connect(m_controller, &CameraController::trackingStateConfirmed,
            this, [this](bool trackingEnabled, quint64 intentGeneration) {
                if (!m_pendingRecall || m_completionQueued
                    || m_pendingRecall->trackingIntentGeneration
                        != intentGeneration) {
                    return;
                }
                if (m_pendingRecall->preset.trackingEnabled != trackingEnabled) {
                    cancelPendingRecall();
                    emit presetRecallFinished(false);
                    return;
                }

                const PendingRecall pending = *m_pendingRecall;
                m_completionQueued = true;
                QTimer::singleShot(0, this, [this, pending]() {
                    if (!m_pendingRecall
                        || pending.generation != m_recallGeneration
                        || pending.trackingIntentGeneration
                            != m_pendingRecall->trackingIntentGeneration) {
                        return;
                    }
                    const PresetState preset = m_pendingRecall->preset;
                    m_pendingRecall.reset();
                    m_completionQueued = false;
                    emit presetRecallFinished(finishPresetRecall(preset));
                });
            });
    connect(m_controller, &CameraController::trackingStateConfirmationFailed,
            this, [this](bool, quint64 intentGeneration) {
                if (m_pendingRecall
                    && m_pendingRecall->trackingIntentGeneration
                        == intentGeneration) {
                    cancelPendingRecall();
                    emit presetRecallFinished(false);
                }
            });
    connect(m_controller, &CameraController::trackingStateConfirmationUncertain,
            this, [this](quint64 intentGeneration) {
                if (m_pendingRecall
                    && m_pendingRecall->trackingIntentGeneration
                        == intentGeneration) {
                    cancelPendingRecall();
                    emit presetRecallFinished(false);
                }
            });

    // Presets section
    QGroupBox *presetGroup = new QGroupBox("Camera Presets", this);
    QVBoxLayout *presetLayout = new QVBoxLayout(presetGroup);
    presetLayout->setContentsMargins(16, 16, 16, 16);
    presetLayout->setSpacing(8);

    for (int i = 0; i < 3; ++i) {
        PresetUi presetUi{};
        presetUi.defined = false;
        presetUi.pan = 0.0;
        presetUi.tilt = 0.0;
        presetUi.zoom = 1.0;
        presetUi.sceneDefined = false;
        presetUi.trackingEnabled = false;
        presetUi.aiMode = 0;
        presetUi.aiSubMode = 0;
        presetUi.autoZoom = false;
        presetUi.focusPolicy = TrackingFocusPolicy::Manual;
        presetUi.manualFocusPosition = 50;
        presetUi.trackSpeed = Device::AiTrackSpeedStandard;
        presetUi.paperCrop = {};

        QHBoxLayout *row = new QHBoxLayout();
        row->setSpacing(8);
        QLabel *titleLabel = new QLabel(QString("Preset %1").arg(i + 1), this);
        titleLabel->setStyleSheet("font-weight: 600; font-size: 11px;");
        row->addWidget(titleLabel);

        presetUi.statusLabel = new QLabel("Empty", this);
        presetUi.statusLabel->setStyleSheet("color: palette(mid); font-size: 11px;");
        row->addWidget(presetUi.statusLabel, 1);

        presetUi.recallButton = new QPushButton("Recall", this);
        presetUi.recallButton->setProperty("presetIndex", i);
        presetUi.recallButton->setEnabled(false);
        connect(presetUi.recallButton, &QPushButton::clicked, this, &PTZControlWidget::onRecallPreset);
        const QKeySequence shortcutKey(QString("Ctrl+%1").arg(i + 1));
        presetUi.recallButton->setToolTip(
            QString("Recall this position (%1)").arg(shortcutKey.toString(QKeySequence::NativeText)));
        auto *shortcut = new QShortcut(shortcutKey, this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, i]() {
            recallPreset(i);
        });
        row->addWidget(presetUi.recallButton);

        presetUi.saveButton = new QPushButton("Save", this);
        presetUi.saveButton->setProperty("presetIndex", i);
        connect(presetUi.saveButton, &QPushButton::clicked, this, &PTZControlWidget::onStorePreset);
        row->addWidget(presetUi.saveButton);

        presetLayout->addLayout(row);
        m_presets[static_cast<size_t>(i)] = presetUi;
        updatePresetLabel(i);
    }

    layout->addWidget(presetGroup);

    QLabel *shortcutHint = new QLabel("Recall shortcuts: Ctrl+1, Ctrl+2, Ctrl+3", this);
    presetLayout->addWidget(shortcutHint);

    // Image Quality Presets section
    QGroupBox *imagePresetGroup = new QGroupBox("Image Quality Presets", this);
    QVBoxLayout *imagePresetLayout = new QVBoxLayout(imagePresetGroup);
    imagePresetLayout->setContentsMargins(16, 16, 16, 16);
    imagePresetLayout->setSpacing(8);

    for (int i = 0; i < 3; ++i) {
        ImagePresetUi imagePresetUi{};
        imagePresetUi.state.defined = false;

        QHBoxLayout *row = new QHBoxLayout();
        row->setSpacing(8);
        QLabel *titleLabel = new QLabel(QString("Preset %1").arg(i + 1), this);
        titleLabel->setStyleSheet("font-weight: 600; font-size: 11px;");
        row->addWidget(titleLabel);

        imagePresetUi.statusLabel = new QLabel("Empty", this);
        imagePresetUi.statusLabel->setStyleSheet("color: palette(mid); font-size: 11px;");
        row->addWidget(imagePresetUi.statusLabel, 1);

        imagePresetUi.recallButton = new QPushButton("Recall", this);
        imagePresetUi.recallButton->setProperty("presetIndex", i);
        imagePresetUi.recallButton->setEnabled(false);
        connect(imagePresetUi.recallButton, &QPushButton::clicked, this, &PTZControlWidget::onRecallImagePreset);
        row->addWidget(imagePresetUi.recallButton);

        imagePresetUi.saveButton = new QPushButton("Save", this);
        imagePresetUi.saveButton->setProperty("presetIndex", i);
        connect(imagePresetUi.saveButton, &QPushButton::clicked, this, &PTZControlWidget::onStoreImagePreset);
        row->addWidget(imagePresetUi.saveButton);

        imagePresetLayout->addLayout(row);
        m_imagePresets[static_cast<size_t>(i)] = imagePresetUi;
        updateImagePresetLabel(i);
    }

    layout->addWidget(imagePresetGroup);
    layout->addStretch();
}
void PTZControlWidget::applyPresetStates(const std::array<PresetState, 3> &presets)
{
    for (int i = 0; i < 3; ++i) {
        auto &ui = m_presets[static_cast<size_t>(i)];
        const auto &preset = presets[static_cast<size_t>(i)];
        ui.defined = preset.defined;
        ui.pan = preset.pan;
        ui.tilt = preset.tilt;
        ui.zoom = preset.zoom;
        ui.sceneDefined = preset.sceneDefined;
        ui.trackingEnabled = preset.trackingEnabled;
        ui.aiMode = preset.aiMode;
        ui.aiSubMode = preset.aiSubMode;
        ui.autoZoom = preset.autoZoom;
        ui.focusPolicy = preset.focusPolicy;
        ui.manualFocusPosition = preset.manualFocusPosition;
        ui.trackSpeed = preset.trackSpeed;
        ui.paperCrop = preset.paperCrop;
        updatePresetLabel(i);
    }
}

std::array<PTZControlWidget::PresetState, 3> PTZControlWidget::currentPresets() const
{
    std::array<PresetState, 3> out{};
    for (int i = 0; i < 3; ++i) {
        const auto &ui = m_presets[static_cast<size_t>(i)];
        out[static_cast<size_t>(i)] = {
            ui.defined,
            ui.pan,
            ui.tilt,
            ui.zoom,
            ui.sceneDefined,
            ui.trackingEnabled,
            ui.aiMode,
            ui.aiSubMode,
            ui.autoZoom,
            ui.focusPolicy,
            ui.manualFocusPosition,
            ui.trackSpeed,
            ui.paperCrop
        };
    }
    return out;
}

bool PTZControlWidget::canRecallPreset(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        return false;
    }

    const auto &preset = m_presets[static_cast<size_t>(index)];
    const TrackingModeProfile profile{
        preset.focusPolicy,
        preset.manualFocusPosition,
        preset.autoZoom,
        preset.trackSpeed
    };
    return preset.defined
        && std::isfinite(preset.pan) && preset.pan >= -1.0 && preset.pan <= 1.0
        && std::isfinite(preset.tilt) && preset.tilt >= -1.0 && preset.tilt <= 1.0
        && std::isfinite(preset.zoom) && preset.zoom >= 1.0 && preset.zoom <= 2.0
        && (!preset.sceneDefined
            || (preset.paperCrop.isValid()
                && isValidTrackingModeProfile(profile)
                && (preset.trackingEnabled
                    || (preset.focusPolicy == TrackingFocusPolicy::Manual
                        && !preset.autoZoom))));
}

bool PTZControlWidget::recallPreset(int index)
{
    if (!m_cameraAvailable || !canRecallPreset(index)) {
        return false;
    }

    const auto &ui = m_presets[static_cast<size_t>(index)];
    PresetState preset = {
        ui.defined,
        ui.pan,
        ui.tilt,
        ui.zoom,
        ui.sceneDefined,
        ui.trackingEnabled,
        ui.aiMode,
        ui.aiSubMode,
        ui.autoZoom,
        ui.focusPolicy,
        ui.manualFocusPosition,
        ui.trackSpeed,
        ui.paperCrop
    };
    if (hasPendingRecall()) {
        cancelPendingRecall();
        emit presetRecallFinished(false);
    }
    const quint64 recallGeneration = ++m_recallGeneration;

    TrackingControlWidget::TrackingApplyResult trackingResult{true, false, 0};
    if (preset.sceneDefined && m_trackingWidget) {
        TrackingIntentState tracking = trackingIntentFromPreset(preset);
        tracking = applyAutomaticPaperCropTrackingPolicy(
            static_cast<int>(preset.paperCrop.mode),
            m_trackingWidget->hasTiny2Capabilities(),
            tracking,
            m_trackingWidget->modeProfiles());
        updatePresetTracking(preset, tracking);
        trackingResult = m_trackingWidget->applyTrackingState(tracking);
    } else if (!preset.sceneDefined && m_trackingWidget) {
        auto tracking = m_trackingWidget->trackingState();
        tracking.enabled = false;
        tracking.profile.focusPolicy = TrackingFocusPolicy::Manual;
        tracking.profile.autoZoom = false;
        updatePresetTracking(preset, tracking);
        trackingResult = m_trackingWidget->applyTrackingState(tracking);
    }

    if (!trackingResult.accepted) {
        return false;
    }
    if (trackingResult.confirmationPending) {
        // Never send PTZ/zoom while AI ownership is unconfirmed. Continue the
        // accepted recall only after CameraController reports a fresh match.
        m_pendingRecall = PendingRecall{
            preset, recallGeneration, trackingResult.intentGeneration};
        return true;
    }
    return finishPresetRecall(preset);
}

void PTZControlWidget::cancelPendingRecall()
{
    // Always invalidate a zero-delay continuation, even after confirmation has
    // moved it out of the transport-pending phase.
    ++m_recallGeneration;
    m_pendingRecall.reset();
    m_completionQueued = false;
}

bool PTZControlWidget::finishPresetRecall(const PresetState &preset)
{
    if (!m_cameraAvailable) {
        return false;
    }

    if (m_trackingWidget) {
        if (preset.sceneDefined) {
            const TrackingControlWidget::TrackingState expected =
                trackingIntentFromPreset(preset);
            if (!m_trackingWidget->matchesTrackingState(expected)) {
                return false;
            }
        } else if (!m_trackingWidget->isManualControlEnabled()) {
            return false;
        }
    }

    const bool positionApplied =
        !(preset.sceneDefined && preset.trackingEnabled);
    if (positionApplied) {
        if (!m_controller->setPanTilt(preset.pan, preset.tilt)) {
            return false;
        }
        if (!m_controller->setZoom(preset.zoom)) {
            return false;
        }
    }

    // Crop is local and infallible, so apply it last. A failed tracking/PTZ
    // transition therefore cannot persist a partial crop-only scene recall.
    if (preset.sceneDefined && m_effectsWidget) {
        m_effectsWidget->applyPaperCropForScene(preset.paperCrop);
    }
    emit sceneIntentApplied(
        trackingIntentFromPreset(preset), positionApplied,
        preset.pan, preset.tilt, preset.zoom, preset.paperCrop);
    return true;
}

void PTZControlWidget::onRecallPreset()
{
    auto *button = qobject_cast<QPushButton*>(sender());
    if (button) {
        recallPreset(button->property("presetIndex").toInt());
    }
}

void PTZControlWidget::onStorePreset()
{
    if (!m_cameraAvailable) {
        return;
    }

    auto *button = qobject_cast<QPushButton*>(sender());
    if (!button) {
        return;
    }
    int index = button->property("presetIndex").toInt();
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        return;
    }

    auto state = m_controller->getCurrentState();
    auto &preset = m_presets[static_cast<size_t>(index)];
    preset.defined = true;
    preset.pan = state.pan;
    preset.tilt = state.tilt;
    preset.zoom = state.zoom;
    if (m_trackingWidget) {
        const auto tracking = m_trackingWidget->trackingState();
        preset.sceneDefined = true;
        preset.trackingEnabled = tracking.enabled;
        preset.aiMode = tracking.aiMode;
        preset.aiSubMode = tracking.aiSubMode;
        preset.autoZoom = tracking.profile.autoZoom;
        preset.focusPolicy = tracking.profile.focusPolicy;
        preset.manualFocusPosition = tracking.profile.manualFocusPosition;
        preset.trackSpeed = tracking.profile.trackSpeed;
    }
    if (m_effectsWidget) {
        preset.paperCrop = m_effectsWidget->settings().paperCrop;
    }
    updatePresetLabel(index);

    emit presetUpdated(index);
}

void PTZControlWidget::updatePresetLabel(int index)
{
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        return;
    }
    auto &preset = m_presets[static_cast<size_t>(index)];
    if (!preset.statusLabel) {
        return;
    }

    if (preset.defined) {
        QString modeLabel = tr("Position");
        if (preset.sceneDefined) {
            if (!preset.trackingEnabled) {
                modeLabel = tr("Manual");
            } else if (preset.aiMode == 5) {
                modeLabel = tr("Desk tracking");
            } else {
                modeLabel = tr("Tracking");
            }
        }

        QString cropLabel;
        if (preset.paperCrop.mode == PaperCropMode::Automatic) {
            cropLabel = tr(" · Auto crop");
        } else if (preset.paperCrop.mode == PaperCropMode::Manual) {
            cropLabel = tr(" · Manual crop");
        }

        if (preset.sceneDefined && preset.trackingEnabled) {
            preset.statusLabel->setText(modeLabel + cropLabel);
        } else {
            preset.statusLabel->setText(
                QString("%1 · Pan %2, Tilt %3, Zoom %4x%5")
                    .arg(modeLabel)
                    .arg(preset.pan, 0, 'f', 2)
                    .arg(preset.tilt, 0, 'f', 2)
                    .arg(preset.zoom, 0, 'f', 1)
                    .arg(cropLabel));
        }
    } else {
        preset.statusLabel->setText("Empty");
    }

    if (preset.recallButton) {
        preset.recallButton->setEnabled(m_cameraAvailable && preset.defined);
    }
    if (preset.saveButton) {
        preset.saveButton->setEnabled(m_cameraAvailable);
    }
}

void PTZControlWidget::onStoreImagePreset()
{
    if (!m_settingsWidget) {
        return;
    }

    auto *button = qobject_cast<QPushButton*>(sender());
    if (!button) {
        return;
    }
    int index = button->property("presetIndex").toInt();
    if (index < 0 || index >= static_cast<int>(m_imagePresets.size())) {
        return;
    }

    auto &preset = m_imagePresets[static_cast<size_t>(index)];
    preset.state.defined = true;
    preset.state.hdrEnabled = m_settingsWidget->isHDREnabled();
    preset.state.fovMode = m_settingsWidget->getFOVMode();
    preset.state.faceAE = m_settingsWidget->isFaceAEEnabled();
    preset.state.faceFocus = m_settingsWidget->isFaceFocusEnabled();
    preset.state.brightnessAuto = m_settingsWidget->isBrightnessAuto();
    preset.state.brightness = m_settingsWidget->getBrightness();
    preset.state.contrastAuto = m_settingsWidget->isContrastAuto();
    preset.state.contrast = m_settingsWidget->getContrast();
    preset.state.saturationAuto = m_settingsWidget->isSaturationAuto();
    preset.state.saturation = m_settingsWidget->getSaturation();
    preset.state.whiteBalance = m_settingsWidget->getWhiteBalance();
    preset.state.whiteBalanceKelvin = m_settingsWidget->getWhiteBalanceKelvin();
    updateImagePresetLabel(index);

    emit imagePresetUpdated(index);
}

void PTZControlWidget::onRecallImagePreset()
{
    if (!m_settingsWidget) {
        return;
    }

    auto *button = qobject_cast<QPushButton*>(sender());
    if (!button) {
        return;
    }
    int index = button->property("presetIndex").toInt();
    if (index < 0 || index >= static_cast<int>(m_imagePresets.size())) {
        return;
    }

    auto &preset = m_imagePresets[static_cast<size_t>(index)];
    if (!preset.state.defined) {
        return;
    }

    m_settingsWidget->setHDREnabled(preset.state.hdrEnabled);
    m_settingsWidget->setFOVMode(preset.state.fovMode);
    m_settingsWidget->setFaceAEEnabled(preset.state.faceAE);
    m_settingsWidget->setFaceFocusEnabled(preset.state.faceFocus);
    m_settingsWidget->setBrightnessAuto(preset.state.brightnessAuto);
    m_settingsWidget->setBrightness(preset.state.brightness);
    m_settingsWidget->setContrastAuto(preset.state.contrastAuto);
    m_settingsWidget->setContrast(preset.state.contrast);
    m_settingsWidget->setSaturationAuto(preset.state.saturationAuto);
    m_settingsWidget->setSaturation(preset.state.saturation);
    m_settingsWidget->setWhiteBalance(preset.state.whiteBalance);
    m_settingsWidget->setWhiteBalanceKelvin(preset.state.whiteBalanceKelvin);
}

void PTZControlWidget::updateImagePresetLabel(int index)
{
    if (index < 0 || index >= static_cast<int>(m_imagePresets.size())) {
        return;
    }
    auto &preset = m_imagePresets[static_cast<size_t>(index)];
    if (!preset.statusLabel) {
        return;
    }

    if (preset.state.defined) {
        preset.statusLabel->setText("Saved");
    } else {
        preset.statusLabel->setText("Empty");
    }

    if (preset.recallButton) {
        preset.recallButton->setEnabled(preset.state.defined);
    }
}

void PTZControlWidget::applyImagePresetStates(const std::array<ImagePresetState, 3> &presets)
{
    for (int i = 0; i < 3; ++i) {
        auto &ui = m_imagePresets[static_cast<size_t>(i)];
        ui.state = presets[static_cast<size_t>(i)];
        updateImagePresetLabel(i);
    }
}

std::array<PTZControlWidget::ImagePresetState, 3> PTZControlWidget::currentImagePresets() const
{
    std::array<ImagePresetState, 3> out{};
    for (int i = 0; i < 3; ++i) {
        const auto &ui = m_imagePresets[static_cast<size_t>(i)];
        out[static_cast<size_t>(i)] = ui.state;
    }
    return out;
}
