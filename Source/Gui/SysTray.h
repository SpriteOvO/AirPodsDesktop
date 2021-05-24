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

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>

#include <Config.h>
#include "../Core/AirPods.h"
#include "SettingsWindow.h"


namespace Gui
{
    class SysTray : public QWidget
    {
        Q_OBJECT

    public:
        SysTray();

    Q_SIGNALS:
        void UpdateStateSafety(const Core::AirPods::State &state);
        void DisconnectSafety(const QString &title);

    private:
        QSystemTrayIcon *_tray = new QSystemTrayIcon{this};
        QMenu *_menu = new QMenu{this};

        QAction *_actionSettings = new QAction{tr("Settings"), this};
        QAction *_actionAbout = new QAction{tr("About"), this};
        QAction *_actionQuit = new QAction{tr("Quit"), this};

        void UpdateState(const Core::AirPods::State &state);
        void Disconnect(const QString &title);

        void ShowInfoWindow();

        void OnSettingsClicked();
        void OnAboutClicked();
        void OnIconClicked(QSystemTrayIcon::ActivationReason reason);

    protected:
        Core::AirPods::State _state{};

        SettingsWindow _settingsWindow{this};
    };

} // namespace Gui
