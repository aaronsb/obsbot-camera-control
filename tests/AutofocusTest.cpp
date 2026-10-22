#include "CameraController.h"
#include "TrackingControlWidget.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDebug>

static void require(bool condition, const char *message)
{
    if (!condition) {
        qFatal("%s", message);
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir configDir;
    require(configDir.isValid(), "Cannot create temporary config directory");
    qputenv("XDG_CONFIG_HOME", configDir.path().toUtf8());

    CameraController controller;
    require(controller.getCurrentState().autoFocusEnabled, "Controller must default to autofocus");
    require(controller.getConfig().getSettings().focus == -1, "Config must default to autofocus");

    TrackingControlWidget widget(&controller);
    auto *autoFocus = widget.findChild<QCheckBox *>("autoFocusCheckBox");
    auto *manualFocus = widget.findChild<QSlider *>("manualFocusSlider");
    require(autoFocus && manualFocus, "Focus controls must exist");
    require(widget.isAutoFocusEnabled(), "Autofocus checkbox must default to on");
    require(!manualFocus->isEnabled(), "Manual focus must be disabled in autofocus mode");

    int stateChanges = 0;
    int commandFailures = 0;
    QObject::connect(&controller, &CameraController::stateChanged,
                     [&stateChanges]() { ++stateChanges; });
    QObject::connect(&controller, &CameraController::commandFailed,
                     [&commandFailures]() { ++commandFailures; });

    for (int focus : {-1, 0, 45, 100}) {
        auto settings = controller.getConfig().getSettings();
        settings.focus = focus;
        controller.getConfig().setSettings(settings);
        require(controller.getConfig().save(), "Cannot save focus setting");
        Config reloaded;
        std::vector<Config::ValidationError> errors;
        require(reloaded.load(errors), "Cannot load saved focus setting");
        require(reloaded.getSettings().focus == focus, "Focus setting must round-trip");

        widget.setFocusSettings(reloaded.getSettings().focus);
        require(widget.isAutoFocusEnabled() == (focus < 0), "Saved focus mode must be reflected in UI");
        require(manualFocus->isEnabled() == (focus >= 0), "Slider must follow saved focus mode");
        if (focus >= 0) {
            require(widget.manualFocusValue() == focus, "Saved manual focus must be reflected in slider");
        }
    }
    require(stateChanges == 0 && commandFailures == 0, "Loading UI values must not send camera commands");

    auto state = controller.getCurrentState();
    state.manualFocusValue = 37;
    for (bool tracking : {true, false}) {
        state.autoFramingEnabled = tracking;
        for (bool autofocus : {true, false}) {
            state.autoFocusEnabled = autofocus;
            widget.updateFromState(state);
            require(widget.isTrackingEnabled() == tracking, "Tracking state must synchronize");
            require(widget.isAutoFocusEnabled() == autofocus, "Focus mode must synchronize independently");
            require(autoFocus->isEnabled(), "Autofocus toggle must remain enabled during tracking");
            require(manualFocus->isEnabled() == !autofocus, "Only autofocus may disable manual focus");
            require(widget.manualFocusValue() == 37, "Camera focus position must synchronize");
        }
    }
    require(stateChanges == 0 && commandFailures == 0, "Camera state synchronization must not send commands");

    widget.setV4l2Mode(true);
    require(!autoFocus->isHidden() && !manualFocus->isHidden(), "Focus controls must remain available in V4L2 mode");

    widget.setFocusSettings(-1);
    require(widget.manualFocusValue() == 37, "Enabling autofocus must retain the manual slider value");
    autoFocus->setChecked(false);
    require(widget.isAutoFocusEnabled(), "A failed focus command must restore the previous mode");
    require(!manualFocus->isEnabled(), "A failed focus command must not enable manual focus");

    qInfo() << "Autofocus defaults, config persistence, independent controls and failure rollback passed";
    return 0;
}
