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

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>
#include <QString>
#include <QVariantMap>

// Thin helpers over BlueZ's D-Bus API (org.bluez), shared by the Bluetooth and QuickConnect
// backends. The calls are synchronous and safe from any thread; signals are delivered by `Monitor`
// on the GUI thread.
//
namespace Core::OS::BlueZ {

constexpr auto kService = "org.bluez";
constexpr auto kAdapterInterface = "org.bluez.Adapter1";
constexpr auto kDeviceInterface = "org.bluez.Device1";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto kObjectManagerInterface = "org.freedesktop.DBus.ObjectManager";

// The bus BlueZ lives on. It is the system bus, unless `APD_BLUEZ_BUS=session` is set, which lets
// the test-suite talk to a fake BlueZ on a private session bus.
QDBusConnection Bus();

// "AA:BB:CC:DD:EE:FF" <-> 0xAABBCCDDEEFF
std::optional<uint64_t> ParseAddress(const QString &text);
QString FormatAddress(uint64_t address);

// "bluetooth:v004Cp200Ed0100" / "usb:v05ACp200E..." -> vendor / product ids.
struct Modalias {
    uint16_t vendorId{};
    uint16_t productId{};
};
std::optional<Modalias> ParseModalias(const QString &text);

// ManufacturerData: a{qv} where every variant is a byte array. Accepts the raw `QVariant` coming out
// of a `GetAll`/`PropertiesChanged` map.
std::map<uint16_t, std::vector<uint8_t>> ParseManufacturerData(const QVariant &value);

// One org.bluez.Device1 object, flattened.
struct DeviceInfo {
    QString path;
    QVariantMap properties;
};

// GetManagedObjects() filtered to org.bluez.Device1. Empty when BlueZ is not running.
std::vector<DeviceInfo> GetDevices();

// Path of the first adapter that is powered (or any adapter if none is), empty if there is none.
QString FindAdapterPath();

// Properties of one interface of one object (`GetAll`); empty map on failure.
QVariantMap GetAllProperties(const QString &path, const QString &interface);

// Fire-and-wait method call without arguments. Returns an empty string on success, otherwise the
// D-Bus error message. `timeoutMs` is how long to wait for the reply.
QString CallMethod(
    const QString &path, const QString &interface, const QString &method, int timeoutMs = 25000);

// Delivers `PropertiesChanged` of every org.bluez.* object, and `InterfacesAdded` for new ones, on
// the GUI thread. Handlers run in registration order.
class Monitor final : public QObject
{
    Q_OBJECT

public:
    using FnPropertiesChanged =
        std::function<void(const QString &path, const QString &interface, const QVariantMap &)>;

    // Created lazily and parented to the GUI thread so DBus signals always land there.
    static Monitor &Instance();

    uint64_t Subscribe(FnPropertiesChanged handler);
    void Unsubscribe(uint64_t token);

private slots:
    void OnPropertiesChanged(const QDBusMessage &message);
    void OnInterfacesAdded(const QDBusMessage &message);

private:
    Monitor();

    std::mutex _mutex;
    uint64_t _nextToken{1};
    std::vector<std::pair<uint64_t, FnPropertiesChanged>> _handlers;

    void Dispatch(const QString &path, const QString &interface, const QVariantMap &changed);
};

} // namespace Core::OS::BlueZ
