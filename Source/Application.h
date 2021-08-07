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

#include <memory>
#include <cxxopts.hpp>
#include <SingleApplication>

#include "Gui/SysTray.h"
#include "Gui/InfoWindow.h"
#include "Gui/DownloadWindow.h"
#include "Core/AirPods.h"

class ApdApplication : public SingleApplication
{
private:
    struct LaunchOptions {
        bool enableTrace{false};
    };

public:
    static bool PreInitialize(int argc, char *argv[]);
    ApdApplication(int argc, char *argv[]);

    bool Prepare();
    int Run();

    inline auto &GetSysTray()
    {
        return _sysTray;
    }
    inline auto &GetInfoWindow()
    {
        return _infoWindow;
    }

    inline static bool IsFirstTimeUse()
    {
        return _isFirstTimeUse;
    }

    static void PopupAboutWindow(QWidget *parent);
    static void QuitSafety();

private:
    static inline LaunchOptions _launchOptions;
    static inline bool _isFirstTimeUse{false};
    std::unique_ptr<Gui::SysTray> _sysTray;
    std::unique_ptr<Gui::InfoWindow> _infoWindow;
    std::unique_ptr<Gui::DownloadWindow> _downloadWindow;

    static void InitSettings();
    static void FirstTimeUse();

    // Returns false if the program will quit
    //
    bool CheckUpdate();

    void QuitHandler();
};

#define ApdApp (dynamic_cast<ApdApplication *>(QCoreApplication::instance()))
