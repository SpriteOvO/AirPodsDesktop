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

#include "DeviceImage.h"

#include <algorithm>

#include <QEasingCurve>
#include <QPainter>

namespace Gui::Widget {

namespace Detail {

ImageSlices SliceByTransparentGaps(const QImage &image, int minGap)
{
    ImageSlices result;
    if (image.isNull()) {
        return result;
    }

    const QImage argb =
        image.format() == QImage::Format_ARGB32 ? image : image.convertToFormat(QImage::Format_ARGB32);
    const int width = argb.width();
    const int height = argb.height();

    std::vector<bool> opaque(width, false);
    for (int y = 0; y < height; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (!opaque[x] && qAlpha(line[x]) > 16) {
                opaque[x] = true;
            }
        }
    }

    // Merge content runs that are separated by gaps narrower than `minGap`, so the shadow
    // between a pod's stem and its bud does not count as a split.
    std::optional<int> runStart;
    int gapWidth = 0;
    for (int x = 0; x <= width; ++x) {
        const bool isOpaque = x < width && opaque[x];
        if (isOpaque) {
            if (!runStart.has_value()) {
                runStart = x;
            }
            else if (gapWidth >= minGap) {
                result.slices.push_back({*runStart, x - gapWidth});
                runStart = x;
            }
            gapWidth = 0;
        }
        else if (runStart.has_value()) {
            ++gapWidth;
        }
    }
    if (runStart.has_value()) {
        result.slices.push_back({*runStart, width - gapWidth});
    }
    return result;
}

} // namespace Detail

DeviceImage::DeviceImage(QWidget *parent) : QWidget{parent}
{
    setAttribute(Qt::WA_TranslucentBackground);
    // Sequential: the old picture fades out completely, then the new one fades in.
    _fade.setDuration(1000);
    _fade.setEasingCurve(QEasingCurve::Linear);
    _fade.setStartValue(0.0);
    _fade.setEndValue(1.0);
    connect(&_fade, &QVariantAnimation::valueChanged, this, [this] { update(); });
    connect(&_fade, &QVariantAnimation::finished, this, [this] {
        _previous = {};
        _previousScaled = {};
        update();
    });
}

void DeviceImage::SetSource(QImage image)
{
    _source = image.isNull() ? QImage{} : image.convertToFormat(QImage::Format_ARGB32);
    _parts.reset();

    if (!_source.isNull()) {
        // Gaps below ~2 % of the width are texture, not separation between objects.
        const int minGap = (std::max)(_source.width() / 50, 4);
        const auto slices = Detail::SliceByTransparentGaps(_source, minGap).slices;
        // Pods are the two leftmost objects when the render has at least three; anything else
        // (a headset, a single bud) is shown as-is.
        if (slices.size() >= 3) {
            const auto &left = slices[0];
            const auto &right = slices[1];
            Parts parts;
            parts.leftPod = _source.copy(left.left, 0, left.right - left.left, _source.height());
            parts.rightPod = _source.copy(right.left, 0, right.right - right.left, _source.height());
            QImage rest = _source.copy();
            QPainter eraser{&rest};
            eraser.setCompositionMode(QPainter::CompositionMode_Clear);
            eraser.fillRect(left.left, 0, right.right - left.left, _source.height(), Qt::transparent);
            eraser.end();
            parts.rest = rest;
            parts.leftPodPos = {left.left, 0};
            parts.rightPodPos = {right.left, 0};
            parts.restPos = {0, 0};
            parts.podGap = right.left - left.right;
            _parts = parts;
        }
    }

    _fade.stop();
    _previous = {};
    _current = Compose(_arrangement);
    _previousScaled = {};
    _currentScaled = {};
    update();
}

void DeviceImage::SetArrangement(Arrangement arrangement, bool animate)
{
    if (_arrangement == arrangement) {
        return;
    }
    _arrangement = arrangement;
    Recompose(animate);
}

auto DeviceImage::GetArrangement() const -> Arrangement
{
    return _arrangement;
}

bool DeviceImage::IsFading() const
{
    return _fade.state() == QAbstractAnimation::Running;
}

