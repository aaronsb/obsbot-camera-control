#include "PaperCropProcessor.h"

#include <QPainter>
#include <QRect>
#include <QtGlobal>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
    std::vector<cv::Point2f> cyclic;
    cyclic.reserve(points.size());
    cv::Point2f center(0.0f, 0.0f);
    for (const cv::Point &point : points) {
        cyclic.emplace_back(point);
        center += cv::Point2f(point);
    }
    center *= 1.0f / static_cast<float>(cyclic.size());

    std::sort(cyclic.begin(), cyclic.end(), [center](const auto &a, const auto &b) {
        return std::atan2(a.y - center.y, a.x - center.x)
            < std::atan2(b.y - center.y, b.x - center.x);
    });

    // Start at the top-left-like corner while retaining cyclic order. Unlike
    // sum/difference extrema, this never assigns one symmetric diamond corner
    // to two output positions.
    const auto first = std::min_element(cyclic.begin(), cyclic.end(), [](const auto &a, const auto &b) {
        const float aSum = a.x + a.y;
        const float bSum = b.x + b.y;
        return aSum == bSum ? a.y < b.y : aSum < bSum;
    });
    std::rotate(cyclic.begin(), first, cyclic.end());

    Quad ordered{};
    std::copy_n(cyclic.begin(), ordered.size(), ordered.begin());
    return ordered;
}

float squaredDistance(const cv::Point2f &a, const cv::Point2f &b)
{
    const cv::Point2f delta = a - b;
    return delta.x * delta.x + delta.y * delta.y;
}

Quad alignQuadToReference(const Quad &candidate, const Quad &reference)
{
    Quad aligned = candidate;
    float bestScore = std::numeric_limits<float>::max();
    for (int shift = 0; shift < 4; ++shift) {
        float score = 0.0f;
        for (int i = 0; i < 4; ++i) {
            score += squaredDistance(reference[i], candidate[(i + shift) % 4]);
        }
        if (score < bestScore) {
            bestScore = score;
            for (int i = 0; i < 4; ++i) {
                aligned[i] = candidate[(i + shift) % 4];
            }
        }
    }
    return aligned;
}

float maximumCornerDistance(const Quad &left, const Quad &right)
{
    float maximum = 0.0f;
    for (int index = 0; index < 4; ++index) {
        maximum = std::max(maximum,
            std::sqrt(squaredDistance(left[index], right[index])));
    }
    return maximum;
}

bool isDocumentLike(const Quad &quad, const cv::Size &size)
{
    const float minEdge = std::min(size.width, size.height) * 0.12f;
    const float borderX = std::max(2.0f, size.width * 0.01f);
    const float borderY = std::max(2.0f, size.height * 0.01f);
    for (int i = 0; i < 4; ++i) {
        const cv::Point2f &point = quad[i];
        if (point.x <= borderX || point.x >= size.width - borderX
            || point.y <= borderY || point.y >= size.height - borderY) {
            // A four-corner homography is not trustworthy when the page exits
            // the image. Keep searching and use the saved manual fallback.
            return false;
        }

        const cv::Point2f previous = quad[(i + 3) % 4] - point;
        const cv::Point2f next = quad[(i + 1) % 4] - point;
        const float previousLength = pointDistance(quad[(i + 3) % 4], point);
        const float nextLength = pointDistance(quad[(i + 1) % 4], point);
        if (previousLength < minEdge || nextLength < minEdge) {
            return false;
        }
        const float cosine = (previous.x * next.x + previous.y * next.y)
            / (previousLength * nextLength);
        if (std::abs(cosine) > 0.94f) {
            return false;
        }
    }

    const float width = std::max(
        pointDistance(quad[0], quad[1]), pointDistance(quad[3], quad[2]));
    const float height = std::max(
        pointDistance(quad[0], quad[3]), pointDistance(quad[1], quad[2]));
    const float aspectRatio = width / height;
    if (aspectRatio < 0.35f || aspectRatio > 2.85f) {
        return false;
    }

    std::vector<cv::Point2f> polygon(quad.begin(), quad.end());
    const double area = std::abs(cv::contourArea(polygon));
    const double frameArea = static_cast<double>(size.width) * size.height;
    return area >= frameArea * 0.08 && area <= frameArea * 0.96;
}

