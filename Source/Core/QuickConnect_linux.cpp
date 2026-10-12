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

#include "QuickConnect_linux.h"

#include <QMetaObject>
#include <QStringList>

#include "OS/BlueZ.h"
#include "../Logger.h"

namespace Core::QuickConnect {
namespace {

namespace BlueZ = OS::BlueZ;

// A2DP sink, Headset, Hands-Free: what makes a device an audio endpoint.
bool HasAudioProfile(const QVariantMap &properties)
{
    static const QStringList audioUuids{
        "0000110b-0000-1000-8000-00805f9b34fb",
        "00001108-0000-1000-8000-00805f9b34fb",
        "0000111e-0000-1000-8000-00805f9b34fb",
    };

    for (const auto &uuid : properties.value("UUIDs").toStringList()) {
        if (audioUuids.contains(uuid.toLower())) {
            return true;
        }
    }
    return false;
}

QString IdOf(const BlueZ::DeviceInfo &info)
{
    const auto address = BlueZ::ParseAddress(info.properties.value("Address").toString());
    return address ? BlueZ::FormatAddress(*address) : QString{};
}

} // namespace

void LinuxBackend::SetController(Controller *controller)
{
    std::lock_guard<std::mutex> lock{_controllerMutex};
    _controller = controller;
}

std::vector<Device> LinuxBackend::ListDevices(std::stop_token stopToken)
{
    std::vector<Device> devices;

    for (const auto &info : BlueZ::GetDevices()) {
        if (stopToken.stop_requested()) {
            return {};
        }
        if (!info.properties.value("Paired").toBool() || !HasAudioProfile(info.properties)) {
            continue;
        }

        const auto id = IdOf(info);
        if (id.isEmpty()) {
            continue;
        }

        auto name = info.properties.value("Alias").toString();
        if (name.isEmpty()) {
            name = info.properties.value("Name").toString();
        }
        devices.push_back(Device{id, name, info.properties.value("Connected").toBool()});
    }
    return devices;
}

bool LinuxBackend::RequestReconnect(const QString &id, std::stop_token stopToken)
{
    QString path;
    for (const auto &info : BlueZ::GetDevices()) {
        if (IdOf(info).compare(id, Qt::CaseInsensitive) == 0) {
            path = info.path;
            break;
        }
    }
    if (path.isEmpty() || stopToken.stop_requested()) {
        return false;
    }

    // Blocks until BlueZ has brought the audio profiles up (or given up).
    const auto error = BlueZ::CallMethod(path, BlueZ::kDeviceInterface, "Connect", 30000);
    if (!error.isEmpty()) {
        LOG(Warn, "BlueZ: Connect failed: '{}'", error);
        return false;
    }

    // BlueZ only answers once the device is connected; tell the Controller before the worker
    // reports "request accepted" so it resolves straight to "connected".
    QueueEndpointState(id, true);
    return true;
}

void LinuxBackend::QueueEndpointState(const QString &id, bool connected)
{
    std::lock_guard<std::mutex> lock{_controllerMutex};
    if (_controller == nullptr) {
        return;
    }

    QMetaObject::invokeMethod(
        _controller,
        [controller = _controller, id, connected]() {
            controller->OnEndpointStateChanged(id, connected);
        },
        Qt::QueuedConnection);
}

} // namespace Core::QuickConnect
