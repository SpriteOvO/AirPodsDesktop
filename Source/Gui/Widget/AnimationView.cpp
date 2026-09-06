//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "AnimationView.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <vector>

#include <QPainter>
#include <QThread>
#include <QVideoFrame>
#include <QVideoSink>

namespace Gui::Widget {

namespace Detail {

// White devices are only a few RGB levels away from the compressed matte. Allow only codec
// rounding here; a broad chroma tolerance removes the case and stems with the background.
constexpr int kMatteDistance = 2;
constexpr int kChromaMatteDistance = 12;
constexpr int kChromaRange = 128;
constexpr int kChromaSpill = 16;
constexpr int kForegroundDistance = 24;
constexpr int kDarkForegroundDistance = 96;
constexpr int kDarkForegroundLuma = 96;
constexpr int kSoftEdgeDistance = 224;
constexpr int kForegroundSearchRadius = 4;
constexpr int kDarkForegroundSearchRadius = 8;
constexpr int kEdgeSearchRadius = 2;
constexpr int kDarkEdgeSearchRadius = 4;
constexpr int kEnclosedForegroundSpan = 8;
constexpr int kMinimumForegroundAreaDivisor = 600;
constexpr int kForegroundIslandAlpha = 32;

int ColorDistance(QRgb pixel, QRgb matte)
{
    return (std::max)(
        {std::abs(qRed(pixel) - qRed(matte)), std::abs(qGreen(pixel) - qGreen(matte)),
         std::abs(qBlue(pixel) - qBlue(matte))});
}

int MatteDistance(QRgb pixel, const std::vector<QRgb> &palette)
{
    int distance = 255;
    for (const auto matte : palette) {
        distance = (std::min)(distance, ColorDistance(pixel, matte));
    }
    return distance;
}

QRgb NearestMatte(QRgb pixel, const std::vector<QRgb> &palette)
{
    return *std::min_element(palette.cbegin(), palette.cend(), [pixel](QRgb lhs, QRgb rhs) {
        return ColorDistance(pixel, lhs) < ColorDistance(pixel, rhs);
    });
}

bool IsMatte(QRgb pixel, const std::vector<QRgb> &palette, int tolerance)
{
    return qAlpha(pixel) != 0 && MatteDistance(pixel, palette) <= tolerance;
}

//
// Clears matte-coloured pixels connected to the frame corners. Some animations crop white device
// artwork against the right edge, so seeding every border pixel would erase that foreground too.
// Starting from guaranteed-background corners lets the device outline stop the flood fill.
//
void KnockOutAnimationBackground(QImage &image, bool removeEnclosedBackground)
{
    const int width = image.width(), height = image.height();
    if (width == 0 || height == 0) {
        return;
    }

    const auto stride = image.bytesPerLine() / static_cast<int>(sizeof(QRgb));
    auto *pixels = reinterpret_cast<QRgb *>(image.bits());
    const auto at = [&](int x, int y) -> QRgb & { return pixels[y * stride + x]; };

    std::vector<QRgb> mattePalette;
    mattePalette.reserve(4);
    const auto addMatte = [&](QRgb sample) {
        const auto alreadyRepresented =
            std::any_of(mattePalette.cbegin(), mattePalette.cend(), [sample](QRgb matte) {
                return ColorDistance(sample, matte) <= kMatteDistance;
            });
        if (!alreadyRepresented) {
            mattePalette.push_back(sample);
        }
    };

    // Only corners are guaranteed background. Following smooth colour changes along an edge can
    // walk into a cropped white case and add its colours to the background palette.
    addMatte(at(0, 0));
    addMatte(at(width - 1, 0));
    addMatte(at(0, height - 1));
    addMatte(at(width - 1, height - 1));

    const bool chromaMatte =
        std::any_of(mattePalette.cbegin(), mattePalette.cend(), [](QRgb matte) {
            const auto [minimum, maximum] = std::minmax({qRed(matte), qGreen(matte), qBlue(matte)});
            return maximum - minimum >= kChromaRange;
        });
    const int matteTolerance = chromaMatte ? kChromaMatteDistance : kMatteDistance;

    std::vector<std::pair<int, int>> stack;
    stack.reserve(4096);

    const auto seed = [&](int x, int y) {
        if (IsMatte(at(x, y), mattePalette, matteTolerance)) {
            stack.emplace_back(x, y);
        }
    };
    seed(0, 0);
    seed(width - 1, 0);
    seed(0, height - 1);
    seed(width - 1, height - 1);

    // Scanline flood fill. A cleared pixel has alpha 0 and therefore never matches again.
    while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();

        if (!IsMatte(at(x, y), mattePalette, matteTolerance)) {
            continue;
        }

        int left = x;
        while (left > 0 && IsMatte(at(left - 1, y), mattePalette, matteTolerance)) {
            --left;
        }
        int right = x;
        while (right < width - 1 && IsMatte(at(right + 1, y), mattePalette, matteTolerance)) {
            ++right;
        }

        for (int i = left; i <= right; ++i) {
            at(i, y) = 0;
        }

        for (const int ny : {y - 1, y + 1}) {
            if (ny < 0 || ny >= height) {
                continue;
            }
            bool inSpan = false;
            for (int i = left; i <= right; ++i) {
                const bool isMatte = IsMatte(at(i, ny), mattePalette, matteTolerance);
                if (isMatte && !inSpan) {
                    stack.emplace_back(i, ny);
                }
                inSpan = isMatte;
            }
        }
    }

