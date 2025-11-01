#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QSystemTrayIcon>
#include <QMenu>
#include "CameraController.h"
#include "TrackingControlWidget.h"
#include "PTZControlWidget.h"
#include "CameraSettingsWidget.h"
#include "CameraPreviewWidget.h"
#include "VideoEffectsWidget.h"

class PreviewWindow;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QFrame;
class QWidget;
class QLineEdit;
class QComboBox;
class VirtualCameraStreamer;

/**
 * @brief Main application window
 *
 * Shows all camera controls with an optional detached camera preview window
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onCameraConnected(const CameraController::CameraInfo &info);
    void onCameraDisconnected();
    void onStateChanged(const CameraController::CameraState &state);
    void onCommandFailed(const QString &description, int errorCode);
    void updateStatus();
    void onStateChangedSaveConfig();
    void onTogglePreview(bool enabled);
    void onDetachPreviewToggled(bool checked);
    void onPreviewStarted();
    void onPreviewFailed(const QString &error);
    void onPreviewFormatChanged(const QString &formatId);
    void onPresetUpdated(int index, double pan, double tilt, double zoom, bool defined);
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onShowHideAction();
    void onQuitAction();
    void onStartMinimizedToggled(bool checked);
    void onPreviewWindowClosed();
    void onVirtualCameraToggled(bool enabled);
    void onVirtualCameraDeviceEdited();
    void onVirtualCameraResolutionChanged(int index);
    void onVirtualCameraError(const QString &message);
    void onVideoEffectsChanged(const FilterPreviewWidget::VideoEffectsSettings &settings);

private:
    void setupUI();
    void setupTrayIcon();
    void loadConfiguration();
    void handleConfigErrors(const std::vector<Config::ValidationError> &errors);
    CameraController::CameraState getUIState() const;  // Get current UI state
    QString getProcessUsingCamera(const QString &devicePath);  // Detect which process is using camera
    QString findObsbotVideoDevice();  // Find which /dev/video* device is the OBSBOT camera
    void applyModernStyle();
    void detachPreviewToWindow();
    void attachPreviewToPanel();
    void updatePreviewControls();
    void updateStatusBanner(bool connected);
    QString currentVirtualCameraDevicePath() const;
    void updateVirtualCameraAvailability(const QString &devicePath);
    void updateVirtualCameraStreamerState();

    // Controller
    CameraController *m_controller;

    // UI
    QPushButton *m_previewToggleButton;
    QPushButton *m_detachPreviewButton;
    QPushButton *m_reconnectButton;
    QLabel *m_deviceInfoLabel;
    QLabel *m_cameraWarningLabel;  // Warning for camera in use
    QLabel *m_statusLabel;
    QCheckBox *m_startMinimizedCheckbox;
    QLabel *m_statusChip;
    QSplitter *m_splitter;
    QFrame *m_previewCard;
    QStackedWidget *m_previewStack;
    QLabel *m_previewPlaceholder;
    QFrame *m_controlCard;
    QTabWidget *m_tabWidget;
    QFrame *m_statusBanner;
    QHBoxLayout *m_mainLayout;
    QCheckBox *m_virtualCameraCheckbox;
    QLineEdit *m_virtualCameraDeviceEdit;
    QComboBox *m_virtualCameraResolutionCombo;
    QLabel *m_virtualCameraStatusLabel;

    // Control widgets
    TrackingControlWidget *m_trackingWidget;
    PTZControlWidget *m_ptzWidget;
    CameraSettingsWidget *m_settingsWidget;
    VideoEffectsWidget *m_effectsWidget;
    CameraPreviewWidget *m_previewWidget;
    PreviewWindow *m_previewWindow;
    VirtualCameraStreamer *m_virtualCameraStreamer;

    // Status timer
    QTimer *m_statusTimer;

    // Track preview state before minimize
    bool m_previewStateBeforeMinimize;
    bool m_previewDetached;
    bool m_widthLocked;
    int m_dockedMinWidth;
    int m_previewCardMinWidth;
    int m_previewCardMaxWidth;
    QSize m_lastDockedSize;

    // System tray
    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;

    bool m_isApplyingStyle;
    bool m_virtualCameraErrorNotified;
    bool m_virtualCameraAvailable;

protected:
    bool event(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
};

#endif // MAINWINDOW_H
