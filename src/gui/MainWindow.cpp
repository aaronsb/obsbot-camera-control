#include "MainWindow.h"
#include "PreviewWindow.h"
#include "VirtualCameraStreamer.h"

#include <QMessageBox>
#include <QWidget>
#include <QIcon>
#include <QEvent>
#include <QWindowStateChangeEvent>
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QFrame>
#include <QStyle>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QSpacerItem>
#include <QSizePolicy>
#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QColor>
#include <QPalette>
#include <QList>
#include <QFileInfo>
#include <iostream>
#include <array>
#include <algorithm>

namespace {

QString toCssColor(const QColor &color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QColor withAlphaF(const QColor &color, qreal alpha)
{
    QColor c(color);
    c.setAlphaF(std::clamp(alpha, 0.0, 1.0));
    return c;
}

QColor blendColors(const QColor &from, const QColor &to, qreal progress)
{
    progress = std::clamp(progress, 0.0, 1.0);
    const qreal inverse = 1.0 - progress;
    return QColor::fromRgbF(
        from.redF() * inverse + to.redF() * progress,
        from.greenF() * inverse + to.greenF() * progress,
        from.blueF() * inverse + to.blueF() * progress,
        from.alphaF() * inverse + to.alphaF() * progress);
}

struct VirtualCameraResolutionPreset {
    const char *key;
    int width;
    int height;
};

constexpr VirtualCameraResolutionPreset kVirtualCameraResolutionPresets[] = {
    {"match", 0, 0},
    {"960x540", 960, 540},
    {"1280x720", 1280, 720},
    {"1920x1080", 1920, 1080}
};

QString buildResolutionLabel(const VirtualCameraResolutionPreset &preset)
{
    if (preset.width <= 0 || preset.height <= 0) {
        return MainWindow::tr("Match preview resolution");
    }

    const int progressiveHeight = preset.height;
    return MainWindow::tr("%1p (%2 × %3)")
        .arg(progressiveHeight)
        .arg(preset.width)
        .arg(preset.height);
}

QSize resolutionSizeForKey(const QString &key)
{
    if (key.isEmpty() || key.compare(QStringLiteral("match"), Qt::CaseInsensitive) == 0) {
        return QSize();
    }

    QString normalized = key.trimmed();
    normalized.replace(QLatin1Char('X'), QLatin1Char('x'));

    const int separator = normalized.indexOf(QLatin1Char('x'));
    if (separator <= 0) {
        return QSize();
    }

    bool okWidth = false;
    bool okHeight = false;
    const int width = normalized.left(separator).toInt(&okWidth);
    const int height = normalized.mid(separator + 1).toInt(&okHeight);

    if (!okWidth || !okHeight || width <= 0 || height <= 0) {
        return QSize();
    }

    return QSize(width, height);
}

bool isDefaultResolutionKey(const QString &key)
{
    for (const auto &preset : kVirtualCameraResolutionPresets) {
        if (QLatin1String(preset.key).compare(key, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString modprobeCommandForDevice(const QString &devicePath)
{
    static const QRegularExpression videoRegex(QStringLiteral("^/dev/video(\\d+)$"));
    const QRegularExpressionMatch match = videoRegex.match(devicePath.trimmed());
    const QString videoNr = match.hasMatch() ? match.captured(1) : QStringLiteral("42");
    return MainWindow::tr("sudo modprobe v4l2loopback video_nr=%1 card_label=\"OBSBOT Virtual Camera\" exclusive_caps=1")
        .arg(videoNr);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_previewStateBeforeMinimize(false)
    , m_previewDetached(false)
    , m_widthLocked(false)
    , m_dockedMinWidth(0)
    , m_previewCardMinWidth(0)
    , m_previewCardMaxWidth(QWIDGETSIZE_MAX)
    , m_virtualCameraCheckbox(nullptr)
    , m_virtualCameraDeviceEdit(nullptr)
    , m_virtualCameraResolutionCombo(nullptr)
    , m_virtualCameraStatusLabel(nullptr)
    , m_effectsWidget(nullptr)
    , m_virtualCameraStreamer(nullptr)
    , m_isApplyingStyle(false)
    , m_virtualCameraErrorNotified(false)
    , m_virtualCameraAvailable(false)
{
    setWindowTitle("OBSBOT Control");
    setWindowIcon(QIcon(":/icons/camera.svg"));

    // Create controller
    m_controller = new CameraController(this);

    // Connect signals
    connect(m_controller, &CameraController::cameraConnected,
            this, &MainWindow::onCameraConnected);
    connect(m_controller, &CameraController::cameraDisconnected,
            this, &MainWindow::onCameraDisconnected);
    connect(m_controller, &CameraController::stateChanged,
            this, &MainWindow::onStateChanged);
    connect(m_controller, &CameraController::commandFailed,
            this, &MainWindow::onCommandFailed);

    m_virtualCameraStreamer = new VirtualCameraStreamer(this);
    connect(m_virtualCameraStreamer, &VirtualCameraStreamer::errorOccurred,
            this, &MainWindow::onVirtualCameraError);

    setupUI();
    setupTrayIcon();

    m_lastDockedSize = size();

    // Load configuration
    loadConfiguration();

    // Check if we should start minimized
    if (m_controller->getConfig().getSettings().startMinimized) {
        // Start hidden in tray
        QTimer::singleShot(0, this, [this]() {
            hide();
        });
    }

    // Start connecting to camera
    m_controller->connectToCamera();

    // Update status periodically
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statusTimer->start(2000);
}

MainWindow::~MainWindow()
{
    // Save config on exit
    if (m_controller->isConnected()) {
        m_controller->saveConfig();
    }
}

void MainWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    m_mainLayout = new QHBoxLayout(centralWidget);
    m_mainLayout->setContentsMargins(16, 16, 16, 16);
    m_mainLayout->setSpacing(16);

    m_splitter = new QSplitter(Qt::Horizontal, centralWidget);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(10);
    m_mainLayout->addWidget(m_splitter);

    // Preview column
    m_previewCard = new QFrame(m_splitter);
    m_previewCard->setObjectName("previewCard");
    m_previewCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_previewCard->setMinimumWidth(520);
    QVBoxLayout *previewLayout = new QVBoxLayout(m_previewCard);
    previewLayout->setContentsMargins(16, 16, 16, 16);
    previewLayout->setSpacing(12);

    QHBoxLayout *previewHeader = new QHBoxLayout();
    previewHeader->setContentsMargins(0, 0, 0, 0);
    previewHeader->setSpacing(8);

    QLabel *previewTitle = new QLabel(tr("Live Preview"), m_previewCard);
    previewTitle->setObjectName("previewTitle");
    previewHeader->addWidget(previewTitle);
    previewHeader->addStretch();

    m_detachPreviewButton = new QPushButton(tr("Pop Out Preview"), m_previewCard);
    m_detachPreviewButton->setObjectName("detachButton");
    m_detachPreviewButton->setCheckable(true);
    m_detachPreviewButton->setEnabled(false);
    connect(m_detachPreviewButton, &QPushButton::toggled,
            this, &MainWindow::onDetachPreviewToggled);
    previewHeader->addWidget(m_detachPreviewButton);
    previewLayout->addLayout(previewHeader);

    m_previewStack = new QStackedWidget(m_previewCard);
    m_previewStack->setObjectName("previewStack");
    previewLayout->addWidget(m_previewStack, 1);

    m_previewWidget = new CameraPreviewWidget();
    m_previewWidget->setVirtualCameraStreamer(m_virtualCameraStreamer);
    m_previewWidget->setControlsVisible(true);
    m_previewWidget->setMinimumSize(320, 240);

    // Move the preview widget's control row into the header for compact layout
    QWidget *controlRow = m_previewWidget->findChild<QWidget*>("controlRow");
    if (controlRow) {
        // Insert after the title and spacing, before the stretch
        previewHeader->insertWidget(2, controlRow);
        previewHeader->insertSpacing(2, 16);
    }

    m_previewStack->addWidget(m_previewWidget);

    m_previewPlaceholder = new QLabel(tr("Preview not active.\nUse \"Start Preview\" to view the camera."), m_previewCard);
    m_previewPlaceholder->setObjectName("previewPlaceholder");
    m_previewPlaceholder->setAlignment(Qt::AlignCenter);
    m_previewPlaceholder->setWordWrap(true);
    m_previewPlaceholder->setMinimumHeight(240);
    m_previewStack->addWidget(m_previewPlaceholder);
    m_previewStack->setCurrentWidget(m_previewPlaceholder);

    // Control column
    m_controlCard = new QFrame(m_splitter);
    m_controlCard->setObjectName("controlCard");
    m_controlCard->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_controlCard->setMinimumWidth(360);
    m_controlCard->setMaximumWidth(420);

    QVBoxLayout *controlLayout = new QVBoxLayout(m_controlCard);
    controlLayout->setContentsMargins(18, 20, 18, 20);
    controlLayout->setSpacing(18);

    m_statusBanner = new QFrame(m_controlCard);
    m_statusBanner->setObjectName("statusBanner");
    m_statusBanner->setProperty("state", "disconnected");
    QVBoxLayout *statusLayout = new QVBoxLayout(m_statusBanner);
    statusLayout->setContentsMargins(16, 18, 16, 16);
    statusLayout->setSpacing(10);

    QHBoxLayout *statusHeader = new QHBoxLayout();
    statusHeader->setContentsMargins(0, 0, 0, 0);
    statusHeader->setSpacing(10);
    m_statusChip = new QLabel(tr("Camera • Offline"), m_statusBanner);
    m_statusChip->setObjectName("statusChip");
    statusHeader->addWidget(m_statusChip);
    statusHeader->addStretch();
    statusLayout->addLayout(statusHeader);

    m_deviceInfoLabel = new QLabel(tr("Connecting to camera..."), m_statusBanner);
    m_deviceInfoLabel->setObjectName("deviceInfoLabel");
    m_deviceInfoLabel->setWordWrap(true);
    statusLayout->addWidget(m_deviceInfoLabel);

    m_cameraWarningLabel = new QLabel(QString(), m_statusBanner);
    m_cameraWarningLabel->setObjectName("warningLabel");
    m_cameraWarningLabel->setWordWrap(true);
    m_cameraWarningLabel->setVisible(false);
    statusLayout->addWidget(m_cameraWarningLabel);

    controlLayout->addWidget(m_statusBanner);

    QWidget *actionRow = new QWidget(m_controlCard);
    actionRow->setObjectName("actionRow");
    QHBoxLayout *actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(14);

    m_previewToggleButton = new QPushButton(tr("Start Preview"), actionRow);
    m_previewToggleButton->setObjectName("primaryAction");
    m_previewToggleButton->setCheckable(true);
    connect(m_previewToggleButton, &QPushButton::toggled,
            this, &MainWindow::onTogglePreview);
    actionLayout->addWidget(m_previewToggleButton, 1);

    m_reconnectButton = new QPushButton(tr("Reconnect"), actionRow);
    m_reconnectButton->setObjectName("secondaryAction");
    connect(m_reconnectButton, &QPushButton::clicked, this, [this]() {
        m_controller->disconnectFromCamera();
        m_controller->connectToCamera();
    });
    actionLayout->addWidget(m_reconnectButton);

    controlLayout->addWidget(actionRow);

    m_trackingWidget = new TrackingControlWidget(m_controller, this);
    m_ptzWidget = new PTZControlWidget(m_controller, this);
    m_settingsWidget = new CameraSettingsWidget(m_controller, this);
    m_ptzWidget->setCameraSettingsWidget(m_settingsWidget);
    connect(m_ptzWidget, &PTZControlWidget::presetUpdated,
            this, &MainWindow::onPresetUpdated);

    m_tabWidget = new QTabWidget(m_controlCard);
    m_tabWidget->setObjectName("controlTabs");
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->addTab(m_trackingWidget, tr("Tracking"));
    m_tabWidget->addTab(m_ptzWidget, tr("Presets"));
    m_tabWidget->addTab(m_settingsWidget, tr("Camera Image"));
    m_effectsWidget = new VideoEffectsWidget(this);
    connect(m_effectsWidget, &VideoEffectsWidget::effectsChanged,
            this, &MainWindow::onVideoEffectsChanged);
    m_tabWidget->addTab(m_effectsWidget, tr("Creative FX"));
    controlLayout->addWidget(m_tabWidget, 1);
    m_effectsWidget->reset();

    QGroupBox *virtualCameraGroup = new QGroupBox(tr("Virtual Camera"), m_controlCard);
    QVBoxLayout *virtualLayout = new QVBoxLayout(virtualCameraGroup);
    virtualLayout->setContentsMargins(16, 16, 16, 16);
    virtualLayout->setSpacing(10);

    m_virtualCameraCheckbox = new QCheckBox(tr("Enable virtual camera output"), virtualCameraGroup);
    m_virtualCameraCheckbox->setObjectName("footerCheckbox");
    connect(m_virtualCameraCheckbox, &QCheckBox::toggled,
            this, &MainWindow::onVirtualCameraToggled);
    virtualLayout->addWidget(m_virtualCameraCheckbox);

    QHBoxLayout *virtualDeviceLayout = new QHBoxLayout();
    virtualDeviceLayout->setContentsMargins(0, 0, 0, 0);
    virtualDeviceLayout->setSpacing(8);

    QLabel *virtualDeviceLabel = new QLabel(tr("Device path"), virtualCameraGroup);
    virtualDeviceLayout->addWidget(virtualDeviceLabel);

    m_virtualCameraDeviceEdit = new QLineEdit(virtualCameraGroup);
    m_virtualCameraDeviceEdit->setPlaceholderText("/dev/video42");
    connect(m_virtualCameraDeviceEdit, &QLineEdit::editingFinished,
            this, &MainWindow::onVirtualCameraDeviceEdited);
    virtualDeviceLayout->addWidget(m_virtualCameraDeviceEdit, 1);

    virtualLayout->addLayout(virtualDeviceLayout);

    m_virtualCameraStatusLabel = new QLabel(
        tr("Virtual camera support requires the v4l2loopback kernel module."),
        virtualCameraGroup);
    m_virtualCameraStatusLabel->setWordWrap(true);
    m_virtualCameraStatusLabel->setObjectName("virtualCameraStatus");
    virtualLayout->addWidget(m_virtualCameraStatusLabel);

    QHBoxLayout *virtualResolutionLayout = new QHBoxLayout();
    virtualResolutionLayout->setContentsMargins(0, 0, 0, 0);
    virtualResolutionLayout->setSpacing(8);

    QLabel *virtualResolutionLabel = new QLabel(tr("Output resolution"), virtualCameraGroup);
    virtualResolutionLayout->addWidget(virtualResolutionLabel);

    m_virtualCameraResolutionCombo = new QComboBox(virtualCameraGroup);
    m_virtualCameraResolutionCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    for (const auto &preset : kVirtualCameraResolutionPresets) {
        const QString key = QLatin1String(preset.key);
        const QString label = buildResolutionLabel(preset);
        m_virtualCameraResolutionCombo->addItem(label, key);
    }
    connect(m_virtualCameraResolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onVirtualCameraResolutionChanged);
    virtualResolutionLayout->addWidget(m_virtualCameraResolutionCombo, 1);

    virtualLayout->addLayout(virtualResolutionLayout);

    QLabel *virtualResolutionHint = new QLabel(tr("Pick a fixed size to keep Zoom and other apps happy when you change preview quality."), virtualCameraGroup);
    virtualResolutionHint->setWordWrap(true);
    virtualResolutionHint->setStyleSheet("color: palette(mid); font-size: 11px;");
    virtualLayout->addWidget(virtualResolutionHint);
    controlLayout->addWidget(virtualCameraGroup);

    m_startMinimizedCheckbox = new QCheckBox(tr("Launch minimized / Close to tray"), m_controlCard);
    m_startMinimizedCheckbox->setObjectName("footerCheckbox");
    connect(m_startMinimizedCheckbox, &QCheckBox::toggled,
            this, &MainWindow::onStartMinimizedToggled);
    controlLayout->addWidget(m_startMinimizedCheckbox);

    m_statusLabel = new QLabel(tr("Status: Initializing..."), m_controlCard);
    m_statusLabel->setObjectName("footerStatus");
    m_statusLabel->setWordWrap(true);
    controlLayout->addWidget(m_statusLabel);

    m_splitter->addWidget(m_previewCard);
    m_splitter->addWidget(m_controlCard);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);

    // Detached preview window
    m_previewWindow = new PreviewWindow(this);
    m_previewWindow->hide();
    connect(m_previewWindow, &PreviewWindow::previewClosed,
            this, &MainWindow::onPreviewWindowClosed);

    connect(m_previewWidget, &CameraPreviewWidget::previewStarted,
            this, &MainWindow::onPreviewStarted);
    connect(m_previewWidget, &CameraPreviewWidget::previewFailed,
            this, &MainWindow::onPreviewFailed);
    connect(m_previewWidget, &CameraPreviewWidget::preferredFormatChanged,
            this, &MainWindow::onPreviewFormatChanged);

    applyModernStyle();
    updateStatusBanner(false);
    updatePreviewControls();

    m_previewCardMinWidth = m_previewCard->minimumWidth();
    m_previewCardMaxWidth = m_previewCard->maximumWidth();
    m_dockedMinWidth = sizeHint().width();
    setMinimumWidth(m_dockedMinWidth);
    setMaximumWidth(QWIDGETSIZE_MAX);

    const int desiredWidth = 1600;
    const int desiredHeight = 900;
    const int width = std::max(m_dockedMinWidth, desiredWidth);
    const int height = std::max(sizeHint().height(), desiredHeight);
    resize(width, height);
}

void MainWindow::applyModernStyle()
{
    if (m_isApplyingStyle) {
        return;
    }
    m_isApplyingStyle = true;

    const QPalette pal = palette();

    const QColor window = pal.color(QPalette::Window);
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor button = pal.color(QPalette::Button);
    const QColor buttonText = pal.color(QPalette::ButtonText);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlightedText = pal.color(QPalette::HighlightedText);
    const QColor shadow = pal.color(QPalette::Shadow);
    const QColor brightText = pal.color(QPalette::BrightText);
    const QColor placeholder = pal.color(QPalette::PlaceholderText);

    const QColor cardBackground = blendColors(base, window, 0.5);
    const QColor cardBorder = withAlphaF(blendColors(shadow, text, 0.4), 0.45);

    const QColor mutedTone = blendColors(text, shadow, 0.35);
    const QColor mutedBackground = withAlphaF(blendColors(mutedTone, cardBackground, 0.2), 0.35);
    const QColor mutedBorder = withAlphaF(blendColors(mutedTone, cardBorder, 0.4), 0.55);
    const QColor mutedChipBackground = withAlphaF(blendColors(mutedTone, cardBackground, 0.25), 0.6);
    const QColor mutedChipText = blendColors(text, highlightedText, 0.1);

    const QColor accentBackground = withAlphaF(blendColors(highlight, cardBackground, 0.25), 0.48);
    const QColor accentBorder = withAlphaF(blendColors(highlight, cardBorder, 0.25), 0.75);
    const QColor accentChipBackground = withAlphaF(blendColors(highlight, cardBackground, 0.15), 0.7);
    const QColor accentChipText = blendColors(highlightedText.isValid() ? highlightedText : brightText, text, 0.2);

    const QColor warningColor = blendColors(highlight.lighter(140), text, 0.4);

    const QColor previewPlaceholderText = placeholder.isValid()
        ? placeholder
        : blendColors(text, mutedTone, 0.45);
    const QColor previewPlaceholderBorder = withAlphaF(blendColors(text, cardBackground, 0.2), 0.35);

    const QColor buttonBase = withAlphaF(blendColors(button, cardBackground, 0.2), 0.55);
    const QColor buttonHover = withAlphaF(blendColors(button, cardBackground, 0.15), 0.7);
    const QColor buttonPressed = withAlphaF(blendColors(button, cardBackground, 0.1), 0.8);
    const QColor buttonTextColor = buttonText.isValid() ? buttonText : text;

    const QColor primaryBackground = highlight;
    const QColor primaryHover = highlight.lighter(115);
    const QColor primaryChecked = highlight.darker(120);
    const QColor primaryCheckedHover = highlight.darker(135);
    const QColor primaryText = highlightedText.isValid() ? highlightedText : brightText;

    const QColor secondaryBackground = buttonBase;
    const QColor secondaryHover = withAlphaF(blendColors(button, cardBackground, 0.18), 0.75);

    const QColor comboBorder = withAlphaF(blendColors(text, cardBackground, 0.3), 0.5);
    const QColor comboBackground = withAlphaF(blendColors(button, cardBackground, 0.25), 0.6);
    const QColor comboPopupBackground = cardBackground;
    const QColor comboPopupBorder = withAlphaF(blendColors(text, cardBorder, 0.25), 0.6);
    const QColor comboSelectionBackground = withAlphaF(highlight, 0.75);
    const QColor comboSelectionText = accentChipText;

    const QColor groupBorder = withAlphaF(blendColors(cardBorder, mutedTone, 0.35), 0.55);
    const QColor tabBackground = withAlphaF(blendColors(button, cardBackground, 0.2), 0.5);

    const QColor footerStatusColor = withAlphaF(text, 0.65);
    const QColor footerCheckboxColor = withAlphaF(text, 0.7);
    const QColor detachCheckedText = highlight;

    const QString style = QStringLiteral(R"(
        QFrame#previewCard, QFrame#controlCard {
            background-color: %1;
            border-radius: 18px;
            border: 1px solid %2;
        }
        QFrame#statusBanner {
            border-radius: 12px;
            border: 1px solid %3;
            background-color: %4;
        }
        QFrame#statusBanner[state="connected"] {
            background-color: %5;
            border: 1px solid %6;
        }
        QLabel#deviceInfoLabel {
            font-weight: 600;
        }
        QLabel#statusChip {
            padding: 6px 12px;
            border-radius: 14px;
            background-color: %7;
            color: %8;
            font-weight: 600;
            letter-spacing: 0.3px;
        }
        QFrame#statusBanner[state="connected"] QLabel#statusChip {
            background-color: %9;
            color: %10;
        }
        QLabel#warningLabel {
            color: %11;
            font-weight: 600;
        }
        QLabel#previewTitle {
            font-size: 16px;
            font-weight: 600;
        }
        QLabel#previewPlaceholder {
            color: %12;
            border: 1px dashed %13;
            border-radius: 12px;
            padding: 28px;
        }
        QGroupBox {
            border: 1px solid %14;
            border-radius: 14px;
            margin-top: 14px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 18px;
            top: -2px;
            padding: 0 8px;
            font-weight: 600;
            background-color: %1;
        }
        QTabWidget#controlTabs::pane {
            border: none;
            margin-top: 18px;
        }
        QTabWidget#controlTabs::tab-bar {
            alignment: center;
        }
        QTabBar::tab {
            padding: 6px 10px;
            margin: 0 2px;
            font-size: 13px;
        }
        QTabBar::tab:selected {
            font-weight: 600;
        }
        QLabel#footerStatus {
            color: %15;
        }
        QCheckBox#footerCheckbox {
            color: %16;
        }
    )")
        .arg(toCssColor(cardBackground))
        .arg(toCssColor(cardBorder))
        .arg(toCssColor(mutedBorder))
        .arg(toCssColor(mutedBackground))
        .arg(toCssColor(accentBackground))
        .arg(toCssColor(accentBorder))
        .arg(toCssColor(mutedChipBackground))
        .arg(toCssColor(mutedChipText))
        .arg(toCssColor(accentChipBackground))
        .arg(toCssColor(accentChipText))
        .arg(toCssColor(warningColor))
        .arg(toCssColor(previewPlaceholderText))
        .arg(toCssColor(previewPlaceholderBorder))
        .arg(toCssColor(groupBorder))
        .arg(toCssColor(footerStatusColor))
        .arg(toCssColor(footerCheckboxColor));

    setStyleSheet(style);
    m_isApplyingStyle = false;
}