    if (chromaMatte) {
        // The generated chroma matte cannot occur naturally in the product, so enclosed samples
        // are always background and can be removed without the white-on-white preservation logic.
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (IsMatte(at(x, y), mattePalette, matteTolerance)) {
                    at(x, y) = 0;
                }
            }
        }
    }
    else if (removeEnclosedBackground) {
        // The Max headband surrounds a large area of real matte. Its narrow white supports and
        // highlights can use exactly the same RGB value, so clearing every matte-coloured pixel
        // punches holes through the product. Keep a white pixel only when confidently non-matte
        // pixels bracket it across a short horizontal, vertical, or diagonal span. That preserves
        // the thin product surfaces while removing the broad enclosed background.
        std::vector<bool> foreground(static_cast<size_t>(width) * height);
        const auto index = [width](int x, int y) { return static_cast<size_t>(y) * width + x; };
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                foreground[index(x, y)] =
                    qAlpha(at(x, y)) != 0 &&
                    MatteDistance(at(x, y), mattePalette) >= kForegroundDistance;
            }
        }
        const auto hasForeground = [&](int x, int y, int dx, int dy) {
            for (int distance = 1; distance <= kEnclosedForegroundSpan; ++distance) {
                const int sx = x + dx * distance, sy = y + dy * distance;
                if (sx < 0 || sx >= width || sy < 0 || sy >= height) {
                    return false;
                }
                if (foreground[index(sx, sy)]) {
                    return true;
                }
            }
            return false;
        };
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (!IsMatte(at(x, y), mattePalette, matteTolerance)) {
                    continue;
                }
                const bool horizontal = hasForeground(x, y, -1, 0) && hasForeground(x, y, 1, 0);
                const bool vertical = hasForeground(x, y, 0, -1) && hasForeground(x, y, 0, 1);
                const bool diagonal = (hasForeground(x, y, -1, -1) && hasForeground(x, y, 1, 1)) ||
                                      (hasForeground(x, y, 1, -1) && hasForeground(x, y, -1, 1));
                const int centreGapHalfWidth = (std::max)(2, width / 32);
                const bool centralLowerGap =
                    y >= height / 2 && std::abs(x - width / 2) <= centreGapHalfWidth && !vertical;
                const bool insideEnclosedRegion = x >= width / 5 && x <= width * 4 / 5;
                if ((!(horizontal || vertical || diagonal) && insideEnclosedRegion) ||
                    centralLowerGap)
                {
                    at(x, y) = 0;
                }
            }
        }
    }

    // Reconstruct antialiased edge pixels against transparency. Looking up a nearby foreground
    // colour also removes the white spill that otherwise becomes a bright outline in dark mode.
    std::vector<bool> clear(static_cast<size_t>(width) * height);
    const auto index = [width](int x, int y) { return static_cast<size_t>(y) * width + x; };
    qint64 foregroundLuma = 0;
    int foregroundPixelCount = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            clear[index(x, y)] = qAlpha(at(x, y)) == 0;
            if (!clear[index(x, y)]) {
                foregroundLuma += qGray(at(x, y));
                ++foregroundPixelCount;
            }
        }
    }
    const bool darkForeground =
        foregroundPixelCount > 0 && foregroundLuma / foregroundPixelCount < kDarkForegroundLuma;
    const int foregroundSearchRadius =
        darkForeground || chromaMatte ? kDarkForegroundSearchRadius : kForegroundSearchRadius;
    const int edgeSearchRadius =
        darkForeground || chromaMatte ? kDarkEdgeSearchRadius : kEdgeSearchRadius;
    const int minimumForegroundDistance =
        darkForeground ? kDarkForegroundDistance : kForegroundDistance;
    const QImage edgeSource = image.copy();
    const auto edgeStride = edgeSource.bytesPerLine() / static_cast<int>(sizeof(QRgb));
    const auto *edgePixels = reinterpret_cast<const QRgb *>(edgeSource.constBits());
    const auto edgeAt = [&](int x, int y) { return edgePixels[y * edgeStride + x]; };
    const auto isClear = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height && clear[index(x, y)];
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto pixel = edgeAt(x, y);
            if (qAlpha(pixel) == 0) {
                continue;
            }
            const auto distance = MatteDistance(pixel, mattePalette);
            const bool chromaContaminated = chromaMatte &&
                                            qRed(pixel) - qGreen(pixel) >= kChromaSpill &&
                                            qBlue(pixel) - qGreen(pixel) >= kChromaSpill;
            if (chromaMatte && !chromaContaminated) {
                continue;
            }
            bool nearClear = chromaContaminated;
            for (int dy = -edgeSearchRadius; dy <= edgeSearchRadius && !nearClear; ++dy) {
                for (int dx = -edgeSearchRadius; dx <= edgeSearchRadius; ++dx) {
                    if ((dx != 0 || dy != 0) && isClear(x + dx, y + dy)) {
                        nearClear = true;
                        break;
                    }
                }
            }
            if (!nearClear) {
                continue;
            }
            if (!chromaMatte && distance >= kSoftEdgeDistance) {
                continue;
            }

            QRgb foreground = pixel;
            int foregroundDistance = distance;
            for (int sy = (std::max)(0, y - foregroundSearchRadius);
                 sy <= (std::min)(height - 1, y + foregroundSearchRadius); ++sy)
            {
                for (int sx = (std::max)(0, x - foregroundSearchRadius);
                     sx <= (std::min)(width - 1, x + foregroundSearchRadius); ++sx)
                {
                    if (clear[index(sx, sy)]) {
                        continue;
                    }
                    const int candidateDistance = MatteDistance(edgeAt(sx, sy), mattePalette);
                    if (candidateDistance > foregroundDistance) {
                        foreground = edgeAt(sx, sy);
                        foregroundDistance = candidateDistance;
                    }
                }
            }

            if (foregroundDistance < minimumForegroundDistance) {
                at(x, y) = 0;
                continue;
            }

            const auto matte = NearestMatte(pixel, mattePalette);
            const int channelDistances[]{
                std::abs(qRed(foreground) - qRed(matte)),
                std::abs(qGreen(foreground) - qGreen(matte)),
                std::abs(qBlue(foreground) - qBlue(matte)),
            };
            const int channel = static_cast<int>(std::distance(
                std::begin(channelDistances),
                std::max_element(std::begin(channelDistances), std::end(channelDistances))));
            const int observed[]{qRed(pixel), qGreen(pixel), qBlue(pixel)};
            const int matteChannels[]{qRed(matte), qGreen(matte), qBlue(matte)};
            const int foregroundChannels[]{qRed(foreground), qGreen(foreground), qBlue(foreground)};
            const int denominator = foregroundChannels[channel] - matteChannels[channel];
            const int alpha =
                denominator == 0
                    ? 255
                    : std::clamp(
                          (observed[channel] - matteChannels[channel]) * 255 / denominator, 0, 255);
            int outputRed = qRed(foreground), outputGreen = qGreen(foreground);
            int outputBlue = qBlue(foreground);
            const auto [minimumChannel, maximumChannel] =
                std::minmax({outputRed, outputGreen, outputBlue});
            const bool foregroundHasMagentaSpill =
                outputRed - outputGreen >= kChromaSpill && outputBlue - outputGreen >= kChromaSpill;
            if (chromaMatte && (maximumChannel - minimumChannel < 64 || foregroundHasMagentaSpill))
            {
                // Chroma subsampling can tint otherwise neutral AirPods/Beats edges. Their nearby
                // opaque foreground is the reliable colour reference, so neutralise only that
                // low-saturation reference while preserving coloured LEDs and logos.
                outputRed = outputGreen = outputBlue = qGray(foreground);
            }
            at(x, y) = qPremultiply(qRgba(outputRed, outputGreen, outputBlue, alpha));
        }
    }

    // Compression can leave detached one-pixel strips and specks that are not connected to any
    // device surface (notably beside the Pro 3 case). Remove only tiny alpha islands; separate
    // earbuds and cases remain orders of magnitude larger than this scale-relative threshold.
    std::vector<bool> visited(static_cast<size_t>(width) * height);
    std::vector<std::pair<int, int>> component;
    component.reserve(512);
    const int minimumArea = (std::max)(16, width * height / kMinimumForegroundAreaDivisor);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (visited[index(x, y)] || qAlpha(at(x, y)) < kForegroundIslandAlpha) {
                continue;
            }
            component.clear();
            stack.clear();
            stack.emplace_back(x, y);
            visited[index(x, y)] = true;
            while (!stack.empty()) {
                const auto [cx, cy] = stack.back();
                stack.pop_back();
                component.emplace_back(cx, cy);
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || nx >= width || ny < 0 || ny >= height ||
                            visited[index(nx, ny)] || qAlpha(at(nx, ny)) < kForegroundIslandAlpha)
                        {
                            continue;
                        }
                        visited[index(nx, ny)] = true;
                        stack.emplace_back(nx, ny);
                    }
                }
            }
            if (static_cast<int>(component.size()) < minimumArea) {
                for (const auto [cx, cy] : component) {
                    at(cx, cy) = 0;
                }
            }
        }
    }
}

} // namespace Detail

