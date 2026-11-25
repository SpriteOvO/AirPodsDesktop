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

#include "ActivityIndicator.h"

#include <QPainter>
#include <QtMath>

namespace Gui::Widget {

namespace {

constexpr int kTurnMs = 1000;
// The spoke just behind the head is this much dimmer, and so on round the circle.
constexpr qreal kFadePerSpoke = 0.1;

} // namespace

ActivityIndicator::ActivityIndicator(int size, QWidget *parent) : QWidget{parent}
{
    setFixedSize(size, size);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    _timer.setInterval(kTurnMs / kSpokes);
    connect(&_timer, &QTimer::timeout, this, [this] {
        _head = (_head + 1) % kSpokes;
        update();
    });
}

void ActivityIndicator::SetColor(const QColor &color)
{
    _color = color;
    update();
}

int ActivityIndicator::HeadSpoke() const
{
    return _head;
}

void ActivityIndicator::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const qreal side = qMin(width(), height());
    const qreal spokeWidth = side / 8.0;
    const qreal outer = side / 2.0 - spokeWidth / 2.0;
    const qreal inner = side / 4.0;

    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(width() / 2.0, height() / 2.0);

    for (int spoke = 0; spoke < kSpokes; ++spoke) {
        const int age = (_head - spoke + kSpokes) % kSpokes;
        QColor color = _color;
        color.setAlphaF(_color.alphaF() * (1.0 - age * kFadePerSpoke));
        painter.setPen(QPen{color, spokeWidth, Qt::SolidLine, Qt::RoundCap});

        // Clockwise from straight up.
        const qreal angle = qDegreesToRadians(spoke * 360.0 / kSpokes);
        const QPointF direction{qSin(angle), -qCos(angle)};
        painter.drawLine(direction * inner, direction * outer);
    }
}

void ActivityIndicator::showEvent(QShowEvent *event)
{
    _timer.start();
    QWidget::showEvent(event);
}

void ActivityIndicator::hideEvent(QHideEvent *event)
{
    _timer.stop();
    QWidget::hideEvent(event);
}

} // namespace Gui::Widget