bool approximateDocumentQuad(const std::vector<cv::Point> &contour, Quad &quad)
{
    // A hand overlapping an edge commonly makes the visible page contour
    // concave. Its convex hull retains the outer page corners while removing
    // bounded inward finger occlusions.
    std::vector<cv::Point> hull;
    cv::convexHull(contour, hull);
    if (hull.size() < 4) {
        return false;
    }

    const double hullArea = std::abs(cv::contourArea(hull));
    const double contourArea = std::abs(cv::contourArea(contour));
    constexpr double minimumSolidity = 0.78;
    if (hullArea <= 0.0 || contourArea / hullArea < minimumSolidity) {
        return false;
    }

    const double perimeter = cv::arcLength(hull, true);
    constexpr std::array<double, 4> approximationRatios = {0.015, 0.02, 0.03, 0.04};
    for (const double ratio : approximationRatios) {
        std::vector<cv::Point> approximate;
        cv::approxPolyDP(hull, approximate, perimeter * ratio, true);
        if (approximate.size() == 4 && cv::isContourConvex(approximate)) {
            quad = orderQuad(approximate);
            return true;
        }
    }
    return false;
}

struct BoundaryEvidence {
    float topX = 0.0f;
    float bottomX = 0.0f;
    float firstSupportedFraction = 0.0f;
    float lastSupportedFraction = 1.0f;
    float contrast = 0.0f;
    float support = 0.0f;
    float score = 0.0f;
};

