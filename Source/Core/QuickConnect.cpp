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

#include "../Assert.h"

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

Controller::Controller(std::shared_ptr<Backend> backend, std::chrono::milliseconds resolveTimeout)
    : QObject{nullptr}, _backend{std::move(backend)}
{
    APD_ASSERT(_backend != nullptr);

    _link->controller = this;
    _backend->SetController(this);

    _resolveTimer.setSingleShot(true);
    _resolveTimer.setInterval(resolveTimeout);
    QObject::connect(&_resolveTimer, &QTimer::timeout, this, &Controller::ResolveTimedOut);
}

Controller::~Controller()
{
    // Detach the workers from this object first, so anything still running posts nowhere.
    {
        std::lock_guard<std::mutex> lock{_link->mutex};
        _link->controller = nullptr;
    }

    _deviceRefreshThread.request_stop();
    _requestStop.request_stop();

    // The refresh worker cancels promptly - WinRT enumeration takes a stop callback and the
    // endpoint walk checks between items - so waiting for it costs nothing.
    if (_deviceRefreshThread.joinable()) {
        _deviceRefreshThread.join();
    }

    // The reconnect worker can be inside a blocking KsProperty call that has no cancellation point,
    // and the driver decides when that returns. Joining it here would freeze the GUI thread on
    // quit, so let it finish on its own; it holds a shared reference to the backend and posts
    // through `_link`, so it cannot outlive anything it touches.
    if (_requestThread.joinable()) {
        _requestThread.detach();
    }

    _backend->SetController(nullptr);
}

void Controller::PostToController(
    const std::shared_ptr<Link> &link, std::function<void(Controller &)> work)
{
    std::lock_guard<std::mutex> lock{link->mutex};
    if (link->controller == nullptr) {
        return;
    }

    QMetaObject::invokeMethod(
        link->controller,
        [controller = link->controller, work = std::move(work)]() { work(*controller); },
        Qt::QueuedConnection);
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

QString Controller::DeviceNameFor(const QString &id) const
{
    const auto iter = std::find_if(_devices.begin(), _devices.end(), [&](const Device &device) {
        return NormalizeId(device.id) == NormalizeId(id);
    });
    return iter != _devices.end() ? iter->name : QString{};
}

void Controller::RefreshDevices()
{
    if (_deviceRefreshRunning.exchange(true)) {
        return;
    }

    if (_deviceRefreshThread.joinable()) {
        _deviceRefreshThread.join();
    }

    _deviceRefreshThread =
        std::jthread{[backend = _backend, link = _link](std::stop_token stopToken) {
            auto devices = backend->ListDevices(stopToken);
            if (stopToken.stop_requested()) {
                return;
            }

            PostToController(link, [devices = std::move(devices)](Controller &self) mutable {
                self._devices = std::move(devices);
                self._deviceRefreshRunning = false;
                emit self.DevicesChanged();
            });
        }};
}

Outcome Controller::Request()
{
    // A click while work is already under way always answers with the same outcome, whether that
    // work is the enumeration/reconnect round trip or the wait for the endpoint to come up. The
    // caller decides how loudly to report it.
    if (_pending) {
        EmitOutcome(Outcome::RequestStarted, _pendingDeviceName);
        return Outcome::RequestStarted;
    }

    if (_requestRunning) {
        EmitOutcome(Outcome::RequestStarted, DeviceNameFor(_requestingDeviceId));
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

    _requestStop = std::stop_source{};

    _requestThread =
        std::thread{[backend = _backend, link = _link, stopToken = _requestStop.get_token(),
                     requestedDeviceId = _deviceId]() {
            const auto devices = backend->ListDevices(stopToken);
            const auto deviceIter =
                std::find_if(devices.begin(), devices.end(), [&](const Device &device) {
                    return device.id == requestedDeviceId;
                });

            auto outcome = Outcome::DeviceUnavailable;
            QString deviceName;
            if (deviceIter != devices.end()) {
                deviceName = deviceIter->name;
                if (deviceIter->connected) {
                    outcome = Outcome::AlreadyConnected;
                }
                else {
                    outcome = backend->RequestReconnect(requestedDeviceId, stopToken)
                                  ? Outcome::RequestStarted
                                  : Outcome::Failed;
                }
            }

            if (stopToken.stop_requested()) {
                return;
            }

            PostToController(
                link, [requestedDeviceId, deviceName = std::move(deviceName),
                       outcome](Controller &self) mutable {
                    self._requestRunning = false;
                    self._requestingDeviceId.clear();

                    if (outcome == Outcome::RequestStarted) {
                        if (self._connectedDuringRequest) {
                            self._connectedDuringRequest = false;
                            self._backend->StopObserving();
                            self.EmitOutcome(Outcome::Connected, deviceName);
                            return;
                        }

                        self._pending = true;
                        self._pendingDeviceId = requestedDeviceId;
                        self._pendingDeviceName = deviceName;
                        self._resolveTimer.start();
                    }

                    self._connectedDuringRequest = false;
                    self.EmitOutcome(outcome, deviceName);
                });
        }};

    return Outcome::RequestStarted;
}

Outcome Controller::OnEndpointStateChanged(const QString &id, bool connected)
{
    if (_requestRunning && connected && NormalizeId(id) == NormalizeId(_requestingDeviceId)) {
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

    // Nothing is waiting on endpoint notifications any more; let the backend disarm whatever it
    // armed, so it stops resolving unrelated device changes for the rest of the process lifetime.
    _backend->StopObserving();

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
