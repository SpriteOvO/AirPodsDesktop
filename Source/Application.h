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

#include <memory>
#include <SingleApplication>

#include <QLocale>
#include <QTranslator>

#include "Gui/GuiContext.h"
#include "Core/Settings.h"
#include "Opts.h"

namespace Core::AirPods {
class Manager;
}

namespace Core::LowAudioLatency {
class Controller;
}

namespace Gui {
class DownloadWindow;
class MainWindow;
class TaskbarStatus;
class TrayIcon;
}

class ApdApplication :
    public SingleApplication,
    private Core::Settings::ApplyObserver,
    public Gui::AppServices
{
    Q_OBJECT

public:
    static void PreConstruction();
    ApdApplication(int argc, char *argv[]);
    ~ApdApplication() override;

    bool Prepare(int argc, char *argv[]);
    int Run();

    inline auto &GetTrayIcon()
    {
        return _trayIcon;
    }
    inline auto &GetTaskbarStatus()
    {
        return _taskbarStatus;
    }
    inline auto &GetMainWindow()
    {
        return _mainWindow;
    }
    inline auto &GetLowAudioLatencyController()
    {
        return _lowAudioLatencyController;
    }

    int GetCurrentLoadedLocaleIndex() const override
    {
        return _currentLoadedLocaleIndex;
    }

    static void QuitSafely();

Q_SIGNALS:
    void SetTranslatorSafely(const QLocale &locale);

private:
    static inline Opts::LaunchOptsManager _launchOptsMgr;
    QTranslator _translator;
    int _currentLoadedLocaleIndex{0};
    std::unique_ptr<Gui::TrayIcon> _trayIcon;
    std::unique_ptr<Gui::TaskbarStatus> _taskbarStatus;
    std::unique_ptr<Gui::MainWindow> _mainWindow;
    std::unique_ptr<Gui::DownloadWindow> _downloadWindow;
    std::unique_ptr<Core::AirPods::Manager> _airPodsManager;
    std::unique_ptr<Core::LowAudioLatency::Controller> _lowAudioLatencyController;

    void InitSettings(Core::Settings::LoadResult loadResult);
    void FirstTimeUse();

    void ConnectAirPodsManager();

    void SetTranslator(const QLocale &locale);
    void InitTranslator();

    void OnLanguageLocaleChanged(const QLocale &locale) override;
    void OnLowAudioLatencyChanged(bool enable) override;
    void OnAutomaticEarDetectionChanged(bool enable) override;
    void OnRssiMinChanged(int16_t rssiMin) override;
    void OnDeviceAddressChanged(uint64_t address) override;
    void OnTrayIconBatteryChanged(Core::Settings::TrayIconBatteryBehavior behavior) override;
    void OnTaskbarBatteryChanged(Core::Settings::TaskbarStatusBehavior behavior) override;
};

#define ApdApp (dynamic_cast<ApdApplication *>(QCoreApplication::instance()))
