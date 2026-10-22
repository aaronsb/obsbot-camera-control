#include "CameraSettingsWidget.h"
#include "ShutterSpeed.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <algorithm>

CameraSettingsWidget::CameraSettingsWidget(CameraController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_userInitiated(false)
{
    // Create debounce timer for command completion
    m_commandTimer = new QTimer(this);
    m_commandTimer->setSingleShot(true);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 14, 8, 14);
    layout->setSpacing(14);

    m_advancedGroupBox = new QGroupBox("Advanced Camera Settings", this);
    QVBoxLayout *groupLayout = new QVBoxLayout(m_advancedGroupBox);
    groupLayout->setContentsMargins(16, 16, 16, 16);
    groupLayout->setSpacing(10);

    // HDR
    m_hdrCheckBox = new QCheckBox("HDR (High Dynamic Range)", this);
    m_hdrCheckBox->setToolTip("Enhance image quality in high-contrast scenes");
    connect(m_hdrCheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onHDRToggled);
    groupLayout->addWidget(m_hdrCheckBox);

    // FOV
    QHBoxLayout *fovLayout = new QHBoxLayout();
    fovLayout->addWidget(new QLabel("Field of View:", this));
    m_fovComboBox = new QComboBox(this);
    m_fovComboBox->addItem("Wide (86°)", 0);
    m_fovComboBox->addItem("Medium (78°)", 1);
    m_fovComboBox->addItem("Narrow (65°)", 2);
    m_fovComboBox->setToolTip("Change the camera's field of view");
    connect(m_fovComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraSettingsWidget::onFOVChanged);
    fovLayout->addWidget(m_fovComboBox);
    fovLayout->addStretch();
    groupLayout->addLayout(fovLayout);

    // Face AE
    m_faceAECheckBox = new QCheckBox("Face-based Auto Exposure", this);
    m_faceAECheckBox->setToolTip("Optimize exposure for faces");
    connect(m_faceAECheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onFaceAEToggled);
    groupLayout->addWidget(m_faceAECheckBox);

    // Face Focus
    m_faceFocusCheckBox = new QCheckBox("Face-based Auto Focus", this);
    m_faceFocusCheckBox->setToolTip("Optimize focus for faces");
    connect(m_faceFocusCheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onFaceFocusToggled);
    groupLayout->addWidget(m_faceFocusCheckBox);

    layout->addWidget(m_advancedGroupBox);

    // Image Controls Group
    QGroupBox *imageGroupBox = new QGroupBox("Image Controls", this);
    QVBoxLayout *imageLayout = new QVBoxLayout(imageGroupBox);
    imageLayout->setContentsMargins(16, 16, 16, 16);
    imageLayout->setSpacing(10);

    // Brightness
    QHBoxLayout *brightnessLayout = new QHBoxLayout();
    m_brightnessAutoCheckBox = new QCheckBox("Auto", this);
    m_brightnessAutoCheckBox->setToolTip("Enable automatic brightness (disables manual control)");
    connect(m_brightnessAutoCheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onBrightnessAutoToggled);
    brightnessLayout->addWidget(m_brightnessAutoCheckBox);
    brightnessLayout->addWidget(new QLabel("Brightness:", this));
    m_brightnessSlider = new QSlider(Qt::Horizontal, this);
    m_brightnessSlider->setRange(0, 255);
    m_brightnessSlider->setToolTip("Adjust image brightness (0-255)");
    connect(m_brightnessSlider, &QSlider::valueChanged, this, &CameraSettingsWidget::onBrightnessChanged);
    brightnessLayout->addWidget(m_brightnessSlider);
    imageLayout->addLayout(brightnessLayout);

    // Contrast
    QHBoxLayout *contrastLayout = new QHBoxLayout();
    m_contrastAutoCheckBox = new QCheckBox("Auto", this);
    m_contrastAutoCheckBox->setToolTip("Enable automatic contrast (disables manual control)");
    connect(m_contrastAutoCheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onContrastAutoToggled);
    contrastLayout->addWidget(m_contrastAutoCheckBox);
    contrastLayout->addWidget(new QLabel("Contrast:", this));
    m_contrastSlider = new QSlider(Qt::Horizontal, this);
    m_contrastSlider->setRange(0, 255);
    m_contrastSlider->setToolTip("Adjust image contrast (0-255)");
    connect(m_contrastSlider, &QSlider::valueChanged, this, &CameraSettingsWidget::onContrastChanged);
    contrastLayout->addWidget(m_contrastSlider);
    imageLayout->addLayout(contrastLayout);

    // Saturation
    QHBoxLayout *saturationLayout = new QHBoxLayout();
    m_saturationAutoCheckBox = new QCheckBox("Auto", this);
    m_saturationAutoCheckBox->setToolTip("Enable automatic saturation (disables manual control)");
    connect(m_saturationAutoCheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onSaturationAutoToggled);
    saturationLayout->addWidget(m_saturationAutoCheckBox);
    saturationLayout->addWidget(new QLabel("Saturation:", this));
    m_saturationSlider = new QSlider(Qt::Horizontal, this);
    m_saturationSlider->setRange(0, 255);
    m_saturationSlider->setToolTip("Adjust color saturation (0-255)");
    connect(m_saturationSlider, &QSlider::valueChanged, this, &CameraSettingsWidget::onSaturationChanged);
    saturationLayout->addWidget(m_saturationSlider);
    imageLayout->addLayout(saturationLayout);

    // White Balance
    QHBoxLayout *wbLayout = new QHBoxLayout();
    wbLayout->addWidget(new QLabel("White Balance:", this));
    m_whiteBalanceComboBox = new QComboBox(this);
    m_whiteBalanceComboBox->addItem("Auto", static_cast<int>(Device::DevWhiteBalanceAuto));
    m_whiteBalanceComboBox->addItem("Daylight", static_cast<int>(Device::DevWhiteBalanceDaylight));
    m_whiteBalanceComboBox->addItem("Fluorescent", static_cast<int>(Device::DevWhiteBalanceFluorescent));
    m_whiteBalanceComboBox->addItem("Tungsten", static_cast<int>(Device::DevWhiteBalanceTungsten));
    m_whiteBalanceComboBox->addItem("Flash", static_cast<int>(Device::DevWhiteBalanceFlash));
    m_whiteBalanceComboBox->addItem("Fine", static_cast<int>(Device::DevWhiteBalanceFine));
    m_whiteBalanceComboBox->addItem("Cloudy", static_cast<int>(Device::DevWhiteBalanceCloudy));
    m_whiteBalanceComboBox->addItem("Shade", static_cast<int>(Device::DevWhiteBalanceShade));
    m_whiteBalanceComboBox->addItem("Manual (Kelvin)", static_cast<int>(Device::DevWhiteBalanceManual));
    m_whiteBalanceComboBox->setToolTip("Adjust white balance for lighting conditions");
    connect(m_whiteBalanceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraSettingsWidget::onWhiteBalanceChanged);
    wbLayout->addWidget(m_whiteBalanceComboBox);
    wbLayout->addStretch();
    imageLayout->addLayout(wbLayout);

    QHBoxLayout *wbKelvinLayout = new QHBoxLayout();
    wbKelvinLayout->setContentsMargins(20, 0, 0, 0);
    wbKelvinLayout->setSpacing(8);
    m_whiteBalanceKelvinSlider = new QSlider(Qt::Horizontal, this);
    m_whiteBalanceKelvinSlider->setRange(2000, 10000);
    m_whiteBalanceKelvinSlider->setSingleStep(100);
    m_whiteBalanceKelvinSlider->setEnabled(false);
    m_whiteBalanceKelvinSlider->setValue(5000);
    connect(m_whiteBalanceKelvinSlider, &QSlider::valueChanged,
            this, &CameraSettingsWidget::onWhiteBalanceKelvinChanged);
    wbKelvinLayout->addWidget(m_whiteBalanceKelvinSlider, 1);
    m_whiteBalanceKelvinLabel = new QLabel("5000 K", this);
    m_whiteBalanceKelvinLabel->setEnabled(false);
    wbKelvinLayout->addWidget(m_whiteBalanceKelvinLabel);
    imageLayout->addLayout(wbKelvinLayout);

    // Exposure
    QHBoxLayout *exposureLayout = new QHBoxLayout();
    m_exposureAutoCheckBox = new QCheckBox("Auto", this);
    m_exposureAutoCheckBox->setToolTip("Enable automatic exposure (disables manual shutter control)");
    connect(m_exposureAutoCheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onExposureAutoToggled);
    exposureLayout->addWidget(m_exposureAutoCheckBox);
    exposureLayout->addWidget(new QLabel("Exposure:", this));
    m_exposureSlider = new QSlider(Qt::Horizontal, this);
    m_exposureSlider->setRange(ShutterSpeed::kFirstIndex, ShutterSpeed::kLastIndex);
    m_exposureSlider->setValue(ShutterSpeed::kDefaultIndex);
    m_exposureSlider->setToolTip("Adjust manual shutter value");
    connect(m_exposureSlider, &QSlider::valueChanged, this, &CameraSettingsWidget::onExposureChanged);
    exposureLayout->addWidget(m_exposureSlider, 1);
    m_exposureValueLabel = new QLabel("1/30s", this);
    m_exposureValueLabel->setObjectName("exposureValueLabel");
    m_exposureValueLabel->setMinimumWidth(56);
    exposureLayout->addWidget(m_exposureValueLabel);
    imageLayout->addLayout(exposureLayout);

    // ISO / Gain
    QHBoxLayout *isoLayout = new QHBoxLayout();
    m_isoLabel = new QLabel("ISO:", this);
    isoLayout->addWidget(m_isoLabel);
    m_isoSlider = new QSlider(Qt::Horizontal, this);
    m_isoSlider->setObjectName("isoSlider");
    m_isoSlider->setRange(1, 64);
    m_isoSlider->setToolTip("Adjust sensor gain / ISO (lower = less noise, higher = brighter in low light). "
                            "ISO value shown is approximated from OBSBOT's published 100-6400 range.");
    connect(m_isoSlider, &QSlider::valueChanged, this, &CameraSettingsWidget::onISOChanged);
    isoLayout->addWidget(m_isoSlider, 1);
    m_isoValueLabel = new QLabel("ISO 100", this);
    m_isoValueLabel->setMinimumWidth(56);
    isoLayout->addWidget(m_isoValueLabel);
    imageLayout->addLayout(isoLayout);

    QPushButton *resetButton = new QPushButton("Reset to Defaults", this);
    resetButton->setToolTip("Restore HDR, field of view, face AE/focus and all image controls to their defaults");
    connect(resetButton, &QPushButton::clicked, this, &CameraSettingsWidget::resetToDefaults);
    imageLayout->addWidget(resetButton);

    layout->addWidget(imageGroupBox);
    layout->addStretch();
}

