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

#include <cstdint>
#include <vector>

#include <QObject>
#include <QMetaType>
#include <QString>
#include <QTimer>

namespace Core::QuickConnect {

class Controller;

enum class Outcome : uint32_t {
    Disabled,
    NoDevice,
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
    virtual std::vector<Device> ListDevices() = 0;
    virtual bool RequestReconnect(const QString &id) = 0;
};

class Controller final : public QObject
{
    Q_OBJECT

public:
    explicit Controller(Backend &backend);

    void SetEnabled(bool enabled);
    void SetDeviceId(QString id);
    Outcome Request();
    Outcome OnEndpointStateChanged(const QString &id, bool connected);
    Outcome ResolveTimedOut();

signals:
    void OutcomeChanged(Core::QuickConnect::Outcome outcome, QString deviceName);

private:
    Backend &_backend;
    bool _enabled{};
    bool _pending{};
    QString _deviceId;
    QString _pendingDeviceName;
    QTimer _resolveTimer;

    Outcome Complete(Outcome outcome);
    void EmitOutcome(Outcome outcome, const QString &deviceName);
};

} // namespace Core::QuickConnect

Q_DECLARE_METATYPE(Core::QuickConnect::Outcome)
