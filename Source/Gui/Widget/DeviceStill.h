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

#include <QImage>
#include <QString>

namespace Gui::Widget {

// The still picture iOS shows while one pod is out of the case: the left pod, the right pod and
// the closed case side by side, each with its own battery ring underneath.
struct DeviceStill {
    QImage image;
    // Horizontal centres of the three objects, as fractions of the image width.
    qreal leftCenter{0.0};
    qreal rightCenter{0.0};
    qreal caseCenter{0.0};
};

// Lays out `L.png`, `R.png` and `Case.png` from `directory` the way the iOS sheet does. Returns
// nothing if the model has no such pictures.
std::optional<DeviceStill> LoadDeviceStill(const QString &directory);

// The same layout for pictures already in memory.
DeviceStill ComposeDeviceStill(const QImage &left, const QImage &right, const QImage &caseBox);

} // namespace Gui::Widget
