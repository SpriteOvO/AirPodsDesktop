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

#pragma once

#include <optional>
#include <vector>

#include <QImage>
#include <QVariantAnimation>
#include <QWidget>

namespace Gui::Widget {

namespace Detail {

// Horizontal spans of opaque columns in a transparent render, left to right.
struct ImageSlices {
    struct Slice {
        int left;
        int right; // exclusive
    };
    std::vector<Slice> slices;
};

ImageSlices SliceByTransparentGaps(const QImage &image, int minGap);

} // namespace Detail

// The still render iOS shows on its AirPods sheet: pods on the left, case on the right. State
// changes re-arrange the pods (together while they sit in the case, apart once taken out) and
// cross-fade between the two compositions instead of cutting.
//
class DeviceImage : public QWidget
{
    Q_OBJECT

public:
    enum class Arrangement {
        Spread,
        Together,
        PodsOnly, // the case is left out and the pods take the middle
    };
    Q_ENUM(Arrangement)

    explicit DeviceImage(QWidget *parent = nullptr);

    // A transparent ARGB render; empty clears the view.
    void SetSource(QImage image);
    void SetArrangement(Arrangement arrangement, bool animate = true);
    Arrangement GetArrangement() const;
    bool IsFading() const;

    // Cross-fades from an arbitrary picture (e.g. the last video frame) into the current
    // composition. A null image just shows the composition.
    void CrossFadeFrom(QImage previous);

    // The composition currently shown (for tests); null while no source is set.
    QImage CurrentComposition() const;

    // Whole-view opacity, on top of any running composition fade.
    void SetOpacity(qreal opacity);
    qreal Opacity() const;

Q_SIGNALS:
    void Clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    struct Parts {
        QImage leftPod, rightPod, rest;
        QPoint leftPodPos, rightPodPos, restPos;
        int podGap{0};
    };

    QImage _source;
    std::optional<Parts> _parts;
    Arrangement _arrangement{Arrangement::Spread};
    QImage _current;
    QImage _previous;
    // `_current`/`_previous` fitted to the widget, so a fade frame is two cheap blits.
    mutable QImage _currentScaled;
    mutable QImage _previousScaled;
    QVariantAnimation _fade{this};
    qreal _opacity{1.0};

    void Recompose(bool animate);
    QImage Compose(Arrangement arrangement) const;
    void DrawFitted(QPainter &painter, const QImage &image, QImage &scaledCache, qreal opacity) const;
    void resizeEvent(QResizeEvent *event) override;
};

} // namespace Gui::Widget