float median(std::vector<float> values)
{
    if (values.empty()) {
        return 0.0f;
    }
    const auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

bool evaluateVerticalBoundary(const cv::Mat &gray,
                              const cv::Mat &edges,
                              float topX,
                              float bottomX,
                              bool interiorOnRight,
                              BoundaryEvidence &evidence)
{
    const int shortDimension = std::min(gray.cols, gray.rows);
    const int sampleOffset = std::max(5, qRound(shortDimension * 0.025));
    const int edgeRadius = std::max(2, qRound(shortDimension * 0.01));
    const int horizontalMargin = sampleOffset + edgeRadius + 1;
    if (topX < horizontalMargin || topX >= gray.cols - horizontalMargin
        || bottomX < horizontalMargin || bottomX >= gray.cols - horizontalMargin) {
        return false;
    }

    int supportedRows = 0;
    int sampledRows = 0;
    int positiveRows = 0;
    int topSupported = 0;
    int topRows = 0;
    int bottomSupported = 0;
    int bottomRows = 0;
    int firstSupportedY = gray.rows;
    int lastSupportedY = -1;
    int maximumUnsupportedRun = 0;
    std::vector<float> differences;
    differences.reserve(static_cast<size_t>(gray.rows));

    const int topBandEnd = qRound(gray.rows * 0.12);
    const int bottomBandStart = qRound(gray.rows * 0.88);
    for (int y = 2; y < gray.rows - 2; ++y) {
        const float t = y / static_cast<float>(gray.rows - 1);
        const int x = qRound(topX + (bottomX - topX) * t);
        if (x < horizontalMargin || x >= gray.cols - horizontalMargin) {
            continue;
        }

        bool edgeSupported = false;
        for (int offset = -edgeRadius; offset <= edgeRadius; ++offset) {
            if (edges.at<uchar>(y, x + offset) != 0) {
                edgeSupported = true;
                break;
            }
        }
        ++sampledRows;
        if (edgeSupported) {
            ++supportedRows;
            if (lastSupportedY >= 0) {
                maximumUnsupportedRun = std::max(
                    maximumUnsupportedRun, y - lastSupportedY - 1);
            }
            firstSupportedY = std::min(firstSupportedY, y);
            lastSupportedY = y;
        }
        if (y < topBandEnd) {
            ++topRows;
            if (edgeSupported) ++topSupported;
        }
        if (y >= bottomBandStart) {
            ++bottomRows;
            if (edgeSupported) ++bottomSupported;
        }

        const int insideX = x + (interiorOnRight ? sampleOffset : -sampleOffset);
        const int outsideX = x + (interiorOnRight ? -sampleOffset : sampleOffset);
        const float difference = static_cast<float>(gray.at<uchar>(y, insideX))
            - static_cast<float>(gray.at<uchar>(y, outsideX));
        differences.push_back(difference);
        if (difference > 5.0f) {
            ++positiveRows;
        }
    }

    if (sampledRows == 0 || topRows == 0 || bottomRows == 0) {
        return false;
    }
    const float support = supportedRows / static_cast<float>(sampledRows);
    const float topSupport = topSupported / static_cast<float>(topRows);
    const float bottomSupport = bottomSupported / static_cast<float>(bottomRows);
    const float positiveFraction = positiveRows / static_cast<float>(sampledRows);
    const float contrast = median(std::move(differences));
    const float firstSupportedFraction =
        firstSupportedY / static_cast<float>(gray.rows - 1);
    const float lastSupportedFraction =
        lastSupportedY / static_cast<float>(gray.rows - 1);
    const float maximumGapFraction =
        maximumUnsupportedRun / static_cast<float>(gray.rows);

    // This fallback is intentionally conservative: both long sides must reach
    // close to the opposite frame edges, stay substantially contiguous through
    // bounded finger occlusion, and be brighter on the candidate's interior.
    if (support < 0.40f || topSupport < 0.12f || bottomSupport < 0.12f
        || firstSupportedFraction > 0.08f || lastSupportedFraction < 0.89f
        || maximumGapFraction > 0.20f
        || positiveFraction < 0.62f || contrast < 12.0f) {
        return false;
    }

    evidence = {topX, bottomX,
                firstSupportedFraction, lastSupportedFraction,
                contrast, support, contrast + support * 20.0f};
    return true;
}

void appendDistinctBoundary(std::vector<BoundaryEvidence> &boundaries,
                            const BoundaryEvidence &candidate)
{
    for (BoundaryEvidence &existing : boundaries) {
        if (std::abs(existing.topX - candidate.topX) < 7.0f
            && std::abs(existing.bottomX - candidate.bottomX) < 7.0f) {
            if (candidate.score > existing.score) {
                existing = candidate;
            }
            return;
        }
    }
    boundaries.push_back(candidate);
}

float parallelEdgeSupport(const cv::Mat &edges,
                          const BoundaryEvidence &boundary,
                          int horizontalOffset)
{
    int supportedRows = 0;
    int sampledRows = 0;
    const int radius = std::max(2, qRound(std::min(edges.cols, edges.rows) * 0.008));
    for (int y = 2; y < edges.rows - 2; ++y) {
        const float t = y / static_cast<float>(edges.rows - 1);
        const int x = qRound(boundary.topX
            + (boundary.bottomX - boundary.topX) * t) + horizontalOffset;
        if (x < radius || x >= edges.cols - radius) {
            continue;
        }
        ++sampledRows;
        for (int delta = -radius; delta <= radius; ++delta) {
            if (edges.at<uchar>(y, x + delta) != 0) {
                ++supportedRows;
                break;
            }
        }
    }
    return sampledRows > 0
        ? supportedRows / static_cast<float>(sampledRows) : 0.0f;
}

float maximumOutsideParallelSupport(const cv::Mat &edges,
                                    const BoundaryEvidence &boundary,
                                    int outsideDirection)
{
    const int shortDimension = std::min(edges.cols, edges.rows);
    const int firstOffset = std::max(10, qRound(shortDimension * 0.04));
    const int lastOffset = std::max(firstOffset, qRound(shortDimension * 0.10));
    const int step = std::max(3, qRound(shortDimension * 0.015));
    float maximum = 0.0f;
    for (int offset = firstOffset; offset <= lastOffset; offset += step) {
        maximum = std::max(maximum, parallelEdgeSupport(
            edges, boundary, outsideDirection * offset));
    }
    return maximum;
}

float outsideBandEdgeSupport(const cv::Mat &edges,
                             const BoundaryEvidence &boundary,
                             int outsideDirection)
{
    const int shortDimension = std::min(edges.cols, edges.rows);
    const int firstOffset = std::max(10, qRound(shortDimension * 0.04));
    const int lastOffset = std::max(firstOffset + 1,
        qRound(shortDimension * 0.12));
    const int radius = std::max(2, qRound(shortDimension * 0.008));
    int sampledRows = 0;
    int supportedRows = 0;
    for (int y = 2; y < edges.rows - 2; ++y) {
        const float t = y / static_cast<float>(edges.rows - 1);
        const int boundaryX = qRound(boundary.topX
            + (boundary.bottomX - boundary.topX) * t);
        ++sampledRows;
        bool supported = false;
        for (int offset = firstOffset;
             offset <= lastOffset && !supported;
             ++offset) {
            const int x = boundaryX + outsideDirection * offset;
            if (x < radius || x >= edges.cols - radius) {
                continue;
            }
            for (int delta = -radius; delta <= radius; ++delta) {
                if (edges.at<uchar>(y, x + delta) != 0) {
                    supported = true;
                    break;
                }
            }
        }
        if (supported) ++supportedRows;
    }
    return sampledRows > 0
        ? supportedRows / static_cast<float>(sampledRows) : 0.0f;
}

bool hasClippedDocumentAppearance(const cv::Mat &gray,
                                  const cv::Mat &edges,
                                  const Quad &quad)
{
    std::vector<cv::Point> polygon;
    polygon.reserve(quad.size());
    for (const cv::Point2f &point : quad) {
        polygon.emplace_back(qRound(point.x), qRound(point.y));
    }

    cv::Mat mask(gray.size(), CV_8UC1, cv::Scalar(0));
    cv::fillConvexPoly(mask, polygon, cv::Scalar(255));
    const int inset = std::max(7, qRound(std::min(gray.cols, gray.rows) * 0.035));
    const cv::Mat insetKernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(inset * 2 + 1, inset * 2 + 1));
    cv::Mat interior;
    cv::erode(mask, interior, insetKernel);

    const cv::Mat ringKernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(inset * 2 + 1, inset * 2 + 1));
    cv::Mat expanded;
    cv::dilate(mask, expanded, ringKernel);
    cv::Mat exteriorRing;
    cv::subtract(expanded, mask, exteriorRing);

    const int interiorPixels = cv::countNonZero(interior);
    const int exteriorPixels = cv::countNonZero(exteriorRing);
    if (interiorPixels <= 0 || exteriorPixels <= 0) {
        return false;
    }

    cv::Scalar interiorMean;
    cv::Scalar interiorStdDev;
    cv::meanStdDev(gray, interiorMean, interiorStdDev, interior);
    const double exteriorMean = cv::mean(gray, exteriorRing)[0];
    cv::Mat interiorEdges;
    cv::bitwise_and(edges, interior, interiorEdges);
    const double edgeDensity =
        cv::countNonZero(interiorEdges) / static_cast<double>(interiorPixels);

    // A line-only recovery is ambiguous for a blank panel or monitor. Require
    // light paper contrast plus bounded interior print/texture before inferring
    // the two clipped edges. Fully visible blank paper still uses the contour
    // detector above and is unaffected by this stricter fallback.
    return interiorMean[0] >= exteriorMean + 10.0
        && interiorStdDev[0] <= 75.0
        && edgeDensity >= 0.015
        && edgeDensity <= 0.35;
}

