//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "AutoStart.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#include <Config.h>

namespace Core::AutoStart {

namespace {

class WindowsService final : public Service
{
public:
    void SetEnabled(bool enabled) override
    {
        QSettings registry{
            "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
            QSettings::Registry64Format};
        const auto executable = QString{"\"%1\""}.arg(
            QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));

        if (enabled) {
            registry.setValue(Config::ProgramName, executable);
        }
        else {
            registry.remove(Config::ProgramName);
        }
    }
};

} // namespace

std::unique_ptr<Service> CreateAutoStartService()
{
    return std::make_unique<WindowsService>();
}

} // namespace Core::AutoStart
