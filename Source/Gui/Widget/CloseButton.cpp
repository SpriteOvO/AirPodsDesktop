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

#include "CloseButton.h"

#include <QCoreApplication>
#include <QPainter>

#include "../Theme.h"

namespace Gui::Widget {

CloseButton::CloseButton(QWidget *parent) : QAbstractButton{parent}
{
    setFixedSize(kSize, kSize);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setAccessibleName(QCoreApplication::translate("Gui::SettingsWindow", "Close"));
}

void CloseButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const auto &colors = Theme::Manager::Instance().Colors();
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setPen(Qt::NoPen);
    painter.setBrush(
        isDown()       ? colors.mainClosePressed
        : underMouse() ? colors.mainCloseHover
                       : colors.mainCloseBg);
    painter.drawEllipse(QRectF{rect()});

    constexpr qreal margin = 7.0;
    const QRectF cross = QRectF{rect()}.adjusted(margin, margin, -margin, -margin);
    painter.setPen(QPen{colors.mainCloseGlyph, 2.5, Qt::SolidLine, Qt::RoundCap});
    painter.drawLine(cross.topLeft(), cross.bottomRight());
    painter.drawLine(cross.topRight(), cross.bottomLeft());
}

} // namespace Gui::Widget
