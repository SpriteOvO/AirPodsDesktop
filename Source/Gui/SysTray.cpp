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

#include "SysTray.h"

#include "../Application.h"
#include "InfoWindow.h"

namespace Gui {

SysTray::SysTray()
{
    connect(_actionSettings, &QAction::triggered, this, &SysTray::OnSettingsClicked);
    connect(_actionAbout, &QAction::triggered, this, &SysTray::OnAboutClicked);
    connect(_actionQuit, &QAction::triggered, qApp, &QApplication::quit, Qt::QueuedConnection);
    connect(_tray, &QSystemTrayIcon::activated, this, &SysTray::OnIconClicked);
    connect(_tray, &QSystemTrayIcon::messageClicked, this, [this]() { ShowInfoWindow(); });

    _menu->addAction(_actionSettings);
    _menu->addSeparator();
    _menu->addAction(_actionAbout);
    _menu->addAction(_actionQuit);

    _tray->setContextMenu(_menu);
    _tray->setIcon(ApdApplication::windowIcon());
    _tray->show();

    if (ApdApplication::IsFirstTimeUse()) {
        _tray->showMessage(
            tr("You can find me in the system tray"),
            tr("Click the icon to view battery information, right-click to "
               "customize settings or quit."));
    }
}

void SysTray::UpdateState(const Core::AirPods::State &state)
{
    QString toolTip;

    // toolTip += Helper::ToString(state.model);
    toolTip += Core::AirPods::GetDisplayName();

    // clang-format off

    if (state.pods.left.battery.has_value()) {
        toolTip += QString{tr("\nLeft: %1%%2")}
            .arg(state.pods.left.battery.value())
            .arg(state.pods.left.isCharging ? tr(" (charging)") : "");
    }

    if (state.pods.right.battery.has_value()) {
        toolTip += QString{tr("\nRight: %1%%2")}
            .arg(state.pods.right.battery.value())
            .arg(state.pods.right.isCharging ? tr(" (charging)") : "");
    }

    if (state.caseBox.battery.has_value()) {
        toolTip += QString{tr("\nCase: %1%%2")}
            .arg(state.caseBox.battery.value())
            .arg(state.caseBox.isCharging ? tr(" (charging)") : "");
    }

    // clang-format on

    _tray->setToolTip(toolTip);
}

void SysTray::Unavailable()
{
    _tray->setToolTip(tr("Unavailable"));
}

void SysTray::Disconnect()
{
    _tray->setToolTip(tr("Disconnected"));
}

void SysTray::Unbind()
{
    _tray->setToolTip(tr("Waiting for Binding"));
}

void SysTray::ShowInfoWindow()
{
    ApdApp->GetInfoWindow()->show();
}

void SysTray::OnSettingsClicked()
{
    _settingsWindow.show();
}

void SysTray::OnAboutClicked()
{
    ApdApplication::PopupAboutWindow(this);
}

void SysTray::OnIconClicked(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger ||
        reason == QSystemTrayIcon::MiddleClick)
    {
        ShowInfoWindow();
    }
}
} // namespace Gui