void CameraSettingsWidget::resetToDefaults()
{
    m_userInitiated = true;

    setHDREnabled(false);
    m_controller->setHDR(false);
    setFOVMode(0);
    m_controller->setFOV(0);
    setFaceAEEnabled(false);
    m_controller->setFaceAE(false);
    setFaceFocusEnabled(false);
    m_controller->setFaceFocus(false);

    // The manual setters are no-ops while the auto flag is set, so drop it
    // first, push the neutral value, then restore auto.
    const int neutral = 128;
    auto resetAutoControl = [&](const CameraController::ParamRange &range,
                                void (CameraController::*setAuto)(bool),
                                bool (CameraController::*setValue)(int),
                                void (CameraSettingsWidget::*setWidgetAuto)(bool),
                                void (CameraSettingsWidget::*setWidgetValue)(int)) {
        const int value = range.valid ? range.defaultValue : neutral;
        (m_controller->*setAuto)(false);
        (m_controller->*setValue)(value);
        (m_controller->*setAuto)(true);
        (this->*setWidgetValue)(value);
        (this->*setWidgetAuto)(true);
    };
    resetAutoControl(m_controller->getBrightnessRange(), &CameraController::setBrightnessAuto,
                     &CameraController::setBrightness, &CameraSettingsWidget::setBrightnessAuto,
                     &CameraSettingsWidget::setBrightness);
    resetAutoControl(m_controller->getContrastRange(), &CameraController::setContrastAuto,
                     &CameraController::setContrast, &CameraSettingsWidget::setContrastAuto,
                     &CameraSettingsWidget::setContrast);
    resetAutoControl(m_controller->getSaturationRange(), &CameraController::setSaturationAuto,
                     &CameraController::setSaturation, &CameraSettingsWidget::setSaturationAuto,
                     &CameraSettingsWidget::setSaturation);

    const int wbAuto = static_cast<int>(Device::DevWhiteBalanceAuto);
    setWhiteBalance(wbAuto);
    m_controller->setWhiteBalance(wbAuto);

    setExposureAuto(true);
    m_controller->setExposureAuto(true);

    const auto isoRange = m_controller->getISORange();
    const int isoDefault = isoRange.valid ? isoRange.defaultValue : 1;
    setISO(isoDefault);
    m_controller->setISO(isoDefault);

    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onHDRToggled(bool checked)
{
    m_userInitiated = true;
    m_controller->setHDR(checked);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onFOVChanged(int index)
{
    m_userInitiated = true;
    m_controller->setFOV(index);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onFaceAEToggled(bool checked)
{
    m_userInitiated = true;
    m_controller->setFaceAE(checked);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onFaceFocusToggled(bool checked)
{
    m_userInitiated = true;
    m_controller->setFaceFocus(checked);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onBrightnessAutoToggled(bool checked)
{
    m_userInitiated = true;
    m_brightnessSlider->setEnabled(!checked);
    if (checked) {
        // Reset to default (neutral) value - send BEFORE setting auto flag
        auto range = m_controller->getBrightnessRange();
        int defaultVal = range.valid ? range.defaultValue : 50;
        m_brightnessSlider->blockSignals(true);
        m_brightnessSlider->setValue(defaultVal);
        m_brightnessSlider->blockSignals(false);
        m_controller->setBrightness(defaultVal);
    } else {
        // When switching to manual, send current slider value
        m_controller->setBrightness(m_brightnessSlider->value());
    }
    // Set auto flag AFTER sending the value
    m_controller->setBrightnessAuto(checked);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onBrightnessChanged(int value)
{
    m_userInitiated = true;
    m_controller->setBrightness(value);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onContrastAutoToggled(bool checked)
{
    m_userInitiated = true;
    m_contrastSlider->setEnabled(!checked);
    if (checked) {
        // Reset to default (neutral) value - send BEFORE setting auto flag
        auto range = m_controller->getContrastRange();
        int defaultVal = range.valid ? range.defaultValue : 50;
        m_contrastSlider->blockSignals(true);
        m_contrastSlider->setValue(defaultVal);
        m_contrastSlider->blockSignals(false);
        m_controller->setContrast(defaultVal);
    } else {
        // When switching to manual, send current slider value
        m_controller->setContrast(m_contrastSlider->value());
    }
    // Set auto flag AFTER sending the value
    m_controller->setContrastAuto(checked);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onContrastChanged(int value)
{
    m_userInitiated = true;
    m_controller->setContrast(value);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onSaturationAutoToggled(bool checked)
{
    m_userInitiated = true;
    m_saturationSlider->setEnabled(!checked);
    if (checked) {
        // Reset to default (neutral) value - send BEFORE setting auto flag
        auto range = m_controller->getSaturationRange();
        int defaultVal = range.valid ? range.defaultValue : 50;
        m_saturationSlider->blockSignals(true);
        m_saturationSlider->setValue(defaultVal);
        m_saturationSlider->blockSignals(false);
        m_controller->setSaturation(defaultVal);
    } else {
        // When switching to manual, send current slider value
        m_controller->setSaturation(m_saturationSlider->value());
    }
    // Set auto flag AFTER sending the value
    m_controller->setSaturationAuto(checked);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onSaturationChanged(int value)
{
    m_userInitiated = true;
    m_controller->setSaturation(value);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onWhiteBalanceChanged(int index)
{
    Q_UNUSED(index);
    const int mode = m_whiteBalanceComboBox->currentData().toInt();
    updateWhiteBalanceControls(mode);

    m_userInitiated = true;
    if (mode == static_cast<int>(Device::DevWhiteBalanceManual)) {
        m_controller->setWhiteBalanceManual(m_whiteBalanceKelvinSlider->value());
    } else {
        m_controller->setWhiteBalance(mode);
    }
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onWhiteBalanceKelvinChanged(int value)
{
    updateWhiteBalanceKelvinLabel(value);
    if (m_whiteBalanceComboBox->currentData().toInt() != static_cast<int>(Device::DevWhiteBalanceManual)) {
        return;
    }

    m_userInitiated = true;
    m_controller->setWhiteBalanceManual(value);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onExposureAutoToggled(bool checked)
{
    m_userInitiated = true;
    m_exposureSlider->setEnabled(!checked);
    m_controller->setExposureAuto(checked);
    if (!checked) {
        // Re-assert the current slider value now that manual mode is active
        m_controller->setExposure(m_exposureSlider->value());
    }
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onExposureChanged(int value)
{
    updateExposureValueLabel(value);
    m_userInitiated = true;
    m_controller->setExposure(value);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onISOChanged(int value)
{
    updateISOValueLabel(value);
    m_userInitiated = true;
    m_controller->setISO(value);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::updateFromState(const CameraController::CameraState &state)
{
    applyControlRanges();

    // Only update if state differs, not user-initiated, command timer expired, and not settling
    bool commandInFlight = m_commandTimer->isActive();
    bool isSettling = m_controller->isSettling();

    if (!m_userInitiated && !commandInFlight && !isSettling) {
        if (m_hdrCheckBox->isChecked() != state.hdrEnabled) {
            m_hdrCheckBox->blockSignals(true);
            m_hdrCheckBox->setChecked(state.hdrEnabled);
            m_hdrCheckBox->blockSignals(false);
        }

        if (m_fovComboBox->currentIndex() != state.fovMode) {
            m_fovComboBox->blockSignals(true);
            m_fovComboBox->setCurrentIndex(state.fovMode);
            m_fovComboBox->blockSignals(false);
        }

        if (m_faceAECheckBox->isChecked() != state.faceAEEnabled) {
            m_faceAECheckBox->blockSignals(true);
            m_faceAECheckBox->setChecked(state.faceAEEnabled);
            m_faceAECheckBox->blockSignals(false);
        }

        if (m_faceFocusCheckBox->isChecked() != state.faceFocusEnabled) {
            m_faceFocusCheckBox->blockSignals(true);
            m_faceFocusCheckBox->setChecked(state.faceFocusEnabled);
            m_faceFocusCheckBox->blockSignals(false);
        }

        // Exposure auto is a real hardware feature (unlike brightness/contrast/saturation), so sync it
        if (m_exposureAutoCheckBox->isChecked() != state.exposureAuto) {
            m_exposureAutoCheckBox->blockSignals(true);
            m_exposureAutoCheckBox->setChecked(state.exposureAuto);
            m_exposureAutoCheckBox->blockSignals(false);
            m_exposureSlider->setEnabled(!state.exposureAuto);
        }

        // Image controls - DON'T update auto checkboxes from camera state
        // These are UI-only flags (camera doesn't have auto brightness/contrast/saturation)
        // The checkboxes are only updated via user interaction or config load
    }

    // Always update slider values when in auto mode (to show polled values)
    // When not in auto mode, only update if not user-initiated
    if (!m_userInitiated && !commandInFlight && !isSettling) {
        if (m_brightnessSlider->value() != state.brightness) {
            m_brightnessSlider->blockSignals(true);
            m_brightnessSlider->setValue(state.brightness);
            m_brightnessSlider->blockSignals(false);
        }

        if (m_contrastSlider->value() != state.contrast) {
            m_contrastSlider->blockSignals(true);
            m_contrastSlider->setValue(state.contrast);
            m_contrastSlider->blockSignals(false);
        }

        if (m_saturationSlider->value() != state.saturation) {
            m_saturationSlider->blockSignals(true);
            m_saturationSlider->setValue(state.saturation);
            m_saturationSlider->blockSignals(false);
        }

        if (m_exposureSlider->value() != state.exposure) {
            m_exposureSlider->blockSignals(true);
            m_exposureSlider->setValue(state.exposure);
            m_exposureSlider->blockSignals(false);
            updateExposureValueLabel(state.exposure);
        }

        if (m_isoSlider->value() != state.iso) {
            m_isoSlider->blockSignals(true);
            m_isoSlider->setValue(state.iso);
            m_isoSlider->blockSignals(false);
            updateISOValueLabel(state.iso);
        }
    }

    int desiredWbIndex = m_whiteBalanceComboBox->findData(state.whiteBalance);
    if (!m_userInitiated && !commandInFlight && !isSettling && desiredWbIndex >= 0 &&
        m_whiteBalanceComboBox->currentIndex() != desiredWbIndex) {
        m_whiteBalanceComboBox->blockSignals(true);
        m_whiteBalanceComboBox->setCurrentIndex(desiredWbIndex);
        m_whiteBalanceComboBox->blockSignals(false);
    }

    updateWhiteBalanceControls(state.whiteBalance);

    if (!m_userInitiated && !commandInFlight && !isSettling) {
        int clampedKelvin = std::clamp(state.whiteBalanceKelvin,
            m_whiteBalanceKelvinSlider->minimum(), m_whiteBalanceKelvinSlider->maximum());
        if (m_whiteBalanceKelvinSlider->value() != clampedKelvin) {
            m_whiteBalanceKelvinSlider->blockSignals(true);
            m_whiteBalanceKelvinSlider->setValue(clampedKelvin);
            m_whiteBalanceKelvinSlider->blockSignals(false);
        }
    }
    updateWhiteBalanceKelvinLabel(m_whiteBalanceKelvinSlider->value());

    // Clear user-initiated flag when command timer expires
    if (!commandInFlight && m_userInitiated) {
        m_userInitiated = false;
    }
}

void CameraSettingsWidget::applyControlRanges()
{
    if (!m_controller) {
        return;
    }

    const auto applyRange = [](const CameraController::ParamRange &range, QSlider *slider, bool &applied, const QString &tooltip) {
        if (!slider || !range.valid) {
            return;
        }
        const int step = std::max(1, range.step);
        const bool rangeChanged = !applied || slider->minimum() != range.min || slider->maximum() != range.max;
        if (rangeChanged) {
            int value = std::clamp(slider->value(), range.min, range.max);
            slider->blockSignals(true);
            slider->setRange(range.min, range.max);
            slider->setSingleStep(step);
            slider->setPageStep(step * 5);
            slider->setValue(value);
            slider->blockSignals(false);
            applied = true;
        } else {
            slider->setSingleStep(step);
            slider->setPageStep(step * 5);
        }
        slider->setToolTip(tooltip.arg(range.min).arg(range.max));
    };

    applyRange(m_controller->getBrightnessRange(), m_brightnessSlider, m_brightnessRangeApplied,
               QStringLiteral("Adjust image brightness (%1-%2)"));
    applyRange(m_controller->getContrastRange(), m_contrastSlider, m_contrastRangeApplied,
               QStringLiteral("Adjust image contrast (%1-%2)"));
    applyRange(m_controller->getSaturationRange(), m_saturationSlider, m_saturationRangeApplied,
               QStringLiteral("Adjust color saturation (%1-%2)"));
    applyRange(m_controller->getExposureRange(), m_exposureSlider, m_exposureRangeApplied,
               QStringLiteral("Adjust manual shutter value (%1-%2)"));
    updateExposureValueLabel(m_exposureSlider->value());
    const auto isoRange = m_controller->getISORange();
    applyRange(isoRange, m_isoSlider, m_isoRangeApplied,
               QStringLiteral("Adjust sensor gain / ISO (%1-%2)"));
    updateISOValueLabel(m_isoSlider->value());
    // The controller skips ISO on cameras that report no ISO range, so don't offer it there
    const bool showIso = !m_controller->isConnected() || isoRange.valid;
    m_isoLabel->setVisible(showIso);
    m_isoSlider->setVisible(showIso);
    m_isoValueLabel->setVisible(showIso);

    const auto wbRange = m_controller->getWhiteBalanceKelvinRange();
    if (wbRange.valid) {
        applyRange(wbRange, m_whiteBalanceKelvinSlider, m_whiteBalanceRangeApplied,
                   QStringLiteral("Manual color temperature (%1-%2 K)"));
        updateWhiteBalanceKelvinLabel(m_whiteBalanceKelvinSlider->value());
    }
}

void CameraSettingsWidget::updateWhiteBalanceControls(int mode)
{
    const bool manualSelected = (mode == static_cast<int>(Device::DevWhiteBalanceManual));
    const bool rangeAvailable = m_controller->getWhiteBalanceKelvinRange().valid;
    const bool enableManual = manualSelected && rangeAvailable;
    m_whiteBalanceKelvinSlider->setEnabled(enableManual);
    m_whiteBalanceKelvinLabel->setEnabled(enableManual);
}

void CameraSettingsWidget::setV4l2Mode(bool v4l2Only)
{
    m_advancedGroupBox->setVisible(!v4l2Only);
}

void CameraSettingsWidget::updateWhiteBalanceKelvinLabel(int value)
{
    if (m_whiteBalanceKelvinLabel) {
        m_whiteBalanceKelvinLabel->setText(QString::number(value) + QStringLiteral(" K"));
    }
}

void CameraSettingsWidget::updateExposureValueLabel(int value)
{
    if (m_exposureValueLabel) {
        m_exposureValueLabel->setText(QString::fromStdString(ShutterSpeed::label(value)));
    }
}

void CameraSettingsWidget::updateISOValueLabel(int value)
{
    if (m_isoValueLabel) {
        // The SDK/V4L2 gain control is a raw 1-64 step value. OBSBOT's published spec lists
        // the Meet-series ISO range as 100-6400, which lines up exactly with gain*100
        // (gain=1 -> ISO 100 base sensitivity, gain=64 -> ISO 6400). This isn't a value
        // documented by the SDK itself, so it's an inferred-but-consistent approximation.
        m_isoValueLabel->setText(QStringLiteral("ISO %1").arg(value * 100));
    }
}