bool detectFrameClippedPaper(const cv::Mat &gray,
                             const cv::Mat &edges,
                             const cv::Mat &contextEdges,
                             Quad &quad)
{
    std::vector<cv::Vec2f> houghLines;
    const int voteThreshold = std::max(50, qRound(gray.rows * 0.28));
    cv::HoughLines(edges, houghLines, 1.0, CV_PI / 720.0, voteThreshold);

    std::vector<BoundaryEvidence> leftBoundaries;
    std::vector<BoundaryEvidence> rightBoundaries;
    int verticalLinesExamined = 0;
    for (size_t index = 0;
         index < houghLines.size() && verticalLinesExamined < 250;
         ++index) {
        const float rho = houghLines[index][0];
        const float theta = houghLines[index][1];
        float direction = theta + static_cast<float>(CV_PI / 2.0);
        if (direction >= CV_PI) direction -= static_cast<float>(CV_PI);
        if (std::abs(direction - static_cast<float>(CV_PI / 2.0))
            > static_cast<float>(CV_PI / 9.0)) {
            continue;
        }
        ++verticalLinesExamined;

        const float cosine = std::cos(theta);
        if (std::abs(cosine) < 0.1f) {
            continue;
        }
        const float sine = std::sin(theta);
        const float topX = rho / cosine;
        const float bottomX =
            (rho - (gray.rows - 1) * sine) / cosine;

        BoundaryEvidence boundary;
        if (evaluateVerticalBoundary(
                gray, edges, topX, bottomX, true, boundary)) {
            appendDistinctBoundary(leftBoundaries, boundary);
        }
        if (evaluateVerticalBoundary(
                gray, edges, topX, bottomX, false, boundary)) {
            appendDistinctBoundary(rightBoundaries, boundary);
        }
    }

    const float frameArea = static_cast<float>(gray.cols * gray.rows);
    const float verticalInset = std::max(1.0f, gray.rows * 0.005f);
    const auto xAtY = [&gray](const BoundaryEvidence &boundary, float y) {
        const float t = y / static_cast<float>(gray.rows - 1);
        return boundary.topX + (boundary.bottomX - boundary.topX) * t;
    };
    float bestScore = -std::numeric_limits<float>::max();
    Quad best{};
    for (const BoundaryEvidence &left : leftBoundaries) {
        for (const BoundaryEvidence &right : rightBoundaries) {
            const float firstSupported = std::max(
                left.firstSupportedFraction, right.firstSupportedFraction);
            const float lastSupported = std::min(
                left.lastSupportedFraction, right.lastSupportedFraction);
            const float topY = std::max(verticalInset,
                (firstSupported - 0.02f) * (gray.rows - 1));
            const float bottomY = std::min(gray.rows - 1.0f - verticalInset,
                (lastSupported + 0.02f) * (gray.rows - 1));
            const float height = bottomY - topY;
            if (height < gray.rows * 0.72f) {
                continue;
            }

            const float leftTopX = xAtY(left, topY);
            const float leftBottomX = xAtY(left, bottomY);
            const float rightTopX = xAtY(right, topY);
            const float rightBottomX = xAtY(right, bottomY);
            const float topWidth = rightTopX - leftTopX;
            const float bottomWidth = rightBottomX - leftBottomX;
            if (topWidth <= height * 0.40f || bottomWidth <= height * 0.40f
                || topWidth >= gray.cols * 0.90f
                || bottomWidth >= gray.cols * 0.90f) {
                continue;
            }

            const float aspectRatio = ((topWidth + bottomWidth) * 0.5f) / height;
            if (aspectRatio < 0.48f || aspectRatio > 1.10f) {
                continue;
            }

            // A bright display inside a dark bezel presents a second pair of
            // long rails just outside the apparent content. Reject that
            // ambiguous geometry rather than locking onto the display interior.
            const float leftOuterSupport =
                maximumOutsideParallelSupport(edges, left, -1);
            const float rightOuterSupport =
                maximumOutsideParallelSupport(edges, right, 1);
            const float leftOutsideBand =
                outsideBandEdgeSupport(contextEdges, left, -1);
            const float rightOutsideBand =
                outsideBandEdgeSupport(contextEdges, right, 1);
            if ((leftOuterSupport > 0.55f && rightOuterSupport > 0.55f)
                || (leftOutsideBand > 0.92f && rightOutsideBand > 0.92f)) {
                continue;
            }

            const Quad candidate = {
                cv::Point2f(leftTopX, topY),
                cv::Point2f(rightTopX, topY),
                cv::Point2f(rightBottomX, bottomY),
                cv::Point2f(leftBottomX, bottomY)
            };
            std::vector<cv::Point2f> polygon(candidate.begin(), candidate.end());
            const float area = static_cast<float>(std::abs(cv::contourArea(polygon)));
            if (area < frameArea * 0.18f || area > frameArea * 0.85f
                || !cv::isContourConvex(polygon)
                || !hasClippedDocumentAppearance(gray, edges, candidate)) {
                continue;
            }

            const float score = left.score + right.score
                + area / frameArea * 10.0f
                - std::abs(aspectRatio - 0.75f) * 8.0f;
            if (score > bestScore) {
                bestScore = score;
                best = candidate;
            }
        }
    }

    if (bestScore == -std::numeric_limits<float>::max()) {
        return false;
    }
    quad = best;
    return true;
}