bool MainWindow::event(QEvent *event)
{
    const bool handled = QMainWindow::event(event);

    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::ThemeChange:
        applyModernStyle();
        break;
    default:
        break;
    }

    return handled;
}

void MainWindow::detachPreviewToWindow()
{
    if (m_previewDetached) {
        return;
    }

    m_lastDockedSize = size();

    if (m_previewStack->indexOf(m_previewWidget) != -1) {
        m_previewStack->removeWidget(m_previewWidget);
    }

    m_previewWidget->setControlsVisible(false);
    m_previewWidget->setParent(nullptr);
    m_previewWindow->setPreviewWidget(m_previewWidget);
    m_previewWidget->show();
    m_previewWindow->show();
    m_previewWindow->raise();
    m_previewWindow->activateWindow();

    m_previewStack->setCurrentWidget(m_previewPlaceholder);
    m_previewCard->setMinimumWidth(0);
    m_previewCard->setMaximumWidth(0);
    m_previewCard->hide();
    QList<int> sizes;
    sizes << 0 << m_controlCard->sizeHint().width();
    m_splitter->setSizes(sizes);

    const int margins = m_mainLayout->contentsMargins().left() + m_mainLayout->contentsMargins().right();
    const int targetWidth = margins + m_controlCard->sizeHint().width() + m_splitter->handleWidth();
    setMinimumWidth(targetWidth);
    setMaximumWidth(targetWidth);
    resize(targetWidth, height());
    m_widthLocked = true;
    m_previewDetached = true;
}

