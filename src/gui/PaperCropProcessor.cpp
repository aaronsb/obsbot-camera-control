#include "PaperCropProcessor.h"

#include <QPainter>
#include <QRect>
#include <QtGlobal>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#ifdef OBSBOT_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace {

QImage fitInsideFrame(const QImage &image, const QSize &frameSize)
{
    if (image.isNull() || !frameSize.isValid()) {
        return image;
    }

    const QImage scaled = image.scaled(frameSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage output(frameSize, QImage::Format_RGBA8888);
    output.fill(Qt::black);

    QPainter painter(&output);
    painter.drawImage((frameSize.width() - scaled.width()) / 2,
                      (frameSize.height() - scaled.height()) / 2,
                      scaled);
    return output;
}

QImage applyManualCrop(const QImage &image, const PaperCropSettings &settings)
{
    const PaperCropSettings crop = settings.normalized();
    const int left = qRound(crop.left * image.width());
    const int top = qRound(crop.top * image.height());
    const int right = qRound(crop.right * image.width());
    const int bottom = qRound(crop.bottom * image.height());
    const QRect rect(left,
                     top,
                     std::max(1, image.width() - left - right),
                     std::max(1, image.height() - top - bottom));
    return fitInsideFrame(image.copy(rect.intersected(image.rect())), image.size());
}

#ifdef OBSBOT_HAS_OPENCV

using Quad = std::array<cv::Point2f, 4>;

float pointDistance(const cv::Point2f &a, const cv::Point2f &b)
{
    const cv::Point2f delta = a - b;
    return std::sqrt(delta.x * delta.x + delta.y * delta.y);
}

Quad orderQuad(const std::vector<cv::Point> &points)
{
    Quad ordered{};
    auto minSum = points.front();
    auto maxSum = points.front();
    auto minDiff = points.front();
    auto maxDiff = points.front();

    for (const cv::Point &point : points) {
        if (point.x + point.y < minSum.x + minSum.y) minSum = point;
        if (point.x + point.y > maxSum.x + maxSum.y) maxSum = point;
        if (point.x - point.y < minDiff.x - minDiff.y) minDiff = point;
        if (point.x - point.y > maxDiff.x - maxDiff.y) maxDiff = point;
    }

    ordered[0] = cv::Point2f(minSum);  // top-left
    ordered[1] = cv::Point2f(maxDiff); // top-right
    ordered[2] = cv::Point2f(maxSum);  // bottom-right
    ordered[3] = cv::Point2f(minDiff); // bottom-left
    return ordered;
}

bool isDocumentLike(const Quad &quad, const cv::Size &size)
{
    const float minEdge = std::min(size.width, size.height) * 0.12f;
    for (int i = 0; i < 4; ++i) {
        if (pointDistance(quad[i], quad[(i + 1) % 4]) < minEdge) {
            return false;
        }
    }

    std::vector<cv::Point2f> polygon(quad.begin(), quad.end());
    const double area = std::abs(cv::contourArea(polygon));
    const double frameArea = static_cast<double>(size.width) * size.height;
    return area >= frameArea * 0.08 && area <= frameArea * 0.96;
}

bool detectPaperQuad(const QImage &image, Quad &normalizedQuad)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    cv::Mat rgbaMat(rgba.height(), rgba.width(), CV_8UC4,
                    const_cast<uchar *>(rgba.constBits()), rgba.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(rgbaMat, bgr, cv::COLOR_RGBA2BGR);

    constexpr int maxDetectionDimension = 640;
    const double scale = std::min(1.0, maxDetectionDimension /
        static_cast<double>(std::max(bgr.cols, bgr.rows)));
    cv::Mat detectionImage;
    if (scale < 1.0) {
        cv::resize(bgr, detectionImage, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        detectionImage = bgr;
    }

    cv::Mat gray;
    cv::cvtColor(detectionImage, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0.0);

    cv::Mat edges;
    cv::Canny(gray, edges, 50.0, 150.0);
    cv::dilate(edges, edges, cv::Mat(), cv::Point(-1, -1), 1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    std::sort(contours.begin(), contours.end(), [](const auto &a, const auto &b) {
        return std::abs(cv::contourArea(a)) > std::abs(cv::contourArea(b));
    });

    for (const auto &contour : contours) {
        const double perimeter = cv::arcLength(contour, true);
        std::vector<cv::Point> approximate;
        cv::approxPolyDP(contour, approximate, perimeter * 0.02, true);
        if (approximate.size() != 4 || !cv::isContourConvex(approximate)) {
            continue;
        }

        Quad ordered = orderQuad(approximate);
        if (!isDocumentLike(ordered, detectionImage.size())) {
            continue;
        }

        for (cv::Point2f &point : ordered) {
            point.x /= detectionImage.cols;
            point.y /= detectionImage.rows;
        }
        normalizedQuad = ordered;
        return true;
    }

    return false;
}

QImage warpPaper(const QImage &image, const Quad &normalizedQuad)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    cv::Mat rgbaMat(rgba.height(), rgba.width(), CV_8UC4,
                    const_cast<uchar *>(rgba.constBits()), rgba.bytesPerLine());

    Quad source = normalizedQuad;
    for (cv::Point2f &point : source) {
        point.x *= rgba.width();
        point.y *= rgba.height();
    }

    const int targetWidth = std::max(32, qRound(std::max(
        pointDistance(source[0], source[1]), pointDistance(source[3], source[2]))));
    const int targetHeight = std::max(32, qRound(std::max(
        pointDistance(source[0], source[3]), pointDistance(source[1], source[2]))));

    const Quad destination = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(targetWidth - 1), 0.0f),
        cv::Point2f(static_cast<float>(targetWidth - 1), static_cast<float>(targetHeight - 1)),
        cv::Point2f(0.0f, static_cast<float>(targetHeight - 1))
    };

    const cv::Mat transform = cv::getPerspectiveTransform(source.data(), destination.data());
    cv::Mat warped;
    cv::warpPerspective(rgbaMat, warped, transform, cv::Size(targetWidth, targetHeight),
                        cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 255));

    QImage result(warped.data, warped.cols, warped.rows,
                  static_cast<int>(warped.step), QImage::Format_RGBA8888);
    return fitInsideFrame(result.copy(), image.size());
}