//////////////////////////////////////////////////

AnimationView::AnimationView(QWidget *parent) : QWidget{parent}, _videoSink{new QVideoSink{this}}
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    connect(
        _videoSink, &QVideoSink::videoFrameChanged, this, &AnimationView::OnVideoFrameChanged,
        Qt::DirectConnection);
}

AnimationView::~AnimationView()
{
    // QVideoSink is a QObject child, but delete it while our frame-delivery state still exists.
    // This also disconnects a decoder thread before the mutex and pending image are destroyed.
    disconnect(_videoSink, nullptr, this, nullptr);
    delete _videoSink;
    _videoSink = nullptr;
}

QVideoSink *AnimationView::VideoSink() const
{
    return _videoSink;
}

void AnimationView::Clear()
{
    InvalidatePendingFrames();
    _frame = {};
    RescaleFrame();
    update();
}

void AnimationView::SetFallbackImage(QImage image)
{
    _fallback = std::move(image);
    Clear();
}

void AnimationView::SetPlaybackEnabled(bool enabled)
{
    {
        std::lock_guard lock{_frameMutex};
        _playbackEnabled = enabled;
        ++_generation;
        _pendingFrame = {};
    }
    if (!enabled) {
        Clear();
    }
}

void AnimationView::InvalidatePendingFrames()
{
    std::lock_guard lock{_frameMutex};
    ++_generation;
    _pendingFrame = {};
}

