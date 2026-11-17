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

#if defined APD_OS_LINUX

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

#include <Config.h>

#include "../Logger.h"

namespace Core::AutoStart {

namespace {

class XdgService final : public Service
{
public:
    void SetEnabled(bool enabled) override
    {
        const auto autostartDir =
            QDir{QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)}
                .filePath("autostart");
        const auto path =
            QDir{autostartDir}.filePath(QString{Config::ProgramName} + QStringLiteral(".desktop"));

        if (!enabled) {
            QFile::remove(path);
            return;
        }

        QDir{}.mkpath(autostartDir);

        QFile file{path};
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            LOG(Warn, "Open autostart entry '{}' for writing failed.", path.toStdString());
            return;
        }

        QTextStream stream{&file};
        stream << "[Desktop Entry]\n"
               << "Type=Application\n"
               << "Name=" << Config::ProgramName << "\n"
               << "Exec=\"" << QCoreApplication::applicationFilePath() << "\"\n"
               << "Terminal=false\n"
               << "X-GNOME-Autostart-enabled=true\n";
    }
};

} // namespace

std::unique_ptr<Service> CreateAutoStartService()
{
    return std::make_unique<XdgService>();
}

} // namespace Core::AutoStart

#endif