void MainWindow::attachPreviewToPanel()
{
    if (!m_previewDetached) {
        m_previewCard->setMinimumWidth(m_previewCardMinWidth);
        m_previewCard->setMaximumWidth(m_previewCardMaxWidth);
        m_previewCard->show();
        if (m_widthLocked) {
            m_widthLocked = false;
            setMinimumWidth(m_dockedMinWidth);
            setMaximumWidth(QWIDGETSIZE_MAX);
            resize(std::max(width(), m_dockedMinWidth), height());
        }
        if (m_previewStack->indexOf(m_previewWidget) == -1) {
            m_previewStack->insertWidget(0, m_previewWidget);
        }
        m_lastDockedSize = size();
        return;
    }

    CameraPreviewWidget *widget = m_previewWindow->takePreviewWidget();
    if (!widget) {
        widget = m_previewWidget;
    }

    if (m_previewStack->indexOf(widget) == -1) {
        m_previewStack->insertWidget(0, widget);
    }
    widget->setParent(m_previewStack);
    widget->show();
    widget->setControlsVisible(true);
    m_previewWidget = widget;

    m_previewCard->setMinimumWidth(m_previewCardMinWidth);
    m_previewCard->setMaximumWidth(m_previewCardMaxWidth);
    m_previewCard->show();
    QList<int> sizes;
    sizes << m_previewCardMinWidth + 40 << m_controlCard->sizeHint().width();
    m_splitter->setSizes(sizes);

    if (m_previewToggleButton->isChecked()) {
        m_previewStack->setCurrentWidget(widget);
    } else {
        m_previewStack->setCurrentWidget(m_previewPlaceholder);
    }

    if (m_widthLocked) {
        m_widthLocked = false;
        setMinimumWidth(m_dockedMinWidth);
        setMaximumWidth(QWIDGETSIZE_MAX);
        const int targetWidth = m_lastDockedSize.width() > 0
            ? std::max(m_lastDockedSize.width(), m_dockedMinWidth)
            : std::max(width(), m_dockedMinWidth);
        const int targetHeight = m_lastDockedSize.height() > 0
            ? m_lastDockedSize.height()
            : height();
        resize(targetWidth, targetHeight);
    }

    m_previewDetached = false;
    m_lastDockedSize = size();
}

