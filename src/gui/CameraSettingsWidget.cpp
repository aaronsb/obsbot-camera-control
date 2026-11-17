#include "CameraSettingsWidget.h"
#include <QStandardItemModel>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
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
    connect(m_commandTimer, &QTimer::timeout, this, [this]() {
        if (m_controller->isConnected())
            m_controller->saveConfig();
    });
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 14);
    layout->setSpacing(14);

    m_advancedGroupBox = new QGroupBox("HDR", this);
    QVBoxLayout *hdrLayout = new QVBoxLayout(m_advancedGroupBox);
    hdrLayout->setContentsMargins(16, 16, 16, 16);

    // HDR
    m_hdrCheckBox = new QCheckBox("HDR (High Dynamic Range)", this);
    m_hdrCheckBox->setToolTip("Enhance image quality in high-contrast scenes");
    connect(m_hdrCheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onHDRToggled);
    hdrLayout->addWidget(m_hdrCheckBox);
    layout->addWidget(m_advancedGroupBox);

    m_exposureGroupBox = new QGroupBox("Exposure", this);
    QVBoxLayout *groupLayout = new QVBoxLayout(m_exposureGroupBox);
    groupLayout->setContentsMargins(16, 16, 16, 16);
    groupLayout->setSpacing(10);
    constexpr int controlLabelWidth = 90;

    auto alignedLabel = [this](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setMinimumWidth(controlLabelWidth);
        return label;
    };

    // Retained as an internal state holder for configuration and image
    // presets; the visible controls live beside the Zoom slider.
    m_fovComboBox = new QComboBox(this);
    m_fovComboBox->addItem("Wide (86°)", 0);
    m_fovComboBox->addItem("Medium (78°)", 1);
    m_fovComboBox->addItem("Narrow (65°)", 2);
    m_fovComboBox->addItem("Custom", Device::FovTypeNull);
    if (auto *model = qobject_cast<QStandardItemModel*>(m_fovComboBox->model())) {
        model->item(3)->setEnabled(false);
    }
    connect(m_fovComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraSettingsWidget::onFOVChanged);
    m_fovComboBox->hide();

    // Face AE
    m_faceAECheckBox = new QCheckBox("Face-based Auto Exposure", this);
    m_faceAECheckBox->setToolTip("Optimize exposure for faces");
    connect(m_faceAECheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onFaceAEToggled);
    groupLayout->addWidget(m_faceAECheckBox);

    QHBoxLayout *exposureLayout = new QHBoxLayout();
    m_exposureAutoCheckBox = new QCheckBox("Auto Exposure", this);
    m_exposureAutoCheckBox->setToolTip(
        "<b>Shutter Priority</b><br>"
        "Keeps exposure time camera-managed and relatively short. "
        "It does not automatically increase image brightness or gain.");
    connect(m_exposureAutoCheckBox, &QCheckBox::toggled,
            this, &CameraSettingsWidget::onExposureAutoToggled);
    exposureLayout->addWidget(m_exposureAutoCheckBox);
    m_exposureComboBox = new QComboBox(this);
    static const char *shutterNames[] = {
        "1/8000", "1/6400", "1/5000", "1/4000", "1/3200", "1/2500",
        "1/2000", "1/1600", "1/1250", "1/1000", "1/800", "1/640",
        "1/500", "1/400", "1/320", "1/240", "1/200", "1/160",
        "1/120", "1/100", "1/80", "1/60", "1/50", "1/40", "1/30",
        "1/25", "1/20", "1/15", "1/12.5", "1/10", "1/8", "1/6.25",
        "1/5", "1/4"
    };
    for (int i = 0; i < 34; ++i)
        m_exposureComboBox->addItem(shutterNames[i], 9 + i);
    connect(m_exposureComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraSettingsWidget::onExposureChanged);
    m_exposureLabel = new QLabel("Shutter:", this);
    exposureLayout->addWidget(m_exposureLabel);
    exposureLayout->addWidget(m_exposureComboBox);
    exposureLayout->addStretch();
    groupLayout->addLayout(exposureLayout);

    QHBoxLayout *antiFlickerLayout = new QHBoxLayout();
    antiFlickerLayout->addWidget(new QLabel("Anti-flicker:", this));
    m_antiFlickerComboBox = new QComboBox(this);
    m_antiFlickerComboBox->addItem("Off", Device::PowerLineFreqOff);
    m_antiFlickerComboBox->addItem("50 Hz", Device::PowerLineFreq50);
    m_antiFlickerComboBox->addItem("60 Hz", Device::PowerLineFreq60);
    m_antiFlickerComboBox->addItem("Auto", Device::PowerLineFreqAuto);
    connect(m_antiFlickerComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraSettingsWidget::onAntiFlickerChanged);
    antiFlickerLayout->addWidget(m_antiFlickerComboBox);
    antiFlickerLayout->addStretch();
    layout->addWidget(m_exposureGroupBox);

    m_tiny4kDeviceGroup = new QWidget(this);
    QVBoxLayout *deviceLayout = new QVBoxLayout(m_tiny4kDeviceGroup);
    deviceLayout->setContentsMargins(0, 0, 0, 0);
    deviceLayout->setSpacing(14);

    auto addUvcSlider = [this, groupLayout, alignedLabel](const QString &label,
                                             QWidget *&rowWidget,
                                             QSlider *&slider,
                                             auto setter) {
        rowWidget = new QWidget(this);
        QHBoxLayout *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(alignedLabel(label));
        slider = new QSlider(Qt::Horizontal, this);
        row->addWidget(slider, 1);
        QPushButton *reset = new QPushButton("Reset", this);
        row->addWidget(reset);
        connect(slider, &QSlider::valueChanged, this, [this, setter](int value) {
            (m_controller->*setter)(value);
            m_commandTimer->start(1000);
        });
        groupLayout->addWidget(rowWidget);
        return reset;
    };
    QPushButton *exposureReset = addUvcSlider(
        "Exposure:", m_uvcExposureRow, m_uvcExposureSlider,
        &CameraController::setTiny4kExposure);
    connect(exposureReset, &QPushButton::clicked, this, [this]() {
        auto range = m_controller->getTiny4kExposureRange();
        if (range.valid) m_uvcExposureSlider->setValue(range.defaultValue);
    });
    QPushButton *gainReset = addUvcSlider(
        "Gain:", m_gainRow, m_gainSlider, &CameraController::setTiny4kGain);
    connect(gainReset, &QPushButton::clicked, this, [this]() {
        auto range = m_controller->getGainRange();
        if (range.valid) m_gainSlider->setValue(range.defaultValue);
    });
    QPushButton *backlightReset = addUvcSlider(
        "Backlight:", m_backlightRow, m_backlightSlider,
        &CameraController::setTiny4kBacklightCompensation);
    connect(backlightReset, &QPushButton::clicked, this, [this]() {
        auto range = m_controller->getBacklightCompensationRange();
        if (range.valid) m_backlightSlider->setValue(range.defaultValue);
    });
    groupLayout->addLayout(antiFlickerLayout);

    QGroupBox *sleepGroup = new QGroupBox("Sleep", m_tiny4kDeviceGroup);
    QVBoxLayout *sleepLayout = new QVBoxLayout(sleepGroup);
    sleepLayout->setContentsMargins(16, 16, 16, 16);
    sleepLayout->setSpacing(10);
    QHBoxLayout *sleepTimeLayout = new QHBoxLayout();
    sleepTimeLayout->addWidget(new QLabel("Sleep time:", sleepGroup));
    QSpinBox *sleepTimeout = new QSpinBox(sleepGroup);
    sleepTimeout->setRange(0, 65535);
    sleepTimeout->setSuffix(" s");
    sleepTimeout->setSpecialValueText("Disabled");
    sleepTimeLayout->addWidget(sleepTimeout, 1);
    sleepLayout->addLayout(sleepTimeLayout);

    QTimer *sleepApplyTimer = new QTimer(this);
    sleepApplyTimer->setSingleShot(true);
    sleepApplyTimer->setInterval(400);
    connect(sleepTimeout, QOverload<int>::of(&QSpinBox::valueChanged),
            sleepApplyTimer, QOverload<>::of(&QTimer::start));
    connect(sleepApplyTimer, &QTimer::timeout, this, [this, sleepTimeout]() {
        m_controller->setSleepTimeout(sleepTimeout->value());
    });

    QHBoxLayout *powerLayout = new QHBoxLayout();
    QPushButton *wakeButton = new QPushButton("Wake", sleepGroup);
    connect(wakeButton, &QPushButton::clicked, this, [this]() {
        m_controller->setDeviceAwake(true);
    });
    powerLayout->addWidget(wakeButton);
    QPushButton *sleepButton = new QPushButton("Sleep", sleepGroup);
    connect(sleepButton, &QPushButton::clicked, this, [this]() {
        m_controller->setDeviceAwake(false);
    });
    powerLayout->addWidget(sleepButton);
    sleepLayout->addLayout(powerLayout);
    deviceLayout->addWidget(sleepGroup);

    QGroupBox *gesturesGroup =
        new QGroupBox("Gestures", m_tiny4kDeviceGroup);
    QVBoxLayout *gesturesLayout = new QVBoxLayout(gesturesGroup);
    gesturesLayout->setContentsMargins(16, 16, 16, 16);
    QCheckBox *gestureTracking = new QCheckBox("Tracking", gesturesGroup);
    connect(gestureTracking, &QCheckBox::toggled, this, [this](bool enabled) {
        m_controller->setGestureControl(0, enabled);
    });
    gesturesLayout->addWidget(gestureTracking);
    QCheckBox *gestureZoom = new QCheckBox("Zoom", gesturesGroup);
    connect(gestureZoom, &QCheckBox::toggled, this, [this](bool enabled) {
        m_controller->setGestureControl(1, enabled);
    });
    gesturesLayout->addWidget(gestureZoom);
    deviceLayout->addWidget(gesturesGroup);

    QHBoxLayout *screenModeLayout = new QHBoxLayout();
    screenModeLayout->addWidget(new QLabel("Screen mode:", m_tiny4kDeviceGroup));
    QComboBox *screenMode = new QComboBox(m_tiny4kDeviceGroup);
    screenMode->addItem("Landscape", false);
    screenMode->addItem("Portrait", true);
    connect(screenMode, QOverload<int>::of(&QComboBox::activated),
            this, [this, screenMode](int index) {
        const bool portrait = screenMode->itemData(index).toBool();
        if (QMessageBox::warning(this, "Restart Camera",
                "Changing screen mode restarts the camera. Continue?",
                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            m_controller->setVerticalMode(portrait);
        }
    });
    screenModeLayout->addWidget(screenMode, 1);
    deviceLayout->addLayout(screenModeLayout);

    QCheckBox *sleepMicrophone =
        new QCheckBox("Microphone available during sleep", sleepGroup);
    connect(sleepMicrophone, &QCheckBox::toggled, this, [this](bool enabled) {
        m_controller->setMicrophoneDuringSleep(enabled);
    });
    sleepLayout->addWidget(sleepMicrophone);

    QPushButton *factoryReset =
        new QPushButton("Factory Reset…", m_tiny4kDeviceGroup);
    connect(factoryReset, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::critical(this, "Factory Reset",
                "Reset every camera setting to its factory default? This cannot be undone.",
                QMessageBox::Reset | QMessageBox::Cancel,
                QMessageBox::Cancel) == QMessageBox::Reset)
            m_controller->restoreFactorySettings();
    });
    deviceLayout->addWidget(factoryReset);
    // Remaining device-specific actions are exposed on the More tab.

    // Image Controls Group
    m_imageGroupBox = new QGroupBox("Image", this);
    QVBoxLayout *imageLayout = new QVBoxLayout(m_imageGroupBox);
    imageLayout->setContentsMargins(16, 16, 16, 16);
    imageLayout->setSpacing(10);

    // Brightness
    QHBoxLayout *brightnessLayout = new QHBoxLayout();
    m_brightnessAutoCheckBox = new QCheckBox("Auto", this);
    m_brightnessAutoCheckBox->setVisible(false);
    m_brightnessAutoCheckBox->setToolTip("Enable automatic brightness (disables manual control)");
    connect(m_brightnessAutoCheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onBrightnessAutoToggled);
    brightnessLayout->addWidget(m_brightnessAutoCheckBox);
    brightnessLayout->addWidget(alignedLabel("Brightness:"));
    m_brightnessSlider = new QSlider(Qt::Horizontal, this);
    m_brightnessSlider->setRange(0, 255);
    m_brightnessSlider->setToolTip("Adjust image brightness (0-255)");
    connect(m_brightnessSlider, &QSlider::valueChanged, this, &CameraSettingsWidget::onBrightnessChanged);
    brightnessLayout->addWidget(m_brightnessSlider, 1);
    QPushButton *brightnessReset = new QPushButton("Reset", this);
    connect(brightnessReset, &QPushButton::clicked, this, [this]() {
        const auto range = m_controller->getBrightnessRange();
        const int value = range.valid ? range.defaultValue : 50;
        m_controller->setBrightnessAuto(false);
        setBrightness(value);
        m_controller->setBrightness(value);
    });
    brightnessLayout->addWidget(brightnessReset);
    imageLayout->addLayout(brightnessLayout);

    // Contrast
    QHBoxLayout *contrastLayout = new QHBoxLayout();
    m_contrastAutoCheckBox = new QCheckBox("Auto", this);
    m_contrastAutoCheckBox->setVisible(false);
    m_contrastAutoCheckBox->setToolTip("Enable automatic contrast (disables manual control)");
    connect(m_contrastAutoCheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onContrastAutoToggled);
    contrastLayout->addWidget(m_contrastAutoCheckBox);
    contrastLayout->addWidget(alignedLabel("Contrast:"));
    m_contrastSlider = new QSlider(Qt::Horizontal, this);
    m_contrastSlider->setRange(0, 255);
    m_contrastSlider->setToolTip("Adjust image contrast (0-255)");
    connect(m_contrastSlider, &QSlider::valueChanged, this, &CameraSettingsWidget::onContrastChanged);
    contrastLayout->addWidget(m_contrastSlider, 1);
    QPushButton *contrastReset = new QPushButton("Reset", this);
    connect(contrastReset, &QPushButton::clicked, this, [this]() {
        const auto range = m_controller->getContrastRange();
        const int value = range.valid ? range.defaultValue : 50;
        m_controller->setContrastAuto(false);
        setContrast(value);
        m_controller->setContrast(value);
    });
    contrastLayout->addWidget(contrastReset);
    imageLayout->addLayout(contrastLayout);

    // Saturation
    QHBoxLayout *saturationLayout = new QHBoxLayout();
    m_saturationAutoCheckBox = new QCheckBox("Auto", this);
    m_saturationAutoCheckBox->setVisible(false);
    m_saturationAutoCheckBox->setToolTip("Enable automatic saturation (disables manual control)");
    connect(m_saturationAutoCheckBox, &QCheckBox::toggled, this, &CameraSettingsWidget::onSaturationAutoToggled);
    saturationLayout->addWidget(m_saturationAutoCheckBox);
    saturationLayout->addWidget(alignedLabel("Saturation:"));
    m_saturationSlider = new QSlider(Qt::Horizontal, this);
    m_saturationSlider->setRange(0, 255);
    m_saturationSlider->setToolTip("Adjust color saturation (0-255)");
    connect(m_saturationSlider, &QSlider::valueChanged, this, &CameraSettingsWidget::onSaturationChanged);
    saturationLayout->addWidget(m_saturationSlider, 1);
    QPushButton *saturationReset = new QPushButton("Reset", this);
    connect(saturationReset, &QPushButton::clicked, this, [this]() {
        const auto range = m_controller->getSaturationRange();
        const int value = range.valid ? range.defaultValue : 50;
        m_controller->setSaturationAuto(false);
        setSaturation(value);
        m_controller->setSaturation(value);
    });
    saturationLayout->addWidget(saturationReset);
    imageLayout->addLayout(saturationLayout);

    QHBoxLayout *hueLayout = new QHBoxLayout();
    hueLayout->addWidget(alignedLabel("Hue:"));
    m_hueSlider = new QSlider(Qt::Horizontal, this);
    m_hueSlider->setRange(0, 100);
    connect(m_hueSlider, &QSlider::valueChanged, this, &CameraSettingsWidget::onHueChanged);
    hueLayout->addWidget(m_hueSlider, 1);
    QPushButton *hueReset = new QPushButton("Reset", this);
    connect(hueReset, &QPushButton::clicked, this, [this]() {
        const auto range = m_controller->getHueRange();
        const int value = range.valid ? range.defaultValue : 50;
        m_hueSlider->blockSignals(true);
        m_hueSlider->setValue(value);
        m_hueSlider->blockSignals(false);
        m_controller->setHue(value);
    });
    hueLayout->addWidget(hueReset);

    QHBoxLayout *sharpnessLayout = new QHBoxLayout();
    sharpnessLayout->addWidget(alignedLabel("Sharpness:"));
    m_sharpnessSlider = new QSlider(Qt::Horizontal, this);
    m_sharpnessSlider->setRange(0, 100);
    connect(m_sharpnessSlider, &QSlider::valueChanged,
            this, &CameraSettingsWidget::onSharpnessChanged);
    sharpnessLayout->addWidget(m_sharpnessSlider, 1);
    QPushButton *sharpnessReset = new QPushButton("Reset", this);
    connect(sharpnessReset, &QPushButton::clicked, this, [this]() {
        const auto range = m_controller->getSharpnessRange();
        const int value = range.valid ? range.defaultValue : 50;
        m_sharpnessSlider->blockSignals(true);
        m_sharpnessSlider->setValue(value);
        m_sharpnessSlider->blockSignals(false);
        m_controller->setSharpness(value);
    });
    sharpnessLayout->addWidget(sharpnessReset);
    imageLayout->addLayout(sharpnessLayout);
    imageLayout->addLayout(hueLayout);

    // White Balance
    m_whiteBalanceGroupBox = new QGroupBox("White Balance", this);
    QVBoxLayout *whiteBalanceLayout =
        new QVBoxLayout(m_whiteBalanceGroupBox);
    whiteBalanceLayout->setContentsMargins(16, 16, 16, 16);
    whiteBalanceLayout->setSpacing(10);
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
    QPushButton *whiteBalanceReset = new QPushButton("Reset", this);
    connect(whiteBalanceReset, &QPushButton::clicked, this, [this]() {
        const int mode = static_cast<int>(Device::DevWhiteBalanceAuto);
        setWhiteBalance(mode);
        const auto range = m_controller->getWhiteBalanceKelvinRange();
        if (range.valid)
            setWhiteBalanceKelvin(range.defaultValue);
        m_controller->setWhiteBalance(mode);
    });
    wbLayout->addWidget(whiteBalanceReset);
    wbLayout->addStretch();
    whiteBalanceLayout->addLayout(wbLayout);

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
    whiteBalanceLayout->addLayout(wbKelvinLayout);

    layout->addWidget(m_whiteBalanceGroupBox);
    layout->addWidget(m_imageGroupBox);
    layout->addStretch();
}

