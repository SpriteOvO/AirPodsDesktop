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

#include <QLocale>
#include <QVector>

class QString;

namespace Gui {

class MainWindow;
class TrayIcon;
class TaskbarStatus;

//
// Services that belong to the application layer but are queried by GUI
// components. Implemented by ApdApplication.
//
class AppServices
{
public:
    virtual ~AppServices() = default;

    virtual int GetCurrentLoadedLocaleIndex() const = 0;
};

void ProvideContext(
    MainWindow *mainWindow, TrayIcon *trayIcon, TaskbarStatus *taskbarStatus,
    AppServices *services);

MainWindow *GetMainWindow();
TrayIcon *GetTrayIcon();
TaskbarStatus *GetTaskbarStatus();
AppServices *GetAppServices();

} // namespace Gui
