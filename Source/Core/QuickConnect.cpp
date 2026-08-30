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

#include <QMetaObject>

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
    _deviceRefreshThread.request_stop();
    _requestThread.request_stop();
    if (_deviceRefreshThread.joinable()) {
        _deviceRefreshThread.join();
    }
    if (_requestThread.joinable()) {
        _requestThread.join();
    }
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
    return _devices;
}

void Controller::RefreshDevices()
{
    if (_deviceRefreshRunning.exchange(true)) {
        return;
    }

    if (_deviceRefreshThread.joinable()) {
        _deviceRefreshThread.join();
    }

    _deviceRefreshThread = std::jthread{[this](std::stop_token stopToken) {
        auto devices = _backend.ListDevices(stopToken);
        if (stopToken.stop_requested()) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, devices = std::move(devices)]() mutable {
                _devices = std::move(devices);
                _deviceRefreshRunning = false;
                emit DevicesChanged();
            },
            Qt::QueuedConnection);
    }};
}

Outcome Controller::Request()
{
    if (_pending) {
        EmitOutcome(Outcome::RequestStarted, _pendingDeviceName);
        return Outcome::RequestStarted;
    }

    if (_requestRunning) {
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

    _requestRunning = true;
    _requestingDeviceId = _deviceId;
    _connectedDuringRequest = false;

    if (_requestThread.joinable()) {
        _requestThread.join();
    }

    const auto requestedDeviceId = _deviceId;
    _requestThread = std::jthread{[this, requestedDeviceId](std::stop_token stopToken) {
        const auto devices = _backend.ListDevices(stopToken);
        const auto deviceIter = std::find_if(
            devices.begin(), devices.end(),
            [&](const Device &device) { return device.id == requestedDeviceId; });

        auto outcome = Outcome::NoDevice;
        QString deviceName;
        if (deviceIter != devices.end()) {
            deviceName = deviceIter->name;
            if (deviceIter->connected) {
                outcome = Outcome::AlreadyConnected;
            }
            else {
                outcome = _backend.RequestReconnect(requestedDeviceId, stopToken)
                              ? Outcome::RequestStarted
                              : Outcome::Failed;
            }
        }

        if (stopToken.stop_requested()) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, requestedDeviceId, deviceName = std::move(deviceName), outcome]() mutable {
                _requestRunning = false;
                _requestingDeviceId.clear();

                if (outcome == Outcome::RequestStarted) {
                    if (_connectedDuringRequest) {
                        _connectedDuringRequest = false;
                        EmitOutcome(Outcome::Connected, deviceName);
                        return;
                    }

                    _pending = true;
                    _pendingDeviceId = requestedDeviceId;
                    _pendingDeviceName = deviceName;
                    _resolveTimer.start();
                }

                _connectedDuringRequest = false;
                EmitOutcome(outcome, deviceName);
            },
            Qt::QueuedConnection);
    }};

    return Outcome::RequestStarted;
}

Outcome Controller::OnEndpointStateChanged(const QString &id, bool connected)
{
    if (_requestRunning && connected &&
        NormalizeId(id) == NormalizeId(_requestingDeviceId))
    {
        _connectedDuringRequest = true;
        return Outcome::RequestStarted;
    }

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
