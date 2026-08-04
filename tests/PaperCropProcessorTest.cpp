#include "PaperCropProcessor.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPolygon>
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

int main()
{
    bool passed = true;
    PaperCropProcessor processor;

    QImage gradient(100, 100, QImage::Format_RGBA8888);
    for (int y = 0; y < gradient.height(); ++y) {
        for (int x = 0; x < gradient.width(); ++x) {
            gradient.setPixelColor(x, y, QColor(x * 255 / 99, y * 255 / 99, 0));
        }
    }

    PaperCropSettings manual;
    manual.mode = PaperCropMode::Manual;
    manual.left = 0.25f;
    const QImage manuallyCropped = processor.process(gradient, manual);
    passed &= check(manuallyCropped.size() == gradient.size(),
                    "manual crop preserves the output frame size");
    passed &= check(manuallyCropped.pixelColor(0, 50) == QColor(Qt::black),
                    "manual crop letterboxes mismatched aspect ratios");
    passed &= check(manuallyCropped.pixelColor(50, 50).red() > 130,
                    "manual crop removes the requested left margin");

    if (PaperCropProcessor::automaticDetectionAvailable()) {
        QImage document(640, 480, QImage::Format_RGBA8888);
        document.fill(Qt::black);
        QPainter painter(&document);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawPolygon(QPolygon({
            QPoint(120, 70),
            QPoint(530, 95),
            QPoint(575, 405),
            QPoint(75, 420)
        }));
        painter.end();

        PaperCropSettings automatic;
        automatic.mode = PaperCropMode::Automatic;
        const QImage automaticallyCropped = processor.process(document, automatic);
        passed &= check(processor.paperDetected(),
                        "automatic mode detects a high-contrast paper quadrilateral");
        passed &= check(automaticallyCropped.size() == document.size(),
                        "automatic crop preserves the output frame size");
        passed &= check(automaticallyCropped.pixelColor(320, 240).lightness() > 200,
                        "automatic crop centers the detected paper");
    }

    return passed ? 0 : 1;
}
