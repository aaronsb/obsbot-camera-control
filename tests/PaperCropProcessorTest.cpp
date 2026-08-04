#include "PaperCropProcessor.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <array>
#include <cmath>
#include <iostream>

namespace {

bool check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

QImage texturedPage(const QPolygon &polygon)
{
    QImage image(640, 480, QImage::Format_RGBA8888);
    image.fill(Qt::black);

    QPainter painter(&image);
    QPainterPath pagePath;
    pagePath.addPolygon(polygon);
    painter.setClipPath(pagePath);
    painter.fillRect(image.rect(), Qt::white);

    const std::array<QColor, 4> colors = {
        QColor(Qt::red), QColor(Qt::green),
        QColor(Qt::blue), QColor(Qt::yellow)
    };
    QPoint center;
    for (const QPoint &point : polygon) {
        center += point;
    }
    center /= 4;
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < 4; ++i) {
        const QPoint vector = polygon[i] - center;
        const QPoint marker = center + QPoint(
            vector.x() * 3 / 4, vector.y() * 3 / 4);
        painter.setBrush(colors[static_cast<size_t>(i)]);
        painter.drawEllipse(marker, 35, 35);
    }
    return image;
}

double sampledColorDifference(const QImage &left, const QImage &right)
{
    long long difference = 0;
    long long channels = 0;
    for (int y = 0; y < left.height(); y += 4) {
        for (int x = 0; x < left.width(); x += 4) {
            const QColor a = left.pixelColor(x, y);
            const QColor b = right.pixelColor(x, y);
            difference += std::abs(a.red() - b.red());
            difference += std::abs(a.green() - b.green());
            difference += std::abs(a.blue() - b.blue());
            channels += 3;
        }
    }
    return channels > 0
        ? static_cast<double>(difference) / static_cast<double>(channels)
        : 0.0;
}

