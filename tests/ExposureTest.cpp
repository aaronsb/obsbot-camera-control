#include "CameraController.h"
#include "CameraSettingsWidget.h"
#include "ShutterSpeed.h"
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QDebug>
#include <cstdlib>

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

    // V4L2 exposure (100 µs units) the SDK set on a Meet 2 for each shutter index. The SDK is
    // inconsistent for the fastest speeds (1/4000s becomes 100 µs), so 9-12 aren't used as reference.
    const struct { int index; int v4l2; } measured[] = {
        {20, 16}, {25, 50}, {28, 100}, {30, 167}, {31, 200}, {33, 330},
        {36, 667}, {37, 800}, {40, 1600}, {42, 2500}
    };
    for (const auto &m : measured) {
        require(ShutterSpeed::fromV4l2Exposure(m.v4l2) == m.index, "V4L2 exposure must map to the SDK's shutter index");
        require(std::abs(ShutterSpeed::toV4l2Exposure(m.index) - m.v4l2) * 100 <= m.v4l2,
                "Shutter index must map to within 1% of the SDK's V4L2 exposure");
    }
    for (int index = ShutterSpeed::kFirstIndex; index <= ShutterSpeed::kLastIndex; ++index) {
        const int v4l2 = ShutterSpeed::toV4l2Exposure(index);
        require(ShutterSpeed::toV4l2Exposure(ShutterSpeed::fromV4l2Exposure(v4l2)) == v4l2,
                "Every V4L2 exposure the app writes must read back as a speed with the same exposure");
    }
    // 1/6400s and 1/5000s share the 200 µs step
    require(ShutterSpeed::fromV4l2Exposure(2, 10) == 10 && ShutterSpeed::fromV4l2Exposure(2, 11) == 11,
            "A shared V4L2 step must keep the selected shutter speed");
    require(ShutterSpeed::fromV4l2Exposure(330, 30) == 33, "A changed V4L2 exposure must be picked up");
    require(ShutterSpeed::label(33) == "1/30s" && ShutterSpeed::label(37) == "1/12.5s",
            "Shutter labels must not depend on the locale");

    CameraController controller;
    require(controller.getConfig().getSettings().exposure == ShutterSpeed::kDefaultIndex,
            "Config must default to 1/30s");
    auto settings = controller.getConfig().getSettings();
    settings.exposure = 42;
    controller.getConfig().setSettings(settings);
    require(controller.getConfig().save(), "Cannot save exposure setting");
    {
        Config reloaded;
        std::vector<Config::ValidationError> errors;
        require(reloaded.load(errors) && reloaded.getSettings().exposure == 42, "Exposure must round-trip");
    }

    // App settings (e.g. the virtual camera frame rate) are saved through saveConfig(). Without a
    // camera that must not replace the saved camera settings with the controller's defaults.
    settings = controller.getConfig().getSettings();
    settings.exposureAuto = false;
    settings.iso = 10;
    settings.focus = 45;
    settings.virtualCameraMaxFps = 30;
    controller.getConfig().setSettings(settings);
    require(controller.saveConfig(), "Cannot save config without a camera");
    {
        Config reloaded;
        std::vector<Config::ValidationError> errors;
        require(reloaded.load(errors), "Cannot load config saved without a camera");
        const auto saved = reloaded.getSettings();
        require(!saved.exposureAuto && saved.exposure == 42 && saved.iso == 10 && saved.focus == 45,
                "Saving without a camera must keep the saved camera settings");
        require(saved.virtualCameraMaxFps == 30, "Saving without a camera must store app settings");
    }

    // A raw V4L2 exposure value is not a shutter speed index
    const QString configPath = QString::fromStdString(controller.getConfig().getConfigPath());
    QFile file(configPath);
    require(file.open(QIODevice::ReadOnly), "Cannot read saved config");
    QByteArray contents = file.readAll();
    file.close();
    contents.replace("exposure=42", "exposure=330");
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size(),
            "Cannot write config");
    file.close();
    {
        Config reloaded;
        std::vector<Config::ValidationError> errors;
        require(!reloaded.load(errors), "Out-of-range exposure must be rejected");
    }

    int commandFailures = 0;
    QObject::connect(&controller, &CameraController::commandFailed,
                     [&commandFailures]() { ++commandFailures; });
    require(!controller.setExposure(30) && !controller.setISO(10) && commandFailures == 0,
            "Commands without a camera must be skipped silently");

    CameraSettingsWidget widget(&controller);
    auto *exposureLabel = widget.findChild<QLabel *>("exposureValueLabel");
    auto *isoSlider = widget.findChild<QSlider *>("isoSlider");
    require(exposureLabel && isoSlider, "Exposure and ISO controls must exist");
    require(widget.getExposure() == ShutterSpeed::kDefaultIndex && exposureLabel->text() == "1/30s",
            "Exposure slider must default to 1/30s");
    widget.setExposure(42);
    require(exposureLabel->text() == "1/4s", "Exposure label must show the shutter speed");
    widget.updateFromState(controller.getCurrentState());
    require(!isoSlider->isHidden(), "ISO stays available until a connected camera reports no ISO range");
    require(commandFailures == 0, "Updating the UI must not send camera commands");

    qInfo() << "Shutter speed conversion, exposure persistence and ISO guards passed";
    return 0;
}
