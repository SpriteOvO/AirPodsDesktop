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

#include <QTranslator>

#include "Gui/TrayIcon.h"
#include "Gui/MainWindow.h"
#include "Gui/DownloadWindow.h"
#include "Core/AirPods.h"
#include "Core/LowAudioLatency.h"
#include "Opts.h"

class ApdApplication : public SingleApplication
{
    Q_OBJECT

public:
    static void PreConstruction();
    ApdApplication(int argc, char *argv[]);

    bool Prepare(int argc, char *argv[]);
    int Run();

    inline auto &GetTrayIcon()
    {
        return _trayIcon;
    }
    inline auto &GetMainWindow()
    {
        return _mainWindow;
    }
    inline auto &GetLowAudioLatencyController()
    {
        return _lowAudioLatencyController;
    }

    inline auto GetCurrentLoadedLocaleIndex()
    {
        return _currentLoadedLocaleIndex;
    }

    const QVector<QLocale> &AvailableLocales();

    static void QuitSafely();

Q_SIGNALS:
    void SetTranslatorSafely(const QLocale &locale);

private:
    static inline Opts::LaunchOptsManager _launchOptsMgr;
    QTranslator _translator;
    int _currentLoadedLocaleIndex{0};
    std::unique_ptr<Gui::TrayIcon> _trayIcon;
    std::unique_ptr<Gui::MainWindow> _mainWindow;
    std::unique_ptr<Gui::DownloadWindow> _downloadWindow;
    std::unique_ptr<Core::LowAudioLatency::Controller> _lowAudioLatencyController;

    void InitSettings(Core::Settings::LoadResult loadResult);
    void FirstTimeUse();

    void SetTranslator(const QLocale &locale);
    void InitTranslator();
};

#define ApdApp (dynamic_cast<ApdApplication *>(QCoreApplication::instance()))