bool containsApproximateColor(const QImage &image,
                              const QRect &region,
                              const QColor &target)
{
    const QRect bounded = region.intersected(image.rect());
    for (int y = bounded.top(); y <= bounded.bottom(); y += 2) {
        for (int x = bounded.left(); x <= bounded.right(); x += 2) {
            const QColor color = image.pixelColor(x, y);
            if (std::abs(color.red() - target.red()) < 80
                && std::abs(color.green() - target.green()) < 80
                && std::abs(color.blue() - target.blue()) < 80) {
                return true;
            }
        }
    }
    return false;
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

        QImage occludedDocument = document;
        QPainter occlusionPainter(&occludedDocument);
        occlusionPainter.setPen(Qt::NoPen);
        occlusionPainter.setBrush(Qt::black);
        occlusionPainter.drawRect(QRect(285, 40, 70, 125));
        occlusionPainter.end();

        PaperCropProcessor occlusionProcessor;
        const QImage occlusionCrop = occlusionProcessor.process(occludedDocument, automatic);
        passed &= check(occlusionProcessor.paperDetected(),
                        "automatic mode tolerates a bounded hand-like edge occlusion");
        passed &= check(occlusionCrop.size() == occludedDocument.size(),
                        "occlusion-tolerant crop preserves the output frame size");
        passed &= check(occlusionCrop.pixelColor(320, 240).lightness() > 200,
                        "occlusion recovery keeps the paper centered");

        QImage diamond(640, 480, QImage::Format_RGBA8888);
        diamond.fill(Qt::black);
        QPainter diamondPainter(&diamond);
        diamondPainter.setPen(Qt::NoPen);
        diamondPainter.setBrush(Qt::white);
        diamondPainter.drawPolygon(QPolygon({
            QPoint(320, 45), QPoint(555, 240),
            QPoint(320, 435), QPoint(85, 240)
        }));
        diamondPainter.end();
        PaperCropProcessor diamondProcessor;
        diamondProcessor.process(diamond, automatic);
        passed &= check(diamondProcessor.paperDetected(),
                        "automatic mode detects a symmetrically rotated page");

        const QPolygon beforeCornerTransition({
            QPoint(320, 50), QPoint(560, 230),
            QPoint(330, 430), QPoint(90, 250)
        });
        const QPolygon afterCornerTransition({
            QPoint(290, 45), QPoint(560, 230),
            QPoint(330, 430), QPoint(90, 250)
        });
        PaperCropProcessor continuityProcessor;
        const QImage beforeCrop = continuityProcessor.process(
            texturedPage(beforeCornerTransition), automatic);
        QImage afterCrop;
        for (int frame = 0; frame < 10; ++frame) {
            afterCrop = continuityProcessor.process(
                texturedPage(afterCornerTransition), automatic);
        }
        passed &= check(continuityProcessor.paperDetected(),
                        "rotating textured page remains detected across corner transition");
        passed &= check(sampledColorDifference(beforeCrop, afterCrop) < 30.0,
                        "cyclic corner alignment prevents a 90-degree crop jump");

        QImage concaveDistractor(640, 480, QImage::Format_RGBA8888);
        concaveDistractor.fill(Qt::black);
        QPainter distractorPainter(&concaveDistractor);
        distractorPainter.setPen(Qt::NoPen);
        distractorPainter.setBrush(Qt::white);
        distractorPainter.drawPolygon(QPolygon({
            QPoint(80, 50), QPoint(560, 50), QPoint(560, 430),
            QPoint(420, 430), QPoint(420, 130), QPoint(220, 130),
            QPoint(220, 430), QPoint(80, 430)
        }));
        distractorPainter.end();
        PaperCropProcessor distractorProcessor;
        distractorProcessor.process(concaveDistractor, automatic);
        passed &= check(!distractorProcessor.paperDetected(),
                        "large concave non-document is not promoted by its convex hull");

        QImage borderClipped(640, 480, QImage::Format_RGBA8888);
        borderClipped.fill(Qt::black);
        QPainter borderPainter(&borderClipped);
        borderPainter.setPen(Qt::NoPen);
        borderPainter.setBrush(Qt::white);
        borderPainter.drawPolygon(QPolygon({
            QPoint(100, 60), QPoint(540, 80),
            QPoint(610, 479), QPoint(20, 479)
        }));
        borderPainter.end();
        PaperCropProcessor borderProcessor;
        borderProcessor.process(borderClipped, automatic);
        passed &= check(!borderProcessor.paperDetected(),
                        "automatic mode rejects a page whose corners leave the frame");

        const QPolygon fullHeightPage({
            QPoint(190, 0), QPoint(430, 0),
            QPoint(500, 479), QPoint(140, 479)
        });
        QImage clippedPrintedPage(640, 480, QImage::Format_RGBA8888);
        clippedPrintedPage.fill(Qt::black);
        QPainter clippedPainter(&clippedPrintedPage);
        QPainterPath clippedPath;
        clippedPath.addPolygon(fullHeightPage);
        clippedPainter.setClipPath(clippedPath);
        clippedPainter.fillRect(clippedPrintedPage.rect(), Qt::white);
        clippedPainter.setPen(QPen(Qt::black, 3));
        for (int y = 70; y < 440; y += 24) {
            clippedPainter.drawLine(225, y, 430, y);
        }
        clippedPainter.setPen(Qt::NoPen);
        clippedPainter.setBrush(Qt::red);
        clippedPainter.drawEllipse(QPoint(215, 45), 18, 18);
        clippedPainter.setBrush(Qt::green);
        clippedPainter.drawEllipse(QPoint(405, 45), 18, 18);
        clippedPainter.setBrush(Qt::blue);
        clippedPainter.drawEllipse(QPoint(455, 430), 18, 18);
        clippedPainter.setBrush(Qt::yellow);
        clippedPainter.drawEllipse(QPoint(175, 430), 18, 18);
        clippedPainter.setPen(Qt::NoPen);
        clippedPainter.setBrush(Qt::black);
        clippedPainter.drawEllipse(QPoint(170, 205), 32, 40);
        clippedPainter.drawEllipse(QPoint(470, 300), 34, 42);
        clippedPainter.end();

        PaperCropProcessor clippedPrintedProcessor;
        clippedPrintedProcessor.process(clippedPrintedPage, automatic);
        passed &= check(!clippedPrintedProcessor.paperDetected(),
                        "one line-only candidate does not acquire a crop lock");
        clippedPrintedProcessor.process(clippedPrintedPage, automatic);
        passed &= check(!clippedPrintedProcessor.paperDetected(),
                        "two line-only candidates remain below acquisition hysteresis");
        const QImage clippedPrintedCrop =
            clippedPrintedProcessor.process(clippedPrintedPage, automatic);
        passed &= check(clippedPrintedProcessor.paperDetected(),
                        "three consistent line recoveries lock a frame-clipped printed page");
        passed &= check(clippedPrintedCrop.size() == clippedPrintedPage.size(),
                        "frame-clipped paper recovery preserves output size");
        passed &= check(containsApproximateColor(
                            clippedPrintedCrop, QRect(0, 0, 320, 240), Qt::red)
                        && containsApproximateColor(
                            clippedPrintedCrop, QRect(320, 0, 320, 240), Qt::green)
                        && containsApproximateColor(
                            clippedPrintedCrop, QRect(320, 240, 320, 240), Qt::blue)
                        && containsApproximateColor(
                            clippedPrintedCrop, QRect(0, 240, 320, 240), Qt::yellow),
                        "frame-clipped warp retains all four page-corner landmarks");

        QImage shiftedPrintedPage(640, 480, QImage::Format_RGBA8888);
        shiftedPrintedPage.fill(Qt::black);
        QPainter shiftPainter(&shiftedPrintedPage);
        shiftPainter.drawImage(110, 0, clippedPrintedPage);
        shiftPainter.end();

        PaperCropProcessor relocationProcessor;
        for (int frame = 0; frame < 3; ++frame) {
            relocationProcessor.process(clippedPrintedPage, automatic);
        }
        QImage beforeScheduledRelocation;
        for (int frame = 1; frame <= 7; ++frame) {
            beforeScheduledRelocation =
                relocationProcessor.process(shiftedPrintedPage, automatic);
        }
        const QImage rejectedRelocation =
            relocationProcessor.process(shiftedPrintedPage, automatic);
        passed &= check(relocationProcessor.paperDetected()
                        && rejectedRelocation == beforeScheduledRelocation,
                        "abrupt valid replacement keeps the retained crop unchanged");

        for (int frame = 9; frame <= 30; ++frame) {
            relocationProcessor.process(shiftedPrintedPage, automatic);
        }
        passed &= check(relocationProcessor.paperDetected(),
                        "abrupt replacement holds the prior crop for 30 full frames");
        relocationProcessor.process(shiftedPrintedPage, automatic);
        passed &= check(!relocationProcessor.paperDetected(),
                        "stale crop expires after persistent abrupt relocation");

        relocationProcessor.process(shiftedPrintedPage, automatic);
        relocationProcessor.process(shiftedPrintedPage, automatic);
        const QImage reacquiredRelocation =
            relocationProcessor.process(shiftedPrintedPage, automatic);
        passed &= check(relocationProcessor.paperDetected(),
                        "relocated page reacquires after three fresh consistent detections");
        passed &= check(containsApproximateColor(
                            reacquiredRelocation, QRect(0, 0, 320, 240), Qt::red)
                        && containsApproximateColor(
                            reacquiredRelocation, QRect(320, 0, 320, 240), Qt::green)
                        && containsApproximateColor(
                            reacquiredRelocation, QRect(320, 240, 320, 240), Qt::blue)
                        && containsApproximateColor(
                            reacquiredRelocation, QRect(0, 240, 320, 240), Qt::yellow),
                        "relocated crop retains all four oriented landmarks");

        QImage plainClippedPanel(640, 480, QImage::Format_RGBA8888);
        plainClippedPanel.fill(Qt::black);
        QPainter panelPainter(&plainClippedPanel);
        panelPainter.setPen(Qt::NoPen);
        panelPainter.setBrush(Qt::white);
        panelPainter.drawPolygon(fullHeightPage);
        panelPainter.end();
        PaperCropProcessor panelProcessor;
        panelProcessor.process(plainClippedPanel, automatic);
        passed &= check(!panelProcessor.paperDetected(),
                        "line recovery rejects an untextured frame-clipped panel");

        QImage texturedDisplay(640, 480, QImage::Format_RGBA8888);
        texturedDisplay.fill(Qt::black);
        const QPolygon displayBezel({
            QPoint(170, 0), QPoint(450, 0),
            QPoint(520, 479), QPoint(120, 479)
        });
        const QPolygon displaySurface({
            QPoint(195, 0), QPoint(425, 0),
            QPoint(485, 479), QPoint(155, 479)
        });
        QPainter displayPainter(&texturedDisplay);
        displayPainter.setPen(Qt::NoPen);
        displayPainter.setBrush(QColor(45, 45, 45));
        displayPainter.drawPolygon(displayBezel);
        QPainterPath displayPath;
        displayPath.addPolygon(displaySurface);
        displayPainter.setClipPath(displayPath);
        displayPainter.fillRect(texturedDisplay.rect(), QColor(230, 230, 230));
        displayPainter.setPen(QPen(QColor(70, 70, 70), 3));
        for (int y = 50; y < 460; y += 25) {
            displayPainter.drawLine(190, y, 460, y);
        }
        for (int x = 230; x < 430; x += 50) {
            displayPainter.drawLine(x, 20, x, 460);
        }
        displayPainter.end();

        PaperCropProcessor displayProcessor;
        for (int frame = 0; frame < 3; ++frame) {
            displayProcessor.process(texturedDisplay, automatic);
        }
        passed &= check(!displayProcessor.paperDetected(),
                        "line recovery rejects a textured display inside a dark bezel");

        QImage wideRectangle(640, 480, QImage::Format_RGBA8888);
        wideRectangle.fill(Qt::black);
        QPainter widePainter(&wideRectangle);
        widePainter.fillRect(QRect(50, 175, 540, 130), Qt::white);
        widePainter.end();
        PaperCropProcessor wideProcessor;
        wideProcessor.process(wideRectangle, automatic);
        passed &= check(!wideProcessor.paperDetected(),
                        "automatic mode rejects implausibly wide rectangular distractors");

        PaperCropSettings automaticWithFallback = automatic;
        automaticWithFallback.left = 0.25f;
        PaperCropProcessor retentionProcessor;
        retentionProcessor.process(document, automaticWithFallback);
        passed &= check(retentionProcessor.paperDetected(),
                        "retention test starts from a confirmed paper lock");

        QImage fallbackFrame(640, 480, QImage::Format_RGBA8888);
        fallbackFrame.fill(Qt::red);
        for (int frame = 1; frame <= 30; ++frame) {
            retentionProcessor.process(fallbackFrame, automaticWithFallback);
            passed &= check(retentionProcessor.paperDetected(),
                            "last good paper boundary is retained for 30 full frames");
        }
        const QImage fallbackOutput =
            retentionProcessor.process(fallbackFrame, automaticWithFallback);
        passed &= check(!retentionProcessor.paperDetected(),
                        "paper lock expires on the 31st missed frame");

        PaperCropSettings manualFallback = automaticWithFallback;
        manualFallback.mode = PaperCropMode::Manual;
        PaperCropProcessor manualFallbackProcessor;
        const QImage expectedFallback =
            manualFallbackProcessor.process(fallbackFrame, manualFallback);
        passed &= check(fallbackOutput == expectedFallback,
                        "expired automatic lock uses the saved manual rectangle");

        retentionProcessor.reset();
        passed &= check(!retentionProcessor.paperDetected(),
                        "explicit reset clears a retained paper boundary");
    } else {
        PaperCropSettings automaticFallback;
        automaticFallback.mode = PaperCropMode::Automatic;
        automaticFallback.left = 0.25f;
        const QImage automaticOutput = processor.process(gradient, automaticFallback);

        PaperCropSettings manualFallback = automaticFallback;
        manualFallback.mode = PaperCropMode::Manual;
        PaperCropProcessor manualProcessor;
        const QImage manualOutput = manualProcessor.process(gradient, manualFallback);
        passed &= check(automaticOutput == manualOutput,
                        "no-OpenCV automatic mode uses the manual fallback");
        passed &= check(!processor.paperDetected(),
                        "no-OpenCV fallback never reports an automatic lock");
    }

    return passed ? 0 : 1;
}
