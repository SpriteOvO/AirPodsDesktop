//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021 SpriteOvO
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

#include <QString>

namespace Gui {

enum class Status { Updating, Available, Unavailable, Disconnected, Bind, Unbind };

inline QString DisplayableStatus(const Gui::Status &value)
{
    switch (value) {
    case Gui::Status::Updating:
    case Gui::Status::Available:
    case Gui::Status::Bind:
        APD_ASSERT(false);
    case Gui::Status::Unavailable:
        return QObject::tr("Unavailable");
    case Gui::Status::Disconnected:
        return QObject::tr("Disconnected");
    case Gui::Status::Unbind:
        return QObject::tr("Waiting for Binding");
    default:
        return "Unknown";
    }
}

} // namespace Gui