void MainWindow::updatePreviewControls()
{
    const bool previewActive = m_previewToggleButton->isChecked();
    if (!previewActive) {
        if (m_previewDetached) {
            attachPreviewToPanel();
        }
        if (m_detachPreviewButton->isChecked()) {
            m_detachPreviewButton->blockSignals(true);
            m_detachPreviewButton->setChecked(false);
            m_detachPreviewButton->blockSignals(false);
        }
        m_detachPreviewButton->setEnabled(false);
        m_detachPreviewButton->setText(tr("Pop Out Preview"));
        m_previewToggleButton->setText(tr("Start Preview"));
    } else {
        m_detachPreviewButton->setEnabled(true);
        if (m_detachPreviewButton->isChecked() != m_previewDetached) {
            m_detachPreviewButton->blockSignals(true);
            m_detachPreviewButton->setChecked(m_previewDetached);
            m_detachPreviewButton->blockSignals(false);
        }
        m_detachPreviewButton->setText(m_previewDetached
            ? tr("Attach Preview")
            : tr("Pop Out Preview"));
        m_previewToggleButton->setText(tr("Stop Preview"));
    }
}

void MainWindow::updateStatusBanner(bool connected)
{
    m_statusBanner->setProperty("state", connected ? "connected" : "disconnected");
    m_statusChip->setText(connected ? tr("Camera • Online") : tr("Camera • Offline"));
    m_statusBanner->style()->unpolish(m_statusBanner);
    m_statusBanner->style()->polish(m_statusBanner);
    m_statusBanner->update();
}

