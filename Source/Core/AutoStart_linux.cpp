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

#include "AutoStart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <Config.h>
#include "../Logger.h"

namespace Core::AutoStart {

namespace {

// Freedesktop autostart: a .desktop file in $XDG_CONFIG_HOME/autostart. Every mainstream desktop
// (GNOME, KDE, Xfce, Cinnamon, MATE...) honours it.
class XdgService final : public Service
{
public:
    void SetEnabled(bool enabled) override
    {
        const auto path = EntryPath();

        if (!enabled) {
            if (QFile::exists(path) && !QFile::remove(path)) {
                LOG(Warn, "AutoStart: cannot remove '{}'", path);
            }
            return;
        }

        QDir{}.mkpath(QFileInfo{path}.absolutePath());

        QSaveFile file{path};
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            LOG(Warn, "AutoStart: cannot write '{}': {}", path, file.errorString());
            return;
        }

        QTextStream out{&file};
        out << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=AirPods Desktop\n"
            << "Comment=" << Config::Description << "\n"
            << "Exec=" << QuoteExec(QCoreApplication::applicationFilePath()) << "\n"
            << "Icon=AirPodsDesktop\n"
            << "Terminal=false\n"
            << "X-GNOME-Autostart-enabled=true\n";
        out.flush();

        if (!file.commit()) {
            LOG(Warn, "AutoStart: cannot save '{}': {}", path, file.errorString());
        }
    }

private:
    static QString EntryPath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
               "/autostart/" + Config::ProgramName + ".desktop";
    }

    // Desktop Entry spec, "The Exec key": quote the whole argument when it has special characters
    // and backslash-escape the characters that stay special inside quotes.
    static QString QuoteExec(QString argument)
    {
        argument.replace("%", "%%");

        static const QString special = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");
        bool needsQuotes = false;
        for (const auto ch : argument) {
            if (special.contains(ch)) {
                needsQuotes = true;
                break;
            }
        }
        if (!needsQuotes) {
            return argument;
        }

        QString quoted;
        for (const auto ch : argument) {
            if (QStringLiteral("\"`$\\").contains(ch)) {
                quoted += '\\';
            }
            quoted += ch;
        }
        return '"' + quoted + '"';
    }
};

} // namespace

std::unique_ptr<Service> CreateAutoStartService()
{
    return std::make_unique<XdgService>();
}

} // namespace Core::AutoStart