void DeviceImage::CrossFadeFrom(QImage previous)
{
    _fade.stop();
    if (previous.isNull() || _current.isNull()) {
        _previous = {};
        _previousScaled = {};
        update();
        return;
    }
    _previous = std::move(previous);
    _previousScaled = {};
    _fade.start();
    update();
}

QImage DeviceImage::CurrentComposition() const
{
    return _current;
}

void DeviceImage::SetOpacity(qreal opacity)
{
    _opacity = std::clamp(opacity, 0.0, 1.0);
    update();
}

qreal DeviceImage::Opacity() const
{
    return _opacity;
}

void DeviceImage::Recompose(bool animate)
{
    if (_source.isNull()) {
        return;
    }
    auto next = Compose(_arrangement);
    if (next == _current) {
        return;
    }
    if (animate && isVisible()) {
        _previous = _current;
        _previousScaled = _currentScaled;
        _current = std::move(next);
        _currentScaled = {};
        _fade.stop();
        _fade.start();
    }
    else {
        _fade.stop();
        _previous = {};
        _previousScaled = {};
        _current = std::move(next);
        _currentScaled = {};
    }
    update();
}

QImage DeviceImage::Compose(Arrangement arrangement) const
{
    if (_source.isNull()) {
        return {};
    }
    if (!_parts.has_value() || arrangement == Arrangement::Spread) {
        return _source;
    }

    const auto &parts = *_parts;
    QImage composed{_source.size(), QImage::Format_ARGB32};
    composed.fill(Qt::transparent);
    QPainter painter{&composed};

    if (arrangement == Arrangement::PodsOnly) {
        // Case gone, the pair centred where the whole render used to be.
        const int pairWidth = parts.rightPodPos.x() + parts.rightPod.width() - parts.leftPodPos.x();
        const int offset = (_source.width() - pairWidth) / 2 - parts.leftPodPos.x();
        painter.drawImage(parts.leftPodPos + QPoint{offset, 0}, parts.leftPod);
        painter.drawImage(parts.rightPodPos + QPoint{offset, 0}, parts.rightPod);
        return composed;
    }

    // Together: the pods slide towards each other until only a sliver of the gap is left.
    const int shift = (std::max)(parts.podGap * 2 / 5, 0);
    painter.drawImage(parts.restPos, parts.rest);
    painter.drawImage(parts.leftPodPos + QPoint{shift, 0}, parts.leftPod);
    painter.drawImage(parts.rightPodPos - QPoint{shift, 0}, parts.rightPod);
    return composed;
}

void DeviceImage::DrawFitted(
    QPainter &painter, const QImage &image, QImage &scaledCache, qreal opacity) const
{
    if (image.isNull() || opacity <= 0.0) {
        return;
    }

    const qreal dpr = devicePixelRatioF();
    const QSize fitted = image.size().scaled(size() * dpr, Qt::KeepAspectRatio);
    if (scaledCache.isNull() || scaledCache.size() != fitted || scaledCache.cacheKey() == 0) {
        scaledCache = image.scaled(fitted, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        scaledCache.setDevicePixelRatio(dpr);
    }

    const QSizeF logical = QSizeF{fitted} / dpr;
    const QPointF topLeft{(width() - logical.width()) / 2.0, (height() - logical.height()) / 2.0};
    painter.setOpacity(opacity * _opacity);
    painter.drawImage(topLeft, scaledCache);
}

void DeviceImage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    _currentScaled = {};
    _previousScaled = {};
}

void DeviceImage::paintEvent(QPaintEvent *)
{
    QPainter painter{this};
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (IsFading()) {
        const qreal t = _fade.currentValue().toReal();
        const QEasingCurve ease{QEasingCurve::InOutSine};
        if (t < 0.5) {
            DrawFitted(painter, _previous, _previousScaled, 1.0 - ease.valueForProgress(t * 2.0));
        }
        else {
            DrawFitted(painter, _current, _currentScaled, ease.valueForProgress(t * 2.0 - 1.0));
        }
    }
    else {
        DrawFitted(painter, _current, _currentScaled, 1.0);
    }
}

void DeviceImage::mouseReleaseEvent(QMouseEvent *)
{
    Q_EMIT Clicked();
}

} // namespace Gui::Widget