void CameraSettingsWidget::insertWidgetAt(int index, QWidget *widget)
{
    if (!widget) return;
    if (QWidget *oldParent = widget->parentWidget()) {
        if (QLayout *oldLayout = oldParent->layout())
            oldLayout->removeWidget(widget);
    }
    if (auto *pageLayout = qobject_cast<QVBoxLayout*>(layout()))
        pageLayout->insertWidget(index, widget);
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

void CameraSettingsWidget::onExposureAutoToggled(bool checked)
{
    m_userInitiated = true;
    m_exposureComboBox->setEnabled(!checked);
    m_uvcExposureSlider->setEnabled(!checked);
    if (m_controller->hasTiny4kCapabilities())
        m_controller->setTiny4kAutoExposure(checked);
    else
        m_controller->setExposure(
            m_exposureComboBox->currentData().toInt(), checked);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onExposureChanged(int index)
{
    if (index < 0 || m_exposureAutoCheckBox->isChecked()) return;
    m_userInitiated = true;
    m_controller->setExposure(m_exposureComboBox->itemData(index).toInt(), false);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onAntiFlickerChanged(int index)
{
    if (index < 0) return;
    m_userInitiated = true;
    m_controller->setAntiFlicker(m_antiFlickerComboBox->itemData(index).toInt());
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

void CameraSettingsWidget::onHueChanged(int value)
{
    m_userInitiated = true;
    m_controller->setHue(value);
    m_commandTimer->start(1000);
}

void CameraSettingsWidget::onSharpnessChanged(int value)
{
    m_userInitiated = true;
    m_controller->setSharpness(value);
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

    const bool tiny4k = m_controller->hasTiny4kCapabilities();
    m_tiny4kDeviceGroup->setVisible(tiny4k);
    m_uvcExposureRow->setVisible(tiny4k);
    m_gainRow->setVisible(tiny4k);
    m_backlightRow->setVisible(tiny4k);
    m_exposureAutoCheckBox->setVisible(true);
    m_exposureLabel->setVisible(!tiny4k);
    m_exposureComboBox->setVisible(!tiny4k);

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

        auto syncSlider = [](QSlider *slider, int value) {
            if (slider->value() == value) return;
            slider->blockSignals(true);
            slider->setValue(value);
            slider->blockSignals(false);
        };
        syncSlider(m_uvcExposureSlider, state.uvcExposure);
        syncSlider(m_gainSlider, state.gain);
        syncSlider(m_backlightSlider, state.backlightCompensation);

        m_exposureAutoCheckBox->blockSignals(true);
        m_exposureAutoCheckBox->setChecked(state.exposureAuto);
        m_exposureAutoCheckBox->blockSignals(false);
        m_exposureComboBox->setEnabled(!state.exposureAuto);
        m_uvcExposureSlider->setEnabled(!state.exposureAuto);
        int exposureIndex = m_exposureComboBox->findData(state.exposure);
        if (exposureIndex >= 0) {
            m_exposureComboBox->blockSignals(true);
            m_exposureComboBox->setCurrentIndex(exposureIndex);
            m_exposureComboBox->blockSignals(false);
        }
        int flickerIndex = m_antiFlickerComboBox->findData(state.antiFlicker);
        if (flickerIndex >= 0) {
            m_antiFlickerComboBox->blockSignals(true);
            m_antiFlickerComboBox->setCurrentIndex(flickerIndex);
            m_antiFlickerComboBox->blockSignals(false);
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
        if (m_hueSlider->value() != state.hue) {
            m_hueSlider->blockSignals(true);
            m_hueSlider->setValue(state.hue);
            m_hueSlider->blockSignals(false);
        }
        if (m_sharpnessSlider->value() != state.sharpness) {
            m_sharpnessSlider->blockSignals(true);
            m_sharpnessSlider->setValue(state.sharpness);
            m_sharpnessSlider->blockSignals(false);
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
    applyRange(m_controller->getHueRange(), m_hueSlider, m_hueRangeApplied,
               QStringLiteral("Adjust image hue (%1-%2)"));
    applyRange(m_controller->getSharpnessRange(), m_sharpnessSlider, m_sharpnessRangeApplied,
               QStringLiteral("Adjust image sharpness (%1-%2)"));
    applyRange(m_controller->getTiny4kExposureRange(), m_uvcExposureSlider,
               m_uvcExposureRangeApplied, QStringLiteral("UVC exposure (%1-%2)"));
    applyRange(m_controller->getGainRange(), m_gainSlider,
               m_gainRangeApplied, QStringLiteral("UVC gain (%1-%2)"));
    applyRange(m_controller->getBacklightCompensationRange(), m_backlightSlider,
               m_backlightRangeApplied, QStringLiteral("Backlight compensation (%1-%2)"));

    const auto antiFlickerRange = m_controller->getAntiFlickerRange();
    if (antiFlickerRange.valid) {
        auto *model = qobject_cast<QStandardItemModel *>(m_antiFlickerComboBox->model());
        for (int i = 0; model && i < m_antiFlickerComboBox->count(); ++i) {
            const int value = m_antiFlickerComboBox->itemData(i).toInt();
            if (QStandardItem *item = model->item(i)) {
                item->setEnabled(value >= antiFlickerRange.min &&
                                 value <= antiFlickerRange.max);
            }
        }
        m_antiFlickerComboBox->setToolTip(
            QStringLiteral("Supported anti-flicker modes: %1-%2")
                .arg(antiFlickerRange.min)
                .arg(antiFlickerRange.max));
    }

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
    m_exposureGroupBox->setVisible(!v4l2Only);
    m_tiny4kDeviceGroup->setVisible(!v4l2Only && m_controller->hasTiny4kCapabilities());
}

void CameraSettingsWidget::updateWhiteBalanceKelvinLabel(int value)
{
    if (m_whiteBalanceKelvinLabel) {
        m_whiteBalanceKelvinLabel->setText(QString::number(value) + QStringLiteral(" K"));
    }
}
