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

#include "DeviceStill.h"

#include <QPainter>

namespace Gui::Widget {

namespace {

// Measured from the iOS sheet, in pixels of the 3x pictures it is built from: the three pictures
// stand in a row 26 pt apart, centred on a common horizontal line. The row's height is the part
// of the sheet the popup's device view covers, so every model is drawn at the size iOS draws it.
constexpr int kGap = 78;
constexpr int kRowHeight = 392;

} // namespace

DeviceStill ComposeDeviceStill(const QImage &left, const QImage &right, const QImage &caseBox)
{
    const int width = left.width() + kGap + right.width() + kGap + caseBox.width();

    DeviceStill still;
    still.image = QImage{width, kRowHeight, QImage::Format_ARGB32_Premultiplied};
    still.image.fill(Qt::transparent);

    QPainter painter{&still.image};
    int x = 0;
    const auto place = [&](const QImage &part) {
        painter.drawImage(x, (kRowHeight - part.height()) / 2, part);
        const qreal center = (x + part.width() / 2.0) / width;
        x += part.width() + kGap;
        return center;
    };
    still.leftCenter = place(left);
    still.rightCenter = place(right);
    still.caseCenter = place(caseBox);
    return still;
}

std::optional<DeviceStill> LoadDeviceStill(const QString &directory)
{
    if (directory.isEmpty()) {
        return std::nullopt;
    }
    const QImage left{directory + "/L.png"};
    const QImage right{directory + "/R.png"};
    const QImage caseBox{directory + "/Case.png"};
    if (left.isNull() || right.isNull() || caseBox.isNull()) {
        return std::nullopt;
    }
    return ComposeDeviceStill(left, right, caseBox);
}

} // namespace Gui::Widget
