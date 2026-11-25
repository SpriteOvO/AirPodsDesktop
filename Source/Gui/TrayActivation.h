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

#include <QSystemTrayIcon>

namespace Gui {

enum class TrayActivationAction { None, ShowMainWindow, QuickConnect };

// A single click acts immediately. Other left-button activation events do not open another window.
inline TrayActivationAction
GetTrayActivationAction(QSystemTrayIcon::ActivationReason reason, bool quickConnectEnabled)
{
    switch (reason) {
    case QSystemTrayIcon::Trigger:
        return quickConnectEnabled ? TrayActivationAction::QuickConnect
                                   : TrayActivationAction::ShowMainWindow;
    case QSystemTrayIcon::MiddleClick:
        return TrayActivationAction::ShowMainWindow;
    default:
        return TrayActivationAction::None;
    }
}

} // namespace Gui
