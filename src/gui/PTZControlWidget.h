#ifndef PTZCONTROLWIDGET_H
#define PTZCONTROLWIDGET_H

#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <array>
#include "CameraController.h"

class CameraSettingsWidget;

/**
 * @brief Widget for camera preset management
 * Manages PTZ (position/zoom) presets and image quality presets
 */
class PTZControlWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PTZControlWidget(CameraController *controller, QWidget *parent = nullptr);

    void setCameraSettingsWidget(CameraSettingsWidget *settingsWidget) {
        m_settingsWidget = settingsWidget;
    }

    struct PresetState {
        bool defined;
        double pan;
        double tilt;
        double zoom;
    };
    void applyPresetStates(const std::array<PresetState, 3> &presets);
    std::array<PresetState, 3> currentPresets() const;

    struct ImagePresetState {
        bool defined;
        bool hdrEnabled;
        int fovMode;
        bool faceAE;
        bool faceFocus;
        bool brightnessAuto;
        int brightness;
        bool contrastAuto;
        int contrast;
        bool saturationAuto;
        int saturation;
        int whiteBalance;
        int whiteBalanceKelvin;
    };
    void applyImagePresetStates(const std::array<ImagePresetState, 3> &presets);
    std::array<ImagePresetState, 3> currentImagePresets() const;

signals:
    void presetUpdated(int index, double pan, double tilt, double zoom, bool defined);
    void imagePresetUpdated(int index);

private slots:
    void onRecallPreset();
    void onStorePreset();
    void onRecallImagePreset();
    void onStoreImagePreset();

private:
    CameraController *m_controller;
    CameraSettingsWidget *m_settingsWidget;

    struct PresetUi {
        QPushButton *recallButton;
        QPushButton *saveButton;
        QLabel *statusLabel;
        bool defined;
        double pan;
        double tilt;
        double zoom;
    };

    struct ImagePresetUi {
        QPushButton *recallButton;
        QPushButton *saveButton;
        QLabel *statusLabel;
        ImagePresetState state;
    };

    std::array<PresetUi, 3> m_presets;
    std::array<ImagePresetUi, 3> m_imagePresets;

    void updatePresetLabel(int index);
    void updateImagePresetLabel(int index);
};

#endif // PTZCONTROLWIDGET_H
