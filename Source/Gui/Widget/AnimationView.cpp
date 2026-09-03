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
#include <array>
#include <cstdlib>
#include <vector>

#include <QAbstractVideoSurface>
#include <QPainter>
#include <QThread>
#include <QVideoFrame>
#include <QVideoSurfaceFormat>

namespace Gui::Widget {

namespace Detail {

// AVI decoding introduces small variations around a nominally solid matte. Matching by distance
// from the corner colour handles both the white and black source variants without keying out the
// enclosed highlights or shadows of the device itself.
constexpr int kMatteDistance = 24;
constexpr int kSoftEdgeDistance = 72;

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

bool IsMatte(QRgb pixel, const std::vector<QRgb> &palette)
{
    return qAlpha(pixel) != 0 && MatteDistance(pixel, palette) <= kMatteDistance;
}

//
// Clears every matte-coloured pixel that is connected to the frame border. The device artwork
// contains white too, but it is always enclosed by darker outlines, so the connectivity test is
// what separates it from the background - the same idea the converter uses on the black source.
//
void KnockOutAnimationBackground(QImage &image)
{
    const int width = image.width(), height = image.height();
    if (width == 0 || height == 0) {
        return;
    }

    const auto stride = image.bytesPerLine() / static_cast<int>(sizeof(QRgb));
    auto *pixels = reinterpret_cast<QRgb *>(image.bits());
    const auto at = [&](int x, int y) -> QRgb & { return pixels[y * stride + x]; };

    // Transitional frames can carry different mattes on different edges. Each of these samples
    // is guaranteed to be outside the centred device artwork, so all matching border-connected
    // regions are background even when a frame fades through grey.
    const std::array<QRgb, 8> matteSamples = {
        at(0, 0),
        at(width - 1, 0),
        at(0, height - 1),
        at(width - 1, height - 1),
        at(width / 2, 0),
        at(width / 2, height - 1),
        at(0, height / 2),
        at(width - 1, height / 2),
    };
    std::vector<QRgb> mattePalette;
    mattePalette.reserve(matteSamples.size());
    for (const auto sample : matteSamples) {
        const auto alreadyRepresented = std::any_of(
            mattePalette.cbegin(), mattePalette.cend(),
            [sample](QRgb matte) { return ColorDistance(sample, matte) <= kMatteDistance; });
        if (!alreadyRepresented) {
            mattePalette.push_back(sample);
        }
    }

    std::vector<std::pair<int, int>> stack;
    stack.reserve(4096);

    const auto seed = [&](int x, int y) {
        if (IsMatte(at(x, y), mattePalette)) {
            stack.emplace_back(x, y);
        }
    };
    for (int x = 0; x < width; ++x) {
        seed(x, 0);
        seed(x, height - 1);
    }
    for (int y = 1; y < height - 1; ++y) {
        seed(0, y);
        seed(width - 1, y);
    }

    // Scanline flood fill. A cleared pixel has alpha 0 and therefore never matches again.
    while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();

        if (!IsMatte(at(x, y), mattePalette)) {
            continue;
        }

        int left = x;
        while (left > 0 && IsMatte(at(left - 1, y), mattePalette)) {
            --left;
        }
        int right = x;
        while (right < width - 1 && IsMatte(at(right + 1, y), mattePalette)) {
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
                const bool isMatte = IsMatte(at(i, ny), mattePalette);
                if (isMatte && !inSpan) {
                    stack.emplace_back(i, ny);
                }
                inSpan = isMatte;
            }
        }
    }

    // Soften the edge: opaque pixels next to the cleared background fade by their lightness.
    const auto isClear = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height && qAlpha(at(x, y)) == 0;
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto pixel = at(x, y);
            if (qAlpha(pixel) == 0) {
                continue;
            }
            if (!isClear(x - 1, y) && !isClear(x + 1, y) && !isClear(x, y - 1) &&
                !isClear(x, y + 1))
            {
                continue;
            }
            const auto distance = MatteDistance(pixel, mattePalette);
            const int alpha = std::clamp(
                (distance - kMatteDistance) * 255 / (kSoftEdgeDistance - kMatteDistance), 0, 255);
            at(x, y) = qPremultiply(qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel), alpha));
        }
    }
}

} // namespace Detail

//////////////////////////////////////////////////

class AnimationView::VideoSurface : public QAbstractVideoSurface
{
public:
    explicit VideoSurface(AnimationView &view) : QAbstractVideoSurface{&view}, _view{view} {}

    QList<QVideoFrame::PixelFormat>
    supportedPixelFormats(QAbstractVideoBuffer::HandleType type) const override
    {
        if (type != QAbstractVideoBuffer::NoHandle) {
            return {};
        }
        // Everything `QVideoFrame::image()` can convert; the backend picks the first it offers.
        return {
            QVideoFrame::Format_ARGB32,
            QVideoFrame::Format_RGB32,
            QVideoFrame::Format_BGRA32,
            QVideoFrame::Format_BGR32,
            QVideoFrame::Format_RGB24,
            QVideoFrame::Format_BGR24,
            QVideoFrame::Format_RGB565,
            QVideoFrame::Format_YUV420P,
            QVideoFrame::Format_YV12,
            QVideoFrame::Format_NV12,
            QVideoFrame::Format_NV21,
            QVideoFrame::Format_UYVY,
            QVideoFrame::Format_YUYV,
            QVideoFrame::Format_ARGB32_Premultiplied,
            QVideoFrame::Format_BGRA32_Premultiplied,
        };
    }

    bool present(const QVideoFrame &frame) override
    {
        if (!frame.isValid()) {
            return false;
        }

        auto image = frame.image();
        if (image.isNull()) {
            setError(IncorrectFormatError);
            stop();
            return false;
        }

        if (QThread::currentThread() == _view.thread()) {
            _view.PresentFrame(std::move(image));
        }
        else {
            QMetaObject::invokeMethod(
                &_view,
                [this, image = std::move(image)]() mutable {
                    _view.PresentFrame(std::move(image));
                },
                Qt::QueuedConnection);
        }
        return true;
    }

private:
    AnimationView &_view;
};

//////////////////////////////////////////////////

AnimationView::AnimationView(QWidget *parent) : QWidget{parent}, _surface{new VideoSurface{*this}}
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

AnimationView::~AnimationView() = default;

QAbstractVideoSurface *AnimationView::Surface() const
{
    return _surface;
}

void AnimationView::Clear()
{
    _frame = {};
    _scaled = {};
    update();
}

void AnimationView::paintEvent(QPaintEvent *event)
{
    if (_scaled.isNull()) {
        return;
    }

    QPainter painter{this};
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto target = QRect{QPoint{}, _scaled.size()};
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
    auto processingSize = size() * 2;
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

    Detail::KnockOutAnimationBackground(frame);

    _frame = std::move(frame);
    RescaleFrame();
    update();
}

void AnimationView::RescaleFrame()
{
    if (_frame.isNull() || size().isEmpty()) {
        _scaled = {};
        return;
    }
    // Area-averaging scale; a plain painter transform would alias at this reduction ratio.
    _scaled = _frame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace Gui::Widget