void MainWindow::onTogglePreview(bool enabled)
{
    if (enabled) {
        m_cameraWarningLabel->setVisible(false);
        m_cameraWarningLabel->setText("");

        // Find which video device is the OBSBOT camera
        QString devicePath = findObsbotVideoDevice();

        if (devicePath.isEmpty()) {
            // Could not detect OBSBOT camera device
            QString warningText = "⚠ Cannot detect camera device\n(OBSBOT camera not found in video devices)";
            m_cameraWarningLabel->setText(warningText);
            m_cameraWarningLabel->setVisible(true);
            m_previewToggleButton->setChecked(false);
            return;
        }

        // Check if camera is already in use BEFORE doing any layout changes
        QString process = getProcessUsingCamera(devicePath);
        if (!process.isEmpty()) {
            // Camera is in use - show warning and abort
            QString warningText = "⚠ Cannot open camera preview\n(In use by: " + process + ")";
            m_cameraWarningLabel->setText(warningText);
            m_cameraWarningLabel->setVisible(true);

            // Uncheck the button
            m_previewToggleButton->setChecked(false);
            return;
        }

        // Try to enable preview - will emit previewStarted() or previewFailed()
        m_previewWidget->setCameraDeviceId(devicePath);
        m_previewWidget->enablePreview(true);

        if (!m_previewDetached) {
            if (m_previewStack->indexOf(m_previewWidget) == -1) {
                m_previewStack->insertWidget(0, m_previewWidget);
            }
            m_previewStack->setCurrentWidget(m_previewWidget);
        } else {
            m_previewWindow->setPreviewWidget(m_previewWidget);
            m_previewWindow->show();
            m_previewWindow->raise();
            m_previewWindow->activateWindow();
        }

    } else {
        attachPreviewToPanel();
        m_previewWidget->enablePreview(false);
        m_previewWindow->hide();
        m_previewStack->setCurrentWidget(m_previewPlaceholder);
    }

    updateVirtualCameraStreamerState();
    updatePreviewControls();
}

void MainWindow::onDetachPreviewToggled(bool checked)
{
    if (!m_previewToggleButton->isChecked()) {
        m_detachPreviewButton->blockSignals(true);
        m_detachPreviewButton->setChecked(false);
        m_detachPreviewButton->blockSignals(false);
        return;
    }

    if (checked) {
        detachPreviewToWindow();
    } else {
        attachPreviewToPanel();
    }

    updatePreviewControls();
}

void MainWindow::onPreviewWindowClosed()
{
    if (m_previewDetached) {
        attachPreviewToPanel();
        if (m_previewToggleButton->isChecked()) {
            m_previewStack->setCurrentWidget(m_previewWidget);
        }
    }

    updatePreviewControls();
}

void MainWindow::onCameraConnected(const CameraController::CameraInfo &info)
{
    QString deviceText = QString("✓ Connected:\n%1\n(v%2)")
        .arg(info.name)
        .arg(info.version);

    m_deviceInfoLabel->setText(deviceText);
    updateStatusBanner(true);
    m_cameraWarningLabel->setVisible(false);
    m_cameraWarningLabel->setText("");

    // Apply current UI state to camera asynchronously (respects user changes before connection)
    // Use a short delay to let the connection stabilize
    QTimer::singleShot(100, this, [this]() {
        auto uiState = getUIState();
        m_controller->applyCurrentStateToCamera(uiState);
    });

    updateStatus();

    if (m_previewToggleButton->isChecked()) {
        if (m_previewDetached) {
            m_previewWindow->show();
            m_previewWindow->raise();
            m_previewWindow->activateWindow();
        } else {
            m_previewStack->setCurrentWidget(m_previewWidget);
        }
    }
}

void MainWindow::onCameraDisconnected()
{
    m_deviceInfoLabel->setText("❌ Camera Disconnected");
    updateStatusBanner(false);
    m_statusLabel->setText("Status: Not connected");
    m_cameraWarningLabel->setVisible(false);
    m_cameraWarningLabel->setText("");

    attachPreviewToPanel();
    m_previewWindow->hide();
    m_previewStack->setCurrentWidget(m_previewPlaceholder);

    if (m_previewToggleButton->isChecked()) {
        m_previewToggleButton->setChecked(false);
    } else {
        updatePreviewControls();
    }
}

void MainWindow::onStateChanged(const CameraController::CameraState &state)
{
    // Update all widgets with new state
    m_trackingWidget->updateFromState(state);
    m_settingsWidget->updateFromState(state);
}

void MainWindow::onCommandFailed(const QString &description, int errorCode)
{
    QMessageBox::warning(this, "Command Failed",
        QString("%1 failed with error code: %2").arg(description).arg(errorCode));
}

void MainWindow::updateStatus()
{
    if (!m_controller->isConnected()) {
        return;
    }

    auto state = m_controller->getCurrentState();

    QStringList statusParts;
    statusParts << QString("AI: %1").arg(state.aiMode == 0 ? "Off" : "On");
    statusParts << QString("Zoom: %1%").arg(state.zoomRatio);
    statusParts << QString("HDR: %1").arg(state.hdrEnabled ? "On" : "Off");
    statusParts << QString("Face AE: %1").arg(state.faceAEEnabled ? "On" : "Off");
    statusParts << QString("Focus: %1").arg(state.autoFocusEnabled ? "Auto" : "Manual");

    m_statusLabel->setText("Status: " + statusParts.join(" | "));
}

QString MainWindow::currentVirtualCameraDevicePath() const
{
    if (!m_virtualCameraDeviceEdit) {
        return QStringLiteral("/dev/video42");
    }

    const QString path = m_virtualCameraDeviceEdit->text().trimmed();
    if (path.isEmpty()) {
        return QStringLiteral("/dev/video42");
    }

    return path;
}

void MainWindow::updateVirtualCameraAvailability(const QString &devicePath)
{
    if (!m_virtualCameraStatusLabel) {
        m_virtualCameraAvailable = false;
        return;
    }

    const QFileInfo moduleInfo(QStringLiteral("/sys/module/v4l2loopback"));
    const QFileInfo deviceInfo(devicePath);

    const bool deviceExists = deviceInfo.exists();
    const bool moduleLoaded = moduleInfo.exists();
    const QString modprobeCommand = modprobeCommandForDevice(devicePath);

    QString statusText;
    QString statusColor;

    if (deviceExists) {
        statusText = tr("Virtual camera available (%1)").arg(devicePath);
        statusColor = QStringLiteral("#2e7d32");
        m_virtualCameraAvailable = true;
    } else if (moduleLoaded) {
        statusText = tr("v4l2loopback is loaded, but %1 does not exist.\nRun: %2")
                         .arg(devicePath, modprobeCommand);
        statusColor = QStringLiteral("#b26a00");
        m_virtualCameraAvailable = false;
    } else {
        statusText = tr("Virtual camera support is disabled.\nInstall the module and load it with:\n%1")
                         .arg(modprobeCommand);
        statusColor = QStringLiteral("#b71c1c");
        m_virtualCameraAvailable = false;
    }

    m_virtualCameraStatusLabel->setText(statusText);
    m_virtualCameraStatusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(statusColor));
    if (m_virtualCameraCheckbox) {
        m_virtualCameraCheckbox->setToolTip(statusText);
    }
}

