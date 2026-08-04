#include "VideoEffectsWidget.h"

#include <QApplication>
#include <QComboBox>
#include <iostream>

namespace {

bool check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication application(argc, argv);
    bool passed = true;

    VideoEffectsWidget widget;
    int resetRequests = 0;
    int effectChanges = 0;
    int cropIntentChanges = 0;
    QObject::connect(&widget, &VideoEffectsWidget::paperDetectionResetRequested,
                     [&resetRequests]() { ++resetRequests; });
    QObject::connect(&widget, &VideoEffectsWidget::effectsChanged,
                     [&effectChanges](const auto &) { ++effectChanges; });
    QObject::connect(&widget, &VideoEffectsWidget::paperCropIntentEdited,
                     [&cropIntentChanges](const auto &) {
                         ++cropIntentChanges;
                     });

    PaperCropSettings sceneCrop;
    sceneCrop.mode = PaperCropMode::Automatic;
    sceneCrop.left = 0.1f;
    sceneCrop.top = 0.05f;
    sceneCrop.right = 0.08f;
    sceneCrop.bottom = 0.12f;

    widget.applyPaperCropForScene(sceneCrop);
    widget.applyPaperCropForScene(sceneCrop);

    passed &= check(resetRequests == 2,
                    "identical scene recalls each reset the old paper lock");
    passed &= check(effectChanges == 2,
                    "identical scene recalls each publish their crop settings");
    passed &= check(cropIntentChanges == 0,
                    "programmatic scene crops never impersonate user intent");
    passed &= check(widget.settings().paperCrop == sceneCrop,
                    "scene recall preserves the requested crop settings");

    auto *modeCombo = widget.findChild<QComboBox *>(
        QStringLiteral("paperCropModeCombo"));
    passed &= check(modeCombo != nullptr,
                    "paper crop mode exposes a stable test identity");
    if (modeCombo) {
        modeCombo->setCurrentIndex(modeCombo->findData(
            static_cast<int>(PaperCropMode::Off)));
        modeCombo->setCurrentIndex(modeCombo->findData(
            static_cast<int>(PaperCropMode::Automatic)));
        passed &= check(cropIntentChanges == 2,
                        "each user crop-mode edit emits one intent");
    }

    return passed ? 0 : 1;
}
