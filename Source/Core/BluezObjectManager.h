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

#if !defined APD_OS_LINUX
    #error "This file shouldn't be compiled."
#endif

#include <QVariantMap>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>

namespace Core::Bluez {

inline constexpr auto kService = "org.bluez";
inline constexpr auto kDeviceInterface = "org.bluez.Device1";

// QtDBus demarshals the nested a{sa{sv}} values into opaque raw types when the whole argument is
// read into a QVariantMap, so the map is walked by hand to get real properties.
inline QVariantMap GetManagedObjects()
{
    const auto message = QDBusMessage::createMethodCall(
        kService, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    const auto reply = QDBusConnection::systemBus().call(message, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }

    const auto arg = reply.arguments().at(0).value<QDBusArgument>();
    QVariantMap objects;
    arg.beginMap();
    while (!arg.atEnd()) {
        arg.beginMapEntry();
        QString path;
        arg >> path;

        QVariantMap interfaces;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            QString interface;
            arg >> interface;

            QVariantMap properties;
            arg.beginMap();
            while (!arg.atEnd()) {
                arg.beginMapEntry();
                QString name;
                QVariant value;
                arg >> name >> value;
                arg.endMapEntry();
                properties.insert(name, value);
            }
            arg.endMap();
            interfaces.insert(interface, properties);
            arg.endMapEntry();
        }
        arg.endMap();

        objects.insert(path, interfaces);
        arg.endMapEntry();
    }
    arg.endMap();
    return objects;
}

} // namespace Core::Bluez