void AnimationView::OnVideoFrameChanged(const QVideoFrame &frame)
{
    quint64 generation;
    {
        std::lock_guard lock{_frameMutex};
        if (!_playbackEnabled) {
            return;
        }
        generation = _generation;
    }
    if (!frame.isValid()) {
        return;
    }

    auto image = frame.toImage();
    if (image.isNull()) {
        return;
    }

    if (QThread::currentThread() == thread()) {
        PresentFrame(std::move(image));
        return;
    }

    std::lock_guard lock{_frameMutex};
    if (!_playbackEnabled || generation != _generation) {
        return;
    }
    _pendingFrame = std::move(image);
    if (_deliveryQueued) {
        return;
    }
    _deliveryQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            QImage latest;
            {
                std::lock_guard lock{_frameMutex};
                latest = std::move(_pendingFrame);
                _pendingFrame = {};
                _deliveryQueued = false;
            }
            if (!latest.isNull()) {
                PresentFrame(std::move(latest));
            }
        },
        Qt::QueuedConnection);
}

void AnimationView::SetRemoveEnclosedBackground(bool enabled)
{
    if (_removeEnclosedBackground != enabled) {
        _removeEnclosedBackground = enabled;
        Clear();
    }
}

void AnimationView::paintEvent(QPaintEvent *event)
{
    if (_scaled.devicePixelRatio() != devicePixelRatioF()) {
        RescaleFrame();
    }
    if (_scaled.isNull()) {
        return;
    }

    QPainter painter{this};
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto target = QRect{QPoint{}, _scaled.size() / _scaled.devicePixelRatio()};
    painter.drawImage(
        QPoint{(width() - target.width()) / 2, (height() - target.height()) / 2}, _scaled);
}