void MainWindow::updateVirtualCameraStreamerState()
{
    if (!m_virtualCameraStreamer) {
        return;
    }

    const QString devicePath = currentVirtualCameraDevicePath();

    updateVirtualCameraAvailability(devicePath);
    m_virtualCameraStreamer->setDevicePath(devicePath);

    QString resolutionKey;
    if (m_virtualCameraResolutionCombo && m_virtualCameraResolutionCombo->currentIndex() >= 0) {
        resolutionKey = m_virtualCameraResolutionCombo->currentData().toString();
    }
    if (resolutionKey.isEmpty()) {
        resolutionKey = QString::fromStdString(m_controller->getConfig().getSettings().virtualCameraResolution);
    }
    const QSize forcedSize = resolutionSizeForKey(resolutionKey);
    m_virtualCameraStreamer->setForcedResolution(forcedSize);

    const bool userRequested = m_virtualCameraCheckbox && m_virtualCameraCheckbox->isChecked();
    const bool previewActive = m_previewWidget && m_previewWidget->isPreviewEnabled();
    const bool enableOutput = userRequested && previewActive && m_virtualCameraAvailable;
    m_virtualCameraStreamer->setEnabled(enableOutput);
}

void MainWindow::loadConfiguration()
{
    std::vector<Config::ValidationError> errors;
    if (!m_controller->loadConfig(errors)) {
        // Config has validation errors
        handleConfigErrors(errors);
    }

    // Initialize UI widgets from config
    auto settings = m_controller->getConfig().getSettings();
    m_trackingWidget->setTrackingEnabled(settings.faceTracking);
    m_trackingWidget->setAiMode(settings.aiMode);
    m_trackingWidget->setHumanSubMode(settings.aiSubMode);
    m_trackingWidget->setAutoZoomEnabled(settings.autoZoom);
    m_trackingWidget->setTrackSpeed(settings.trackSpeed);
    m_trackingWidget->setAudioAutoGain(settings.audioAutoGain);
    m_settingsWidget->setHDREnabled(settings.hdr);
    m_settingsWidget->setFOVMode(settings.fov);
    m_settingsWidget->setFaceAEEnabled(settings.faceAE);
    m_settingsWidget->setFaceFocusEnabled(settings.faceFocus);

    // Image controls
    m_settingsWidget->setBrightnessAuto(settings.brightnessAuto);
    m_settingsWidget->setBrightness(settings.brightness);
    m_settingsWidget->setContrastAuto(settings.contrastAuto);
    m_settingsWidget->setContrast(settings.contrast);
    m_settingsWidget->setSaturationAuto(settings.saturationAuto);
    m_settingsWidget->setSaturation(settings.saturation);
    m_settingsWidget->setWhiteBalance(settings.whiteBalance);
    m_previewWidget->setPreferredFormatId(QString::fromStdString(settings.previewFormat));

    std::array<PTZControlWidget::PresetState, 3> presetStates{};
    for (int i = 0; i < 3; ++i) {
        const auto &preset = settings.presets[static_cast<size_t>(i)];
        presetStates[static_cast<size_t>(i)] = {
            preset.defined,
            preset.pan,
            preset.tilt,
            preset.zoom
        };
    }
    m_ptzWidget->applyPresetStates(presetStates);

    // Application settings - block signals to prevent saving during initialization
    m_startMinimizedCheckbox->blockSignals(true);
    m_startMinimizedCheckbox->setChecked(settings.startMinimized);
    m_startMinimizedCheckbox->blockSignals(false);

    if (m_virtualCameraCheckbox) {
        m_virtualCameraCheckbox->blockSignals(true);
        m_virtualCameraCheckbox->setChecked(settings.virtualCameraEnabled);
        m_virtualCameraCheckbox->blockSignals(false);
    }

    if (m_virtualCameraDeviceEdit) {
        m_virtualCameraDeviceEdit->blockSignals(true);
        m_virtualCameraDeviceEdit->setText(QString::fromStdString(settings.virtualCameraDevice));
        m_virtualCameraDeviceEdit->blockSignals(false);
    }

    m_settingsWidget->setWhiteBalanceKelvin(settings.whiteBalanceKelvin);

    if (m_virtualCameraResolutionCombo) {
        const QString key = QString::fromStdString(settings.virtualCameraResolution);
        m_virtualCameraResolutionCombo->blockSignals(true);
        int index = m_virtualCameraResolutionCombo->findData(key);
        if (index < 0) {
            const QSize customSize = resolutionSizeForKey(key);
            if (customSize.isValid()) {
                const QString label = tr("Custom (%1 × %2)")
                    .arg(customSize.width())
                    .arg(customSize.height());
                m_virtualCameraResolutionCombo->addItem(label, key);
                index = m_virtualCameraResolutionCombo->count() - 1;
            } else {
                index = m_virtualCameraResolutionCombo->findData(QStringLiteral("match"));
            }
        }
        if (index >= 0) {
            m_virtualCameraResolutionCombo->setCurrentIndex(index);
        }
        m_virtualCameraResolutionCombo->blockSignals(false);
    }

    m_virtualCameraErrorNotified = false;
    updateVirtualCameraStreamerState();
}

void MainWindow::handleConfigErrors(const std::vector<Config::ValidationError> &errors)
{
    QString errorMsg = "Configuration file has errors:\n\n";
    for (const auto &err : errors) {
        if (err.lineNumber > 0) {
            errorMsg += QString("Line %1: %2\n")
                .arg(err.lineNumber)
                .arg(QString::fromStdString(err.message));
        } else {
            errorMsg += QString::fromStdString(err.message) + "\n";
        }
    }

    errorMsg += "\nWhat would you like to do?";

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Config Error");
    msgBox.setText(errorMsg);
    msgBox.setIcon(QMessageBox::Warning);

    QPushButton *ignoreBtn = msgBox.addButton("Ignore (Don't Save)", QMessageBox::RejectRole);
    QPushButton *defaultsBtn = msgBox.addButton("Reset to Defaults", QMessageBox::AcceptRole);
    QPushButton *retryBtn = msgBox.addButton("Try Again", QMessageBox::ActionRole);

    msgBox.exec();

    if (msgBox.clickedButton() == ignoreBtn) {
        // User chose to ignore - disable saving
        m_controller->getConfig().disableSaving();
    } else if (msgBox.clickedButton() == defaultsBtn) {
        // User chose to reset to defaults
        m_controller->getConfig().resetToDefaults(true);
    } else if (msgBox.clickedButton() == retryBtn) {
        // User chose to try again - reload config
        loadConfiguration();
    }
}

