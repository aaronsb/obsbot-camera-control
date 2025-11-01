#include "CameraSettingsWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
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

    QGroupBox *groupBox = new QGroupBox("Advanced Camera Settings", this);
    QVBoxLayout *groupLayout = new QVBoxLayout(groupBox);
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

    layout->addWidget(groupBox);

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

    layout->addWidget(imageGroupBox);
    layout->addStretch();
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
    m_controller->setBrightnessAuto(checked);
    m_brightnessSlider->setEnabled(!checked);
    // When switching to manual, send current slider value
    if (!checked) {
        m_controller->setBrightness(m_brightnessSlider->value());
    }
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
    m_controller->setContrastAuto(checked);
    m_contrastSlider->setEnabled(!checked);
    // When switching to manual, send current slider value
    if (!checked) {
        m_controller->setContrast(m_contrastSlider->value());
    }
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
    m_controller->setSaturationAuto(checked);
    m_saturationSlider->setEnabled(!checked);
    // When switching to manual, send current slider value
    if (!checked) {
        m_controller->setSaturation(m_saturationSlider->value());
    }
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

        // Image controls - update auto checkboxes
        if (m_brightnessAutoCheckBox->isChecked() != state.brightnessAuto) {
            m_brightnessAutoCheckBox->blockSignals(true);
            m_brightnessAutoCheckBox->setChecked(state.brightnessAuto);
            m_brightnessAutoCheckBox->blockSignals(false);
            m_brightnessSlider->setEnabled(!state.brightnessAuto);
        }

        if (m_contrastAutoCheckBox->isChecked() != state.contrastAuto) {
            m_contrastAutoCheckBox->blockSignals(true);
            m_contrastAutoCheckBox->setChecked(state.contrastAuto);
            m_contrastAutoCheckBox->blockSignals(false);
            m_contrastSlider->setEnabled(!state.contrastAuto);
        }

        if (m_saturationAutoCheckBox->isChecked() != state.saturationAuto) {
            m_saturationAutoCheckBox->blockSignals(true);
            m_saturationAutoCheckBox->setChecked(state.saturationAuto);
            m_saturationAutoCheckBox->blockSignals(false);
            m_saturationSlider->setEnabled(!state.saturationAuto);
        }
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

void CameraSettingsWidget::updateWhiteBalanceKelvinLabel(int value)
{
    if (m_whiteBalanceKelvinLabel) {
        m_whiteBalanceKelvinLabel->setText(QString::number(value) + QStringLiteral(" K"));
    }
}
