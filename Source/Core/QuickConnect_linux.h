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

#include <memory>
#include <optional>
#include <vector>

#include <QDBusMessage>
#include <QString>
#include <QVariantMap>

#include "QuickConnect.h"

namespace Core::QuickConnect {

namespace Details {

struct BluezDeviceInfo {
    QString path;
    QString id;
    QString name;
    bool connected{};
};

// Turns the ObjectManager tree returned by Core::Bluez::GetManagedObjects() into the paired
// devices that can play audio. Exposed for tests; the backend is otherwise the only caller.
std::vector<BluezDeviceInfo> ParseManagedObjects(const QVariantMap &objects);

// The object path of the device whose BlueZ Address equals `id`, or nothing if it is not there.
std::optional<QString> FindObjectPathById(const QVariantMap &objects, const QString &id);

QString MaskDeviceId(const QString &id);

// Injectable transport lets tests exercise BlueZ method selection without a real adapter.
struct Transport {
    std::function<QVariantMap()> managedObjects;
    std::function<QDBusMessage(const QDBusMessage &)> call;
};

} // namespace Details

// Lists paired audio devices through the BlueZ ObjectManager and reconnects one by calling
// Device1.Connect, watching its PropertiesChanged for the link to come up.
std::shared_ptr<Backend> CreateLinuxBackend();
std::shared_ptr<Backend> CreateLinuxBackend(Details::Transport transport);

} // namespace Core::QuickConnect
