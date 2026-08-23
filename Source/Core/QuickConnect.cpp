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
namespace {

QString NormalizeId(QString id)
{
    id = id.trimmed();
    if (id.startsWith('{') && id.endsWith('}')) {
        id = id.mid(1, id.size() - 2);
    }
    return id.toLower();
}

} // namespace

void Backend::SetController(Controller *controller)
{
    Q_UNUSED(controller)
}

Controller::Controller(Backend &backend) : QObject{nullptr}, _backend{backend}
{
    _backend.SetController(this);

    _resolveTimer.setSingleShot(true);
    _resolveTimer.setInterval(10'000);
    QObject::connect(&_resolveTimer, &QTimer::timeout, this, &Controller::ResolveTimedOut);
}

Controller::~Controller()
{
    _backend.SetController(nullptr);
}

void Controller::SetEnabled(bool enabled)
{
    _enabled = enabled;
}

bool Controller::IsEnabled() const
{
    return _enabled;
}

void Controller::SetDeviceId(QString id)
{
    _deviceId = std::move(id);
}

std::vector<Device> Controller::Devices()
{
    return _backend.ListDevices();
}

Outcome Controller::Request()
{
    if (_pending) {
        EmitOutcome(Outcome::RequestStarted, _pendingDeviceName);
        return Outcome::RequestStarted;
    }

    if (!_enabled) {
        EmitOutcome(Outcome::Disabled, {});
        return Outcome::Disabled;
    }

    if (_deviceId.isEmpty()) {
        EmitOutcome(Outcome::NoDevice, {});
        return Outcome::NoDevice;
    }

    const auto devices = _backend.ListDevices();
    const auto deviceIter = std::find_if(
        devices.begin(), devices.end(),
        [&](const Device &device) { return device.id == _deviceId; });
    if (deviceIter == devices.end()) {
        EmitOutcome(Outcome::NoDevice, {});
        return Outcome::NoDevice;
    }

    if (deviceIter->connected) {
        EmitOutcome(Outcome::AlreadyConnected, deviceIter->name);
        return Outcome::AlreadyConnected;
    }

    if (!_backend.RequestReconnect(_deviceId)) {
        EmitOutcome(Outcome::Failed, deviceIter->name);
        return Outcome::Failed;
    }

    _pending = true;
    _pendingDeviceId = _deviceId;
    _pendingDeviceName = deviceIter->name;
    _resolveTimer.start();
    EmitOutcome(Outcome::RequestStarted, _pendingDeviceName);
    return Outcome::RequestStarted;
}

Outcome Controller::OnEndpointStateChanged(const QString &id, bool connected)
{
    if (!_pending) {
        return connected ? Outcome::Connected : Outcome::RequestStarted;
    }

    if (NormalizeId(id) != NormalizeId(_pendingDeviceId) || !connected) {
        return Outcome::RequestStarted;
    }

    return Complete(Outcome::Connected);
}

Outcome Controller::ResolveTimedOut()
{
    if (!_pending) {
        return Outcome::TimedOut;
    }

    return Complete(Outcome::TimedOut);
}

Outcome Controller::Complete(Outcome outcome)
{
    _resolveTimer.stop();
    _pending = false;

    const auto deviceName = _pendingDeviceName;
    _pendingDeviceId.clear();
    _pendingDeviceName.clear();
    EmitOutcome(outcome, deviceName);

    return outcome;
}

void Controller::EmitOutcome(Outcome outcome, const QString &deviceName)
{
    emit OutcomeChanged(outcome, deviceName);
}

} // namespace Core::QuickConnect