void MainWindow::onStateChangedSaveConfig()
{
    // Debounced auto-save - only save if config saving is enabled
    // This prevents saving during transitional states
    if (m_controller->getConfig().isSavingEnabled()) {
        m_controller->saveConfig();
    }
}

CameraController::CameraState MainWindow::getUIState() const
{
    // Construct a state object from current UI widget values
    CameraController::CameraState state = {};

    // Get state from widgets
    state.autoFramingEnabled = m_trackingWidget->isTrackingEnabled();
    state.aiMode = m_trackingWidget->currentAiMode();
    state.aiSubMode = m_trackingWidget->currentHumanSubMode();
    state.autoZoomEnabled = m_trackingWidget->isAutoZoomEnabled();
    state.trackSpeedMode = m_trackingWidget->currentTrackSpeed();
    state.audioAutoGainEnabled = m_trackingWidget->isAudioAutoGainEnabled();
    state.hdrEnabled = m_settingsWidget->isHDREnabled();
    state.fovMode = m_settingsWidget->getFOVMode();
    state.faceAEEnabled = m_settingsWidget->isFaceAEEnabled();
    state.faceFocusEnabled = m_settingsWidget->isFaceFocusEnabled();

    // Image controls
    state.brightnessAuto = m_settingsWidget->isBrightnessAuto();
    state.brightness = m_settingsWidget->getBrightness();
    state.contrastAuto = m_settingsWidget->isContrastAuto();
    state.contrast = m_settingsWidget->getContrast();
    state.saturationAuto = m_settingsWidget->isSaturationAuto();
    state.saturation = m_settingsWidget->getSaturation();
    state.whiteBalance = m_settingsWidget->getWhiteBalance();
    state.whiteBalanceKelvin = m_settingsWidget->getWhiteBalanceKelvin();

    // Get PTZ state from controller (defaults from config)
    auto currentState = m_controller->getCurrentState();
    state.pan = currentState.pan;
    state.tilt = currentState.tilt;
    state.zoom = currentState.zoom;

    return state;
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);

    if (event->type() == QEvent::WindowStateChange) {
        QWindowStateChangeEvent *stateEvent = static_cast<QWindowStateChangeEvent*>(event);

        if (windowState() & Qt::WindowMinimized) {
            // Window is being minimized

            // Save preview state
            m_previewStateBeforeMinimize = m_previewWidget->isPreviewEnabled();

            // Disable preview if it's on
            if (m_previewStateBeforeMinimize) {
                m_previewToggleButton->setChecked(false);
            }

            // Disconnect from camera to free resources
            m_controller->disconnectFromCamera();

        } else if (stateEvent->oldState() & Qt::WindowMinimized) {
            // Window is being restored from minimized state

            // ALWAYS reconnect to camera (regardless of preview state)
            m_controller->connectToCamera();

            // ONLY restore preview if it was enabled before minimize
            if (m_previewStateBeforeMinimize) {
                QTimer::singleShot(1000, this, [this]() {
                    m_previewToggleButton->setChecked(true);
                });
            }
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    if (!m_previewDetached && !m_widthLocked) {
        m_lastDockedSize = event->size();
    }
    QMainWindow::resizeEvent(event);
}

void MainWindow::onPreviewStarted()
{
    m_cameraWarningLabel->setVisible(false);
    m_cameraWarningLabel->setText("");

    if (m_previewDetached) {
        m_previewWindow->show();
        m_previewWindow->raise();
        m_previewWindow->activateWindow();
    } else {
        if (m_previewStack->indexOf(m_previewWidget) == -1) {
            m_previewStack->insertWidget(0, m_previewWidget);
        }
        m_previewStack->setCurrentWidget(m_previewWidget);
    }

    updatePreviewControls();
}

void MainWindow::onPreviewFailed(const QString &error)
{
    Q_UNUSED(error);
    // Show warning when preview fails
    QString warningText = "⚠ Cannot open camera preview";

    // Try to detect which process is using the camera
    QString process = getProcessUsingCamera("/dev/video0");
    if (!process.isEmpty()) {
        warningText += "\n(In use by: " + process + ")";
    } else {
        warningText += "\n(In use by another application)";
    }

    m_cameraWarningLabel->setText(warningText);
    m_cameraWarningLabel->setVisible(true);

    attachPreviewToPanel();
    m_previewWindow->hide();
    m_previewStack->setCurrentWidget(m_previewPlaceholder);

    if (m_previewToggleButton->isChecked()) {
        m_previewToggleButton->setChecked(false);
    } else {
        updatePreviewControls();
    }
}

void MainWindow::onPreviewFormatChanged(const QString &formatId)
{
    auto settings = m_controller->getConfig().getSettings();
    settings.previewFormat = formatId.toStdString();
    m_controller->getConfig().setSettings(settings);
    m_controller->saveConfig();
    updateVirtualCameraStreamerState();
}

void MainWindow::onPresetUpdated(int index, double pan, double tilt, double zoom, bool defined)
{
    auto settings = m_controller->getConfig().getSettings();
    if (index < 0 || index >= static_cast<int>(settings.presets.size())) {
        return;
    }

    auto &preset = settings.presets[static_cast<size_t>(index)];
    preset.defined = defined;
    preset.pan = pan;
    preset.tilt = tilt;
    preset.zoom = zoom;

    m_controller->getConfig().setSettings(settings);
    m_controller->saveConfig();
}

QString MainWindow::findObsbotVideoDevice()
{
    // Find which /dev/video* device is the OBSBOT camera
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();

    for (const QCameraDevice &cameraDevice : cameras) {
        if (cameraDevice.description().contains("OBSBOT", Qt::CaseInsensitive) ||
            cameraDevice.description().contains("Meet", Qt::CaseInsensitive)) {
            // Try to extract device path from ID
            // QCameraDevice ID on Linux is typically the device path like "/dev/video0"
            QString id = cameraDevice.id();
            if (id.startsWith("/dev/video")) {
                return id;
            }
        }
    }

    // Not found - return empty string
    return QString();
}

QString MainWindow::getProcessUsingCamera(const QString &devicePath)
{
    // Use lsof to find which process has the camera device open
    QProcess process;
    process.start("lsof", QStringList() << devicePath);
    process.waitForFinished(1000);

    QString output = process.readAllStandardOutput();
    QStringList lines = output.split('\n');

    // Get our own PID to filter ourselves out
    qint64 ourPid = QCoreApplication::applicationPid();

    // lsof output format:
    // COMMAND   PID USER   FD   TYPE DEVICE SIZE/OFF NODE NAME
    // chrome  12345 user   42u   CHR  81,0      0t0  123 /dev/video0

    for (int i = 1; i < lines.size(); ++i) {  // Skip header line
        QString line = lines[i].trimmed();
        if (line.isEmpty()) continue;

        QStringList parts = line.split(QRegularExpression("\\s+"));
        if (parts.size() >= 2) {
            QString command = parts[0];
            QString pidStr = parts[1];
            qint64 pid = pidStr.toLongLong();

            // Skip our own process - we already have the camera for control
            if (pid == ourPid) {
                continue;
            }

            return QString("%1 (PID: %2)").arg(command).arg(pidStr);
        }
    }

    return QString();  // Not found (or only we're using it)
}

void MainWindow::setupTrayIcon()
{
    // Create system tray icon
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(QIcon(":/icons/camera.svg"));
    m_trayIcon->setToolTip("OBSBOT Control");

    // Create context menu
    m_trayMenu = new QMenu(this);
    QAction *showHideAction = m_trayMenu->addAction("Show/Hide");
    m_trayMenu->addSeparator();
    QAction *quitAction = m_trayMenu->addAction("Quit");

    connect(showHideAction, &QAction::triggered, this, &MainWindow::onShowHideAction);
    connect(quitAction, &QAction::triggered, this, &MainWindow::onQuitAction);

    m_trayIcon->setContextMenu(m_trayMenu);

    // Connect activation (click) signal
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);

    // Show the tray icon
    m_trayIcon->show();
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        // Single click - toggle visibility
        onShowHideAction();
    }
}

