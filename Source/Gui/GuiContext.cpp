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

#include "GuiContext.h"

#include "MainWindow.h"
#include "TaskbarStatus.h"
#include "TrayIcon.h"

namespace Gui {

namespace {

MainWindow *_mainWindow = nullptr;
TrayIcon *_trayIcon = nullptr;
TaskbarStatus *_taskbarStatus = nullptr;
AppServices *_services = nullptr;

} // namespace

void ProvideContext(
    MainWindow *mainWindow, TrayIcon *trayIcon, TaskbarStatus *taskbarStatus,
    AppServices *services)
{
    _mainWindow = mainWindow;
    _trayIcon = trayIcon;
    _taskbarStatus = taskbarStatus;
    _services = services;
}

MainWindow *GetMainWindow()
{
    return _mainWindow;
}

TrayIcon *GetTrayIcon()
{
    return _trayIcon;
}

TaskbarStatus *GetTaskbarStatus()
{
    return _taskbarStatus;
}

AppServices *GetAppServices()
{
    return _services;
}

} // namespace Gui