bool detectPaperQuad(const QImage &image, Quad &normalizedQuad,
                     bool &frameClipped)
{
    frameClipped = false;
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

    cv::Mat lineEdges;
    cv::Canny(gray, lineEdges, 50.0, 150.0);
    cv::Mat contextEdges;
    cv::Canny(gray, contextEdges, 15.0, 45.0);
    cv::Mat contourEdges = lineEdges.clone();
    const cv::Mat closeKernel = cv::getStructuringElement(
        cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(contourEdges, contourEdges, cv::MORPH_CLOSE, closeKernel);
    cv::dilate(contourEdges, contourEdges, cv::Mat(), cv::Point(-1, -1), 1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(contourEdges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    std::sort(contours.begin(), contours.end(), [](const auto &a, const auto &b) {
        return std::abs(cv::contourArea(a)) > std::abs(cv::contourArea(b));
    });

    for (const auto &contour : contours) {
        Quad ordered{};
        if (!approximateDocumentQuad(contour, ordered)
            || !isDocumentLike(ordered, detectionImage.size())) {
            continue;
        }

        for (cv::Point2f &point : ordered) {
            point.x /= detectionImage.cols;
            point.y /= detectionImage.rows;
        }
        normalizedQuad = ordered;
        return true;
    }

    Quad clipped{};
    if (detectFrameClippedPaper(
            gray, lineEdges, contextEdges, clipped)) {
        for (cv::Point2f &point : clipped) {
            point.x /= detectionImage.cols;
            point.y /= detectionImage.rows;
        }
        normalizedQuad = clipped;
        frameClipped = true;
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
    quint64 frameCounter = 0;
    int framesSinceSuccessfulDetection = 0;
    bool detected = false;
#ifdef OBSBOT_HAS_OPENCV
    Quad normalizedQuad{};
    Quad pendingClippedQuad{};
    int consistentClippedDetections = 0;
    bool pendingClippedQuadValid = false;
#endif
};

PaperCropProcessor::PaperCropProcessor()
    : m_impl(std::make_unique<Impl>())
{
}

PaperCropProcessor::~PaperCropProcessor() = default;

void PaperCropProcessor::reset()
{
    *m_impl = Impl{};
}

QImage PaperCropProcessor::process(const QImage &image, const PaperCropSettings &settings)
{
    if (image.isNull() || settings.mode == PaperCropMode::Off) {
        reset();
        return image;
    }

    if (settings.mode == PaperCropMode::Manual) {
        reset();
        return applyManualCrop(image, settings);
    }

#ifdef OBSBOT_HAS_OPENCV
    constexpr quint64 detectionInterval = 10;
    constexpr int retainedDetectionFrames = 30;
    const bool shouldDetect = !m_impl->detected
        || m_impl->frameCounter % detectionInterval == 0
        || m_impl->framesSinceSuccessfulDetection >= retainedDetectionFrames;
    ++m_impl->frameCounter;

    bool detectedThisFrame = false;
    if (shouldDetect) {
        Quad candidate{};
        bool frameClipped = false;
        const bool found = detectPaperQuad(image, candidate, frameClipped);
        bool candidateAccepted = found;

        if (found && frameClipped && !m_impl->detected) {
            constexpr int requiredConsistentDetections = 3;
            constexpr float acquisitionDistance = 0.06f;
            if (m_impl->pendingClippedQuadValid) {
                candidate = alignQuadToReference(
                    candidate, m_impl->pendingClippedQuad);
                if (maximumCornerDistance(
                        candidate, m_impl->pendingClippedQuad)
                    <= acquisitionDistance) {
                    ++m_impl->consistentClippedDetections;
                    constexpr float pendingSmoothing = 0.5f;
                    for (int index = 0; index < 4; ++index) {
                        m_impl->pendingClippedQuad[index] =
                            m_impl->pendingClippedQuad[index]
                                * (1.0f - pendingSmoothing)
                            + candidate[index] * pendingSmoothing;
                    }
                } else {
                    m_impl->pendingClippedQuad = candidate;
                    m_impl->consistentClippedDetections = 1;
                }
            } else {
                m_impl->pendingClippedQuad = candidate;
                m_impl->pendingClippedQuadValid = true;
                m_impl->consistentClippedDetections = 1;
            }

            candidateAccepted =
                m_impl->consistentClippedDetections
                >= requiredConsistentDetections;
            if (candidateAccepted) {
                candidate = m_impl->pendingClippedQuad;
                m_impl->pendingClippedQuadValid = false;
                m_impl->consistentClippedDetections = 0;
            }
        } else if (found) {
            m_impl->pendingClippedQuadValid = false;
            m_impl->consistentClippedDetections = 0;
        } else if (!m_impl->detected) {
            // Acquisition requires consecutive evidence, not intermittent hits.
            m_impl->pendingClippedQuadValid = false;
            m_impl->consistentClippedDetections = 0;
        }

        if (candidateAccepted) {
            if (m_impl->detected) {
                candidate = alignQuadToReference(candidate, m_impl->normalizedQuad);
                constexpr float maximumUpdateDistance = 0.15f;
                if (maximumCornerDistance(candidate, m_impl->normalizedQuad)
                    <= maximumUpdateDistance) {
                    constexpr float smoothing = 0.35f;
                    for (int index = 0; index < 4; ++index) {
                        m_impl->normalizedQuad[index] =
                            m_impl->normalizedQuad[index] * (1.0f - smoothing)
                            + candidate[index] * smoothing;
                    }
                    detectedThisFrame = true;
                }
            } else {
                m_impl->normalizedQuad = candidate;
                m_impl->detected = true;
                detectedThisFrame = true;
            }

            if (detectedThisFrame) {
                m_impl->framesSinceSuccessfulDetection = 0;
            }
        }
    }

    if (m_impl->detected && !detectedThisFrame
        && ++m_impl->framesSinceSuccessfulDetection > retainedDetectionFrames) {
        m_impl->detected = false;
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