void MainWindow::onShowHideAction()
{
    if (isVisible()) {
        // Save preview state
        m_previewStateBeforeMinimize = m_previewWidget->isPreviewEnabled();

        // Disable preview if it's on
        if (m_previewStateBeforeMinimize) {
            m_previewToggleButton->setChecked(false);
        }

        // Disconnect from camera to free resources
        m_controller->disconnectFromCamera();

        hide();
    } else {
        // ALWAYS reconnect to camera when restoring from tray
        m_controller->connectToCamera();

        // ONLY restore preview if it was enabled before hiding
        if (m_previewStateBeforeMinimize) {
            QTimer::singleShot(1000, this, [this]() {
                m_previewToggleButton->setChecked(true);
            });
        }

        show();
        activateWindow();
        raise();
    }
}

void MainWindow::onQuitAction()
{
    // Save config before quitting
    if (m_controller->isConnected()) {
        m_controller->saveConfig();
    }
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Check if we should minimize to tray or quit
    auto settings = m_controller->getConfig().getSettings();
    std::cout << "[MainWindow] closeEvent: startMinimized = " << settings.startMinimized << std::endl;

    if (settings.startMinimized && m_trayIcon && m_trayIcon->isVisible()) {
        std::cout << "[MainWindow] closeEvent: Minimizing to tray" << std::endl;
        // Minimize to tray instead of closing
        // Save preview state
        m_previewStateBeforeMinimize = m_previewWidget->isPreviewEnabled();

        // Disable preview if it's on
        if (m_previewStateBeforeMinimize) {
            m_previewToggleButton->setChecked(false);
        }

        // Disconnect from camera to free resources
        m_controller->disconnectFromCamera();

        hide();
        event->ignore();

        // Show notification on first minimize
        static bool firstTime = true;
        if (firstTime) {
            m_trayIcon->showMessage(
                "OBSBOT Control",
                "Application minimized to system tray. Click the tray icon to restore.",
                QSystemTrayIcon::Information,
                3000
            );
            firstTime = false;
        }
    } else {
        std::cout << "[MainWindow] closeEvent: Quitting application" << std::endl;
        // Actually quit the application
        // Save config before quitting
        if (m_controller->isConnected()) {
            m_controller->saveConfig();
        }
        if (m_previewWidget->isPreviewEnabled()) {
            m_previewToggleButton->setChecked(false);
        }
        event->accept();
        QApplication::quit();
    }
}

void MainWindow::onStartMinimizedToggled(bool checked)
{
    // Update config and save
    std::cout << "[MainWindow] Checkbox toggled to: " << checked << std::endl;
    auto settings = m_controller->getConfig().getSettings();
    std::cout << "[MainWindow] Before: startMinimized = " << settings.startMinimized << std::endl;
    settings.startMinimized = checked;
    m_controller->getConfig().setSettings(settings);
    std::cout << "[MainWindow] Calling saveConfig()..." << std::endl;
    bool saved = m_controller->saveConfig();
    std::cout << "[MainWindow] saveConfig() returned: " << saved << std::endl;
}

void MainWindow::onVirtualCameraToggled(bool enabled)
{
    m_virtualCameraErrorNotified = false;

    auto settings = m_controller->getConfig().getSettings();
    settings.virtualCameraEnabled = enabled;
    m_controller->getConfig().setSettings(settings);
    m_controller->saveConfig();

    updateVirtualCameraStreamerState();

    if (enabled) {
        if (!m_virtualCameraAvailable) {
            QMessageBox::information(this,
                tr("Virtual Camera Not Available"),
                tr("v4l2loopback is not active yet. See the status message below for setup instructions."));
        } else if (!m_previewWidget->isPreviewEnabled()) {
            QMessageBox::information(this,
                tr("Virtual Camera Ready"),
                tr("The virtual camera will start streaming once the live preview is enabled."));
        }
    }
}

void MainWindow::onVirtualCameraDeviceEdited()
{
    if (!m_virtualCameraDeviceEdit) {
        return;
    }

    QString path = m_virtualCameraDeviceEdit->text().trimmed();
    if (path.isEmpty()) {
        path = QStringLiteral("/dev/video42");
        m_virtualCameraDeviceEdit->setText(path);
    }

    auto settings = m_controller->getConfig().getSettings();
    settings.virtualCameraDevice = path.toStdString();
    m_controller->getConfig().setSettings(settings);
    m_controller->saveConfig();

    m_virtualCameraErrorNotified = false;
    updateVirtualCameraStreamerState();
}

void MainWindow::onVirtualCameraResolutionChanged(int index)
{
    if (!m_virtualCameraResolutionCombo || index < 0) {
        return;
    }

    const QString key = m_virtualCameraResolutionCombo->itemData(index).toString();

    auto settings = m_controller->getConfig().getSettings();
    const QString currentKey = QString::fromStdString(settings.virtualCameraResolution);
    if (key == currentKey) {
        updateVirtualCameraStreamerState();
        return;
    }

    settings.virtualCameraResolution = key.isEmpty() ? std::string("match") : key.toStdString();
    m_controller->getConfig().setSettings(settings);
    m_controller->saveConfig();

    m_virtualCameraErrorNotified = false;
    updateVirtualCameraStreamerState();
}

void MainWindow::onVirtualCameraError(const QString &message)
{
    if (m_virtualCameraErrorNotified) {
        return;
    }
    m_virtualCameraErrorNotified = true;

    const QString detailedMessage = tr("Failed to publish frames to the virtual camera.\n\n%1\n\n" \
        "Ensure the v4l2loopback module is loaded and the device path is writable.")
        .arg(message);

    QMessageBox::warning(this, tr("Virtual Camera Error"), detailedMessage);

    if (m_virtualCameraCheckbox) {
        m_virtualCameraCheckbox->blockSignals(true);
        m_virtualCameraCheckbox->setChecked(false);
        m_virtualCameraCheckbox->blockSignals(false);
    }

    auto settings = m_controller->getConfig().getSettings();
    settings.virtualCameraEnabled = false;
    m_controller->getConfig().setSettings(settings);
    m_controller->saveConfig();

    updateVirtualCameraStreamerState();
}

void MainWindow::onVideoEffectsChanged(const FilterPreviewWidget::VideoEffectsSettings &settings)
{
    if (!m_previewWidget) {
        return;
    }
    m_previewWidget->setVideoEffects(settings);
}