void AnimationView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    RescaleFrame();
}

void AnimationView::mouseReleaseEvent(QMouseEvent *event)
{
    Q_EMIT Clicked();
}

void AnimationView::PresentFrame(QImage frame)
{
    // The source videos are much larger than the popup. Keying at twice the display resolution
    // preserves antialiased edges while avoiding a full-frame flood fill on every decoded frame.
    auto processingSize = size() * (std::max)(2.0, devicePixelRatioF());
    if (processingSize.isEmpty()) {
        processingSize = QSize{520, 244};
    }
    if (frame.width() > processingSize.width() || frame.height() > processingSize.height()) {
        frame = frame.scaled(processingSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // The matte removal writes premultiplied alpha, and downscaling must blend with it too.
    if (frame.format() != QImage::Format_ARGB32_Premultiplied) {
        frame = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    else {
        frame.detach();
    }

    Detail::KnockOutAnimationBackground(frame, _removeEnclosedBackground);

    _frame = std::move(frame);
    RescaleFrame();
    update();
    Q_EMIT FramePresented();
}

void AnimationView::RescaleFrame()
{
    const auto &source = _frame.isNull() ? _fallback : _frame;
    if (source.isNull() || size().isEmpty()) {
        _scaled = {};
        return;
    }
    // Area-averaging scale; a plain painter transform would alias at this reduction ratio.
    _scaled =
        source.scaled(size() * devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    _scaled.setDevicePixelRatio(devicePixelRatioF());
}

} // namespace Gui::Widget
