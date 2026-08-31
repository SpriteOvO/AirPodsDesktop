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

enum class SingleClickTimer { Unchanged, Start, Stop };

struct TrayActivationResult {
    TrayActivationAction action{TrayActivationAction::None};
    SingleClickTimer timer{SingleClickTimer::Unchanged};

    bool operator==(const TrayActivationResult &rhs) const = default;
};

// Decides what a tray-icon activation means, including what should happen to the timer that
// separates a single click from the first click of a double click. `TrayIcon` does exactly what
// this returns and derives nothing on its own, so there is one place to read and one place to test.
class TrayActivationState final
{
public:
    TrayActivationResult
    OnActivation(QSystemTrayIcon::ActivationReason reason, bool quickConnectEnabled)
    {
        switch (reason) {
        case QSystemTrayIcon::Trigger:
            if (!quickConnectEnabled) {
                return {TrayActivationAction::ShowMainWindow, SingleClickTimer::Stop};
            }
            // Wait out the double-click interval: Windows reports the first click of a double
            // click as a Trigger too, and that one must open the window instead of connecting.
            _singleClickPending = true;
            return {TrayActivationAction::None, SingleClickTimer::Start};

        case QSystemTrayIcon::DoubleClick:
        case QSystemTrayIcon::MiddleClick:
            _singleClickPending = false;
            return {TrayActivationAction::ShowMainWindow, SingleClickTimer::Stop};

        case QSystemTrayIcon::Context:
            // The menu is opening; a quick connect fired behind it would be unexplainable.
            _singleClickPending = false;
            return {TrayActivationAction::None, SingleClickTimer::Stop};

        default:
            return {};
        }
    }

    TrayActivationAction OnSingleClickTimeout()
    {
        if (!_singleClickPending) {
            return TrayActivationAction::None;
        }

        _singleClickPending = false;
        return TrayActivationAction::QuickConnect;
    }

private:
    bool _singleClickPending{};
};

} // namespace Gui
