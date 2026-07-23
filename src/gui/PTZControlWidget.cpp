#include "PTZControlWidget.h"
#include "CameraSettingsWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>

PTZControlWidget::PTZControlWidget(CameraController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_settingsWidget(nullptr)
    , m_positionPresetGroup(nullptr)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 14);
    layout->setSpacing(14);

    // Presets section
    m_positionPresetGroup = new QGroupBox("Presets", this);
    QHBoxLayout *presetLayout = new QHBoxLayout(m_positionPresetGroup);
    presetLayout->setContentsMargins(16, 16, 16, 16);
    presetLayout->setSpacing(8);

    for (int i = 0; i < 3; ++i) {
        PresetUi presetUi{};
        presetUi.defined = false;
        presetUi.name = QString::number(i + 1);
        presetUi.pan = 0.0;
        presetUi.tilt = 0.0;
        presetUi.zoom = 1.0;

        presetUi.stack = new QStackedWidget(m_positionPresetGroup);
        presetUi.recallButton = new QPushButton(presetUi.name, presetUi.stack);
        presetUi.recallButton->setProperty("presetIndex", i);
        presetUi.recallButton->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(presetUi.recallButton, &QPushButton::clicked, this, &PTZControlWidget::onRecallPreset);
        connect(presetUi.recallButton, &QPushButton::customContextMenuRequested,
                this, &PTZControlWidget::showPresetMenu);
        presetUi.renameEditor = new QLineEdit(presetUi.stack);
        presetUi.renameEditor->setAlignment(Qt::AlignCenter);
        presetUi.renameEditor->setMaxLength(32);
        presetUi.renameEditor->setProperty("presetIndex", i);
        presetUi.stack->addWidget(presetUi.recallButton);
        presetUi.stack->addWidget(presetUi.renameEditor);
        presetLayout->addWidget(presetUi.stack, 1);
        m_presets[static_cast<size_t>(i)] = presetUi;
        connect(presetUi.renameEditor, &QLineEdit::editingFinished,
                this, [this, i]() {
            auto &preset = m_presets[static_cast<size_t>(i)];
            if (preset.stack->currentWidget() != preset.renameEditor) {
                return;
            }
            QString name = preset.renameEditor->text().trimmed();
            if (!name.isEmpty()) {
                name.replace('\n', ' ');
                name.replace('\r', ' ');
                preset.name = name;
            }
            preset.stack->setCurrentWidget(preset.recallButton);
            updatePresetLabel(i);
            emit presetUpdated(i, preset.pan, preset.tilt, preset.zoom,
                               preset.defined, preset.name);
        });
        updatePresetLabel(i);
    }

    layout->addWidget(m_positionPresetGroup);

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
        ui.name = preset.name.trimmed().isEmpty()
            ? QString::number(i + 1) : preset.name;
        ui.pan = preset.pan;
        ui.tilt = preset.tilt;
        ui.zoom = preset.zoom;
        updatePresetLabel(i);
    }
}

std::array<PTZControlWidget::PresetState, 3> PTZControlWidget::currentPresets() const
{
    std::array<PresetState, 3> out{};
    for (int i = 0; i < 3; ++i) {
        const auto &ui = m_presets[static_cast<size_t>(i)];
        out[static_cast<size_t>(i)] = {
            ui.defined, ui.name, ui.pan, ui.tilt, ui.zoom
        };
    }
    return out;
}

void PTZControlWidget::onRecallPreset()
{
    auto *button = qobject_cast<QPushButton*>(sender());
    if (!button) {
        return;
    }
    int index = button->property("presetIndex").toInt();
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        return;
    }

    auto &preset = m_presets[static_cast<size_t>(index)];
    if (!preset.defined) {
        return;
    }

    if (m_controller->hasTiny4kCapabilities()
            && m_controller->recallHardwarePreset(index)) {
        return;
    }
    m_controller->setPanTilt(preset.pan, preset.tilt);
    m_controller->setZoom(preset.zoom);
}

void PTZControlWidget::storePreset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        return;
    }

    auto state = m_controller->getCurrentState();
    auto &preset = m_presets[static_cast<size_t>(index)];
    preset.defined = true;
    preset.pan = state.pan;
    preset.tilt = state.tilt;
    preset.zoom = state.zoom;
    updatePresetLabel(index);

    if (m_controller->hasTiny4kCapabilities()) {
        m_controller->saveHardwarePreset(index);
    }
    emit presetUpdated(index, preset.pan, preset.tilt, preset.zoom,
                       true, preset.name);
}

void PTZControlWidget::deletePreset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        return;
    }
    auto &preset = m_presets[static_cast<size_t>(index)];
    preset.defined = false;
    preset.name = QString::number(index + 1);
    updatePresetLabel(index);
    emit presetUpdated(index, preset.pan, preset.tilt, preset.zoom,
                       false, preset.name);
}

void PTZControlWidget::beginPresetRename(int index)
{
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        return;
    }
    auto &preset = m_presets[static_cast<size_t>(index)];
    preset.renameEditor->setText(preset.name);
    preset.stack->setCurrentWidget(preset.renameEditor);
    preset.renameEditor->setFocus();
    preset.renameEditor->selectAll();
}

void PTZControlWidget::showPresetMenu(const QPoint &position)
{
    auto *button = qobject_cast<QPushButton*>(sender());
    if (!button) {
        return;
    }
    const int index = button->property("presetIndex").toInt();
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        return;
    }

    const auto &preset = m_presets[static_cast<size_t>(index)];
    QMenu menu(this);
    if (!preset.defined) {
        QAction *setAction = menu.addAction("Set");
        if (menu.exec(button->mapToGlobal(position)) == setAction) {
            storePreset(index);
        }
        return;
    }

    QAction *updateAction = menu.addAction("Update");
    QAction *deleteAction = menu.addAction("Delete");
    QAction *renameAction = menu.addAction("Rename");
    QAction *selected = menu.exec(button->mapToGlobal(position));
    if (selected == updateAction) {
        storePreset(index);
    } else if (selected == deleteAction) {
        deletePreset(index);
    } else if (selected == renameAction) {
        beginPresetRename(index);
    }
}

void PTZControlWidget::updatePresetLabel(int index)
{
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        return;
    }
    auto &preset = m_presets[static_cast<size_t>(index)];
    if (!preset.recallButton) {
        return;
    }

    preset.recallButton->setText(preset.name);
    preset.recallButton->setStyleSheet(
        preset.defined
            ? QString()
            : QString("color: palette(mid); background-color: palette(midlight);"));
    preset.recallButton->setToolTip(preset.defined
        ? QString("Pan %1, Tilt %2, Zoom %3x")
              .arg(preset.pan, 0, 'f', 2)
              .arg(preset.tilt, 0, 'f', 2)
              .arg(preset.zoom, 0, 'f', 2)
        : QString("Empty preset"));
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
    preset.state.faceFocus = m_controller->getCurrentState().faceFocusEnabled;
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
    m_controller->setFaceFocus(preset.state.faceFocus);
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
