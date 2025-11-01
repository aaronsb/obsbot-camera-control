#ifndef CAMERASETTINGSWIDGET_H
#define CAMERASETTINGSWIDGET_H

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <algorithm>
#include "CameraController.h"

/**
 * @brief Advanced camera settings (HDR, FOV, Focus, etc.)
 * Shown in: Expert mode only
 */
class CameraSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CameraSettingsWidget(CameraController *controller, QWidget *parent = nullptr);

    void updateFromState(const CameraController::CameraState &state);

    // Getters for current UI state
    bool isHDREnabled() const { return m_hdrCheckBox->isChecked(); }
    int getFOVMode() const { return m_fovComboBox->currentIndex(); }
    bool isFaceAEEnabled() const { return m_faceAECheckBox->isChecked(); }
    bool isFaceFocusEnabled() const { return m_faceFocusCheckBox->isChecked(); }
    bool isBrightnessAuto() const { return m_brightnessAutoCheckBox->isChecked(); }
    int getBrightness() const { return m_brightnessSlider->value(); }
    bool isContrastAuto() const { return m_contrastAutoCheckBox->isChecked(); }
    int getContrast() const { return m_contrastSlider->value(); }
    bool isSaturationAuto() const { return m_saturationAutoCheckBox->isChecked(); }
    int getSaturation() const { return m_saturationSlider->value(); }
    int getWhiteBalance() const { return m_whiteBalanceComboBox->currentData().toInt(); }
    int getWhiteBalanceKelvin() const { return m_whiteBalanceKelvinSlider->value(); }

    // Setters for initializing from config
    void setHDREnabled(bool enabled) {
        m_hdrCheckBox->blockSignals(true);
        m_hdrCheckBox->setChecked(enabled);
        m_hdrCheckBox->blockSignals(false);
    }
    void setFOVMode(int mode) {
        m_fovComboBox->blockSignals(true);
        m_fovComboBox->setCurrentIndex(mode);
        m_fovComboBox->blockSignals(false);
    }
    void setFaceAEEnabled(bool enabled) {
        m_faceAECheckBox->blockSignals(true);
        m_faceAECheckBox->setChecked(enabled);
        m_faceAECheckBox->blockSignals(false);
    }
    void setFaceFocusEnabled(bool enabled) {
        m_faceFocusCheckBox->blockSignals(true);
        m_faceFocusCheckBox->setChecked(enabled);
        m_faceFocusCheckBox->blockSignals(false);
    }
    void setBrightnessAuto(bool enabled) {
        m_brightnessAutoCheckBox->blockSignals(true);
        m_brightnessAutoCheckBox->setChecked(enabled);
        m_brightnessAutoCheckBox->blockSignals(false);
        m_brightnessSlider->setEnabled(!enabled);
    }
    void setBrightness(int value) {
        m_brightnessSlider->blockSignals(true);
        m_brightnessSlider->setValue(value);
        m_brightnessSlider->blockSignals(false);
    }
    void setContrastAuto(bool enabled) {
        m_contrastAutoCheckBox->blockSignals(true);
        m_contrastAutoCheckBox->setChecked(enabled);
        m_contrastAutoCheckBox->blockSignals(false);
        m_contrastSlider->setEnabled(!enabled);
    }
    void setContrast(int value) {
        m_contrastSlider->blockSignals(true);
        m_contrastSlider->setValue(value);
        m_contrastSlider->blockSignals(false);
    }
    void setSaturationAuto(bool enabled) {
        m_saturationAutoCheckBox->blockSignals(true);
        m_saturationAutoCheckBox->setChecked(enabled);
        m_saturationAutoCheckBox->blockSignals(false);
        m_saturationSlider->setEnabled(!enabled);
    }
    void setSaturation(int value) {
        m_saturationSlider->blockSignals(true);
        m_saturationSlider->setValue(value);
        m_saturationSlider->blockSignals(false);
    }
    void setWhiteBalance(int value) {
        m_whiteBalanceComboBox->blockSignals(true);
        int index = m_whiteBalanceComboBox->findData(value);
        if (index >= 0) {
            m_whiteBalanceComboBox->setCurrentIndex(index);
        }
        m_whiteBalanceComboBox->blockSignals(false);
        applyControlRanges();
        updateWhiteBalanceControls(value);
    }
    void setWhiteBalanceKelvin(int kelvin) {
        m_whiteBalanceKelvinSlider->blockSignals(true);
        int clamped = std::clamp(kelvin, m_whiteBalanceKelvinSlider->minimum(), m_whiteBalanceKelvinSlider->maximum());
        m_whiteBalanceKelvinSlider->setValue(clamped);
        m_whiteBalanceKelvinSlider->blockSignals(false);
        updateWhiteBalanceKelvinLabel(clamped);
    }

private slots:
    void onHDRToggled(bool checked);
    void onFOVChanged(int index);
    void onFaceAEToggled(bool checked);
    void onFaceFocusToggled(bool checked);
    void onBrightnessAutoToggled(bool checked);
    void onBrightnessChanged(int value);
    void onContrastAutoToggled(bool checked);
    void onContrastChanged(int value);
    void onSaturationAutoToggled(bool checked);
    void onSaturationChanged(int value);
    void onWhiteBalanceChanged(int index);
    void onWhiteBalanceKelvinChanged(int value);

private:
    CameraController *m_controller;

    QCheckBox *m_hdrCheckBox;
    QComboBox *m_fovComboBox;
    QCheckBox *m_faceAECheckBox;
    QCheckBox *m_faceFocusCheckBox;

    // Image controls
    QCheckBox *m_brightnessAutoCheckBox;
    QSlider *m_brightnessSlider;
    QCheckBox *m_contrastAutoCheckBox;
    QSlider *m_contrastSlider;
    QCheckBox *m_saturationAutoCheckBox;
    QSlider *m_saturationSlider;
    QComboBox *m_whiteBalanceComboBox;
    QSlider *m_whiteBalanceKelvinSlider;
    QLabel *m_whiteBalanceKelvinLabel;

    bool m_userInitiated;  // Track if change was user-initiated
    QTimer *m_commandTimer;  // Debounce timer for command completion
    bool m_brightnessRangeApplied = false;
    bool m_contrastRangeApplied = false;
    bool m_saturationRangeApplied = false;
    bool m_whiteBalanceRangeApplied = false;

    void applyControlRanges();
    void updateWhiteBalanceControls(int mode);
    void updateWhiteBalanceKelvinLabel(int value);
};

#endif // CAMERASETTINGSWIDGET_H
