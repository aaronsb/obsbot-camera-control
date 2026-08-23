#ifndef PTZCONTROLWIDGET_H
#define PTZCONTROLWIDGET_H

#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QLineEdit>
#include <QStackedWidget>
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
    QGroupBox *positionPresetsGroup() const { return m_positionPresetGroup; }
    QGroupBox *imagePresetsGroup() const { return m_imagePresetGroup; }

    struct PresetState {
        bool defined;
        QString name;
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
    void presetUpdated(int index, double pan, double tilt, double zoom,
                       bool defined, const QString &name);
    void imagePresetUpdated(int index);

private slots:
    void onRecallPreset();
    void showPresetMenu(const QPoint &position);
    void onRecallImagePreset();
    void showImagePresetMenu(const QPoint &position);

private:
    CameraController *m_controller;
    CameraSettingsWidget *m_settingsWidget;
    QGroupBox *m_positionPresetGroup;
    QGroupBox *m_imagePresetGroup;

    struct PresetUi {
        QPushButton *recallButton;
        QLineEdit *renameEditor;
        QStackedWidget *stack;
        bool defined;
        QString name;
        double pan;
        double tilt;
        double zoom;
    };

    struct ImagePresetUi {
        QPushButton *recallButton;
        QLineEdit *renameEditor;
        QStackedWidget *stack;
        QString name;
        ImagePresetState state;
    };

    std::array<PresetUi, 3> m_presets;
    std::array<ImagePresetUi, 3> m_imagePresets;

    void updatePresetLabel(int index);
    void storePreset(int index);
    void deletePreset(int index);
    void beginPresetRename(int index);
    void updateImagePresetLabel(int index);
    void storeImagePreset(int index);
    void deleteImagePreset(int index);
    void beginImagePresetRename(int index);
};

#endif // PTZCONTROLWIDGET_H
