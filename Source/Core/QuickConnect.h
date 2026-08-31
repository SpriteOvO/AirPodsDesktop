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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#include <QObject>
#include <QMetaType>
#include <QString>
#include <QTimer>

namespace Core::QuickConnect {

class Controller;

enum class Outcome : uint32_t {
    Disabled,
    NoDevice,          // nothing is configured yet
    DeviceUnavailable, // configured, but it is not reporting an audio endpoint right now
    AlreadyConnected,
    RequestStarted,
    Failed,
    Connected,
    TimedOut
};

struct Device {
    QString id;
    QString name;
    bool connected{};
};

class Backend
{
public:
    virtual ~Backend() = default;

    virtual void SetController(Controller *controller);
    virtual std::vector<Device> ListDevices(std::stop_token stopToken) = 0;
    virtual bool RequestReconnect(const QString &id, std::stop_token stopToken) = 0;

    // Called once the Controller stops caring about endpoint-state changes for the device it last
    // asked to reconnect. A backend that arms system notifications must disarm them here, otherwise
    // it keeps doing that work for every device on the machine until the process exits.
    virtual void StopObserving() {}
};

class NullBackend final : public Backend
{
public:
    std::vector<Device> ListDevices(std::stop_token) override
    {
        return {};
    }
    bool RequestReconnect(const QString &, std::stop_token) override
    {
        return false;
    }
};

class Controller final : public QObject
{
    Q_OBJECT

public:
    // How long to wait for the endpoint to come up after Windows accepted the reconnect request.
    static constexpr std::chrono::milliseconds kDefaultResolveTimeout{10'000};

    // The backend is shared rather than borrowed because a reconnect request can still be inside a
    // blocking driver call when the Controller goes away; that worker is detached and keeps the
    // backend alive for as long as it needs it.
    //
    // `resolveTimeout` is a parameter so tests can drive the real timer instead of calling the
    // timeout slot by hand.
    explicit Controller(
        std::shared_ptr<Backend> backend,
        std::chrono::milliseconds resolveTimeout = kDefaultResolveTimeout);
    ~Controller() override;

    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void SetDeviceId(QString id);
    std::vector<Device> Devices();
    void RefreshDevices();
    Outcome Request();
    Outcome OnEndpointStateChanged(const QString &id, bool connected);
    Outcome ResolveTimedOut();

signals:
    void DevicesChanged();
    void OutcomeChanged(Core::QuickConnect::Outcome outcome, QString deviceName);

private:
    // Lets a worker post back without ever dereferencing a Controller that has started tearing
    // down. `~Controller` clears the pointer under the mutex before anything else happens, so a
    // worker either posts to a live Controller or finds nothing and gives up.
    struct Link {
        std::mutex mutex;
        Controller *controller{};
    };

    std::shared_ptr<Backend> _backend;
    std::shared_ptr<Link> _link{std::make_shared<Link>()};
    bool _enabled{};
    bool _pending{};
    QString _deviceId;
    QString _pendingDeviceId;
    QString _pendingDeviceName;
    QString _requestingDeviceId;
    bool _connectedDuringRequest{};
    std::vector<Device> _devices;
    std::atomic<bool> _deviceRefreshRunning{};
    std::atomic<bool> _requestRunning{};
    std::jthread _deviceRefreshThread;
    std::stop_source _requestStop;
    std::thread _requestThread;
    QTimer _resolveTimer;

    static void
    PostToController(const std::shared_ptr<Link> &link, std::function<void(Controller &)> work);

    QString DeviceNameFor(const QString &id) const;
    Outcome Complete(Outcome outcome);
    void EmitOutcome(Outcome outcome, const QString &deviceName);
};

} // namespace Core::QuickConnect

Q_DECLARE_METATYPE(Core::QuickConnect::Outcome)
