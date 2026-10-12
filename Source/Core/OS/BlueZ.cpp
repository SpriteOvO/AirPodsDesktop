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

#include "BlueZ.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QRegularExpression>
#include <QThread>

#include "../../Logger.h"

namespace Core::OS::BlueZ {

namespace {

QVariant Unwrap(QVariant value)
{
    if (value.canConvert<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }
    return value;
}

// GetManagedObjects() -> a{oa{sa{sv}}}
using ManagedObjects = std::vector<std::pair<QString, std::map<QString, QVariantMap>>>;

ManagedObjects GetManagedObjects()
{
    ManagedObjects result;

    auto message = QDBusMessage::createMethodCall(
        kService, "/", kObjectManagerInterface, "GetManagedObjects");
    const auto reply = Bus().call(message, QDBus::Block, 10000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        LOG(Warn, "BlueZ: GetManagedObjects failed: '{}'", reply.errorMessage());
        return result;
    }

    const auto argument = reply.arguments().first().value<QDBusArgument>();
    argument.beginMap();
    while (!argument.atEnd()) {
        QDBusObjectPath path;
        std::map<QString, QVariantMap> interfaces;

        argument.beginMapEntry();
        argument >> path;
        argument.beginMap();
        while (!argument.atEnd()) {
            QString name;
            QVariantMap properties;
            argument.beginMapEntry();
            argument >> name >> properties;
            argument.endMapEntry();
            interfaces.emplace(std::move(name), std::move(properties));
        }
        argument.endMap();
        argument.endMapEntry();

        result.emplace_back(path.path(), std::move(interfaces));
    }
    argument.endMap();
    return result;
}

} // namespace

QDBusConnection Bus()
{
    if (qEnvironmentVariable("APD_BLUEZ_BUS") == QStringLiteral("session")) {
        return QDBusConnection::sessionBus();
    }
    return QDBusConnection::systemBus();
}

std::optional<uint64_t> ParseAddress(const QString &text)
{
    static const QRegularExpression pattern{
        R"(^([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2})$)"};

    const auto match = pattern.match(text.trimmed());
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    uint64_t address = 0;
    for (int i = 1; i <= 6; ++i) {
        address = (address << 8) | match.captured(i).toUInt(nullptr, 16);
    }
    return address;
}

QString FormatAddress(uint64_t address)
{
    return QString{"%1:%2:%3:%4:%5:%6"}
        .arg((address >> 40) & 0xFF, 2, 16, QChar{'0'})
        .arg((address >> 32) & 0xFF, 2, 16, QChar{'0'})
        .arg((address >> 24) & 0xFF, 2, 16, QChar{'0'})
        .arg((address >> 16) & 0xFF, 2, 16, QChar{'0'})
        .arg((address >> 8) & 0xFF, 2, 16, QChar{'0'})
        .arg(address & 0xFF, 2, 16, QChar{'0'})
        .toUpper();
}

std::optional<Modalias> ParseModalias(const QString &text)
{
    static const QRegularExpression pattern{
        R"(^(?:bluetooth|usb):v([0-9A-Fa-f]{4})p([0-9A-Fa-f]{4})d[0-9A-Fa-f]{4}$)"};

    const auto match = pattern.match(text.trimmed());
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return Modalias{
        static_cast<uint16_t>(match.captured(1).toUInt(nullptr, 16)),
        static_cast<uint16_t>(match.captured(2).toUInt(nullptr, 16))};
}

std::map<uint16_t, std::vector<uint8_t>> ParseManufacturerData(const QVariant &value)
{
    std::map<uint16_t, std::vector<uint8_t>> result;

    const auto inner = Unwrap(value);
    if (!inner.canConvert<QDBusArgument>()) {
        return result;
    }

    const auto argument = inner.value<QDBusArgument>();
    if (argument.currentType() != QDBusArgument::MapType) {
        return result;
    }

    argument.beginMap();
    while (!argument.atEnd()) {
        quint16 companyId = 0;
        QDBusVariant payload;

        argument.beginMapEntry();
        argument >> companyId >> payload;
        argument.endMapEntry();

        const auto bytes = Unwrap(payload.variant()).toByteArray();
        result[companyId] = std::vector<uint8_t>{bytes.begin(), bytes.end()};
    }
    argument.endMap();
    return result;
}

std::vector<DeviceInfo> GetDevices()
{
    std::vector<DeviceInfo> devices;
    for (auto &[path, interfaces] : GetManagedObjects()) {
        if (const auto iter = interfaces.find(kDeviceInterface); iter != interfaces.end()) {
            devices.push_back(DeviceInfo{path, iter->second});
        }
    }
    return devices;
}

QString FindAdapterPath()
{
    QString fallback;
    for (const auto &[path, interfaces] : GetManagedObjects()) {
        const auto iter = interfaces.find(kAdapterInterface);
        if (iter == interfaces.end()) {
            continue;
        }
        if (iter->second.value("Powered").toBool()) {
            return path;
        }
        if (fallback.isEmpty()) {
            fallback = path;
        }
    }
    return fallback;
}

QVariantMap GetAllProperties(const QString &path, const QString &interface)
{
    QVariantMap result;

    auto message = QDBusMessage::createMethodCall(kService, path, kPropertiesInterface, "GetAll");
    message << interface;
    const auto reply = Bus().call(message, QDBus::Block, 10000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return result;
    }

    const auto argument = reply.arguments().first().value<QDBusArgument>();
    argument >> result;
    return result;
}

QString CallMethod(
    const QString &path, const QString &interface, const QString &method, int timeoutMs)
{
    const auto message = QDBusMessage::createMethodCall(kService, path, interface, method);
    const auto reply = Bus().call(message, QDBus::Block, timeoutMs);
    if (reply.type() == QDBusMessage::ReplyMessage) {
        return {};
    }
    const auto error = reply.errorMessage();
    return error.isEmpty() ? QStringLiteral("unknown D-Bus error") : error;
}

//////////////////////////////////////////////////

Monitor &Monitor::Instance()
{
    static Monitor *instance = [] {
        auto *monitor = new Monitor;
        if (auto *app = QCoreApplication::instance()) {
            monitor->moveToThread(app->thread());
        }
        return monitor;
    }();
    return *instance;
}

Monitor::Monitor() : QObject{nullptr}
{
    auto bus = Bus();

    // An empty path subscribes to every object below org.bluez.
    if (!bus.connect(
            kService, QString{}, kPropertiesInterface, "PropertiesChanged", this,
            SLOT(OnPropertiesChanged(QDBusMessage))))
    {
        LOG(Warn, "BlueZ: cannot subscribe to PropertiesChanged: '{}'", bus.lastError().message());
    }
    if (!bus.connect(
            kService, "/", kObjectManagerInterface, "InterfacesAdded", this,
            SLOT(OnInterfacesAdded(QDBusMessage))))
    {
        LOG(Warn, "BlueZ: cannot subscribe to InterfacesAdded: '{}'", bus.lastError().message());
    }
}

uint64_t Monitor::Subscribe(FnPropertiesChanged handler)
{
    std::lock_guard<std::mutex> lock{_mutex};
    const auto token = _nextToken++;
    _handlers.emplace_back(token, std::move(handler));
    return token;
}

void Monitor::Unsubscribe(uint64_t token)
{
    std::lock_guard<std::mutex> lock{_mutex};
    std::erase_if(_handlers, [token](const auto &entry) { return entry.first == token; });
}

void Monitor::OnPropertiesChanged(const QDBusMessage &message)
{
    const auto arguments = message.arguments();
    if (arguments.size() < 2) {
        return;
    }

    QVariantMap changed;
    arguments.at(1).value<QDBusArgument>() >> changed;
    Dispatch(message.path(), arguments.at(0).toString(), changed);
}

void Monitor::OnInterfacesAdded(const QDBusMessage &message)
{
    const auto arguments = message.arguments();
    if (arguments.size() < 2) {
        return;
    }

    const auto path = arguments.at(0).value<QDBusObjectPath>().path();
    const auto interfaces = arguments.at(1).value<QDBusArgument>();

    interfaces.beginMap();
    while (!interfaces.atEnd()) {
        QString name;
        QVariantMap properties;
        interfaces.beginMapEntry();
        interfaces >> name >> properties;
        interfaces.endMapEntry();

        Dispatch(path, name, properties);
    }
    interfaces.endMap();
}

void Monitor::Dispatch(const QString &path, const QString &interface, const QVariantMap &changed)
{
    // Handlers may (un)subscribe, so run a copy.
    decltype(_handlers) handlers;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        handlers = _handlers;
    }

    for (const auto &[token, handler] : handlers) {
        handler(path, interface, changed);
    }
}

} // namespace Core::OS::BlueZ
