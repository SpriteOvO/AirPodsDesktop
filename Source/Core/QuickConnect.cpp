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

#include "QuickConnect.h"

#include <algorithm>
#include <utility>

namespace Core::QuickConnect {

Controller::Controller(Backend &backend) : QObject{nullptr}, _backend{backend}
{
}

void Controller::SetEnabled(bool enabled)
{
    _enabled = enabled;
}

void Controller::SetDeviceId(QString id)
{
    _deviceId = std::move(id);
}

Outcome Controller::Request()
{
    if (!_enabled) {
        return Outcome::Disabled;
    }

    if (_deviceId.isEmpty()) {
        return Outcome::NoDevice;
    }

    const auto devices = _backend.ListDevices();
    const auto deviceIter = std::find_if(
        devices.begin(), devices.end(),
        [&](const Device &device) { return device.id == _deviceId; });
    if (deviceIter == devices.end()) {
        return Outcome::NoDevice;
    }

    if (deviceIter->connected) {
        return Outcome::AlreadyConnected;
    }

    if (_pending) {
        return Outcome::RequestStarted;
    }

    if (!_backend.RequestReconnect(_deviceId)) {
        return Outcome::Failed;
    }

    _pending = true;
    return Outcome::RequestStarted;
}

} // namespace Core::QuickConnect