#endif

} // namespace

struct PaperCropProcessor::Impl {
    int frameCounter = 0;
    int missedDetections = 0;
    bool detected = false;
#ifdef OBSBOT_HAS_OPENCV
    Quad normalizedQuad{};
#endif
};

PaperCropProcessor::PaperCropProcessor()
    : m_impl(std::make_unique<Impl>())
{
}

PaperCropProcessor::~PaperCropProcessor() = default;

QImage PaperCropProcessor::process(const QImage &image, const PaperCropSettings &settings)
{
    if (image.isNull() || settings.mode == PaperCropMode::Off) {
        m_impl->detected = false;
        return image;
    }

    if (settings.mode == PaperCropMode::Manual) {
        m_impl->detected = false;
        return applyManualCrop(image, settings);
    }

#ifdef OBSBOT_HAS_OPENCV
    constexpr int detectionInterval = 10;
    constexpr int retainedDetectionFrames = 30;
    if (m_impl->frameCounter++ % detectionInterval == 0 || !m_impl->detected) {
        Quad candidate{};
        if (detectPaperQuad(image, candidate)) {
            if (m_impl->detected) {
                constexpr float smoothing = 0.35f;
                for (int i = 0; i < 4; ++i) {
                    m_impl->normalizedQuad[i] =
                        m_impl->normalizedQuad[i] * (1.0f - smoothing) + candidate[i] * smoothing;
                }
            } else {
                m_impl->normalizedQuad = candidate;
            }
            m_impl->detected = true;
            m_impl->missedDetections = 0;
        } else if (++m_impl->missedDetections > retainedDetectionFrames / detectionInterval) {
            m_impl->detected = false;
        }
    }

    if (m_impl->detected) {
        return warpPaper(image, m_impl->normalizedQuad);
    }
#endif

    return applyManualCrop(image, settings);
}

bool PaperCropProcessor::paperDetected() const
{
    return m_impl->detected;
}

bool PaperCropProcessor::automaticDetectionAvailable()
{
#ifdef OBSBOT_HAS_OPENCV
    return true;
#else
    return false;
#endif
}
