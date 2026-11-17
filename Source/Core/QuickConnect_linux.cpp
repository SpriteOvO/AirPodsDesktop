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

#if defined APD_OS_LINUX

#include <algorithm>
#include <mutex>
#include <stop_token>
#include <utility>

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QMetaObject>
#include <QObject>
#include <QStringList>

#include "BluezObjectManager.h"
#include "../Logger.h"

namespace Core::QuickConnect {

namespace {

constexpr int kConnectTimeoutMs = 20'000;

// The audio service UUIDs a device that can play sound advertises. Checking the UUIDs as well as
// the class keeps headsets and speakers that report a blank class working.
const QStringList &AudioUuidPrefixes()
{
    static const QStringList prefixes{
        QStringLiteral("1108"), // Headset
        QStringLiteral("110a"), // Audio Source
        QStringLiteral("110b"), // Audio Sink
        QStringLiteral("110c"), // A/V Remote Control Target
        QStringLiteral("110d"), // Advanced Audio Distribution
        QStringLiteral("110e"), // A/V Remote Control
        QStringLiteral("111e"), // Handsfree
        QStringLiteral("111f"), // Handsfree Audio Gateway
    };
    return prefixes;
}

bool HasAudioUuid(const QVariant &value)
{
    // BlueZ reports the full 128-bit form, "0000110b-0000-1000-8000-00805f9b34fb"; accept the
    // 4-digit short form too, since some stacks hand out that one.
    for (const auto &uuid : value.toStringList()) {
        const auto normalized = uuid.trimmed();
        for (const auto &shortId : AudioUuidPrefixes()) {
            if (normalized.startsWith(shortId, Qt::CaseInsensitive) ||
                normalized.startsWith(QStringLiteral("0000") + shortId, Qt::CaseInsensitive)) {
                return true;
            }
        }
    }
    return false;
}

bool IsAudioDevice(const QVariantMap &properties)
{
    // Major device class 0x04 is Audio/Video. LE-only devices often leave Class at zero, so the
    // UUIDs are the fallback.
    if (((properties.value("Class").toUInt() >> 8) & 0x1fU) == 0x04U) {
        return true;
    }
    return HasAudioUuid(properties.value("UUIDs"));
}

QString DeviceName(const QVariantMap &properties, const QString &id)
{
    auto name = properties.value("Alias").toString();
    if (name.isEmpty()) {
        name = properties.value("Name").toString();
    }
    if (name.isEmpty()) {
        name = id;
    }
    return name;
}

} // namespace

namespace Details {

std::vector<BluezDeviceInfo> ParseManagedObjects(const QVariantMap &objects)
{
    std::vector<BluezDeviceInfo> devices;

    for (auto iter = objects.begin(); iter != objects.end(); ++iter) {
        const auto interfaces = iter.value().toMap();
        const auto deviceIter = interfaces.constFind(QLatin1String{Core::Bluez::kDeviceInterface});
        if (deviceIter == interfaces.constEnd()) {
            continue;
        }

        const auto properties = deviceIter.value().toMap();
        if (!properties.value("Paired").toBool() || !IsAudioDevice(properties)) {
            continue;
        }

        const auto id = properties.value("Address").toString();
        if (id.isEmpty()) {
            continue;
        }

        devices.push_back(BluezDeviceInfo{
            .path = iter.key(),
            .id = id,
            .name = DeviceName(properties, id),
            .connected = properties.value("Connected").toBool()});
    }

    return devices;
}

std::optional<QString> FindObjectPathById(const QVariantMap &objects, const QString &id)
{
    const auto normalized = id.trimmed();

    for (auto iter = objects.begin(); iter != objects.end(); ++iter) {
        const auto interfaces = iter.value().toMap();
        const auto deviceIter = interfaces.constFind(QLatin1String{Core::Bluez::kDeviceInterface});
        if (deviceIter == interfaces.constEnd()) {
            continue;
        }

        const auto address = deviceIter.value().toMap().value("Address").toString();
        if (QString::compare(address, normalized, Qt::CaseInsensitive) == 0) {
            return iter.key();
        }
    }

    return std::nullopt;
}

QString MaskDeviceId(const QString &id)
{
    const auto trimmed = id.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("<empty>");
    }
    return QStringLiteral("***") + trimmed.right(4);
}

} // namespace Details

// Keeps a subscription to every BlueZ PropertiesChanged signal for the life of the process and only
// forwards the ones belonging to the device a reconnect is waiting on. A per-request subscription
// would have to be armed from the request thread and disarmed from the GUI thread at the same time,
// and QtDBus signal connections are not cheap to churn.
class LinuxBackend final : public QObject, public Backend, protected QDBusContext
{
    Q_OBJECT

public:
    explicit LinuxBackend(Details::Transport transport) : _transport{std::move(transport)}
    {
        const auto connected = QDBusConnection::systemBus().connect(
            Core::Bluez::kService, QString(), "org.freedesktop.DBus.Properties", "PropertiesChanged",
            this, SLOT(OnPropertiesChanged(QString, QVariantMap, QStringList)));
        if (!connected) {
            LOG(Warn, "Quick connect: subscribing to BlueZ PropertiesChanged signals failed.");
        }
    }

    ~LinuxBackend() override
    {
        StopObserving();
        SetController(nullptr);
    }

    void SetController(Controller *controller) override
    {
        std::lock_guard<std::mutex> lock{_controllerMutex};
        _controller = controller;
    }

    void StopObserving() override
    {
        std::lock_guard<std::mutex> lock{_observationMutex};
        _observation.reset();
    }

    std::vector<Device> ListDevices(std::stop_token stopToken) override
    {
        if (stopToken.stop_requested()) {
            return {};
        }

        const auto objects = _transport.managedObjects();
        std::vector<Device> devices;
        for (auto &info : Details::ParseManagedObjects(objects)) {
            if (stopToken.stop_requested()) {
                return {};
            }
            devices.push_back(Device{info.id, info.name, info.connected});
        }

        std::sort(devices.begin(), devices.end(), [](const Device &lhs, const Device &rhs) {
            return QString::compare(lhs.name, rhs.name, Qt::CaseInsensitive) < 0;
        });
        return devices;
    }

    bool RequestReconnect(const QString &id, std::stop_token stopToken) override
    {
        return RequestOperation(id, stopToken, false);
    }

    bool RequestDisconnect(const QString &id, std::stop_token stopToken) override
    {
        return RequestOperation(id, stopToken, true);
    }

    bool RequestOperation(const QString &id, std::stop_token stopToken, bool disconnect)
    {
        if (stopToken.stop_requested()) {
            return false;
        }

        const auto objects = _transport.managedObjects();
        const auto objectPath = Details::FindObjectPathById(objects, id);
        if (!objectPath.has_value()) {
            LOG(Warn, "Quick connect: device {} is not known to BlueZ.",
                Details::MaskDeviceId(id).toStdString());
            return false;
        }
        if (stopToken.stop_requested()) {
            return false;
        }

        // Arm the observation before issuing the request: BlueZ can raise PropertiesChanged as soon
        // as it starts connecting, and the link coming up is what resolves the request.
        {
            std::lock_guard<std::mutex> lock{_observationMutex};
            _observation = std::make_shared<const Observation>(Observation{id, *objectPath});
        }

        const auto method = disconnect ? QStringLiteral("Disconnect") : QStringLiteral("Connect");
        const auto message = QDBusMessage::createMethodCall(
            Core::Bluez::kService, *objectPath, Core::Bluez::kDeviceInterface, method);
        const auto reply = _transport.call(message);

        if (reply.type() != QDBusMessage::ErrorMessage) {
            return true;
        }

        // The device may disconnect between enumeration and the method call. BlueZ's
        // NotConnected error confirms the desired state even if we missed its signal.
        if (disconnect && reply.errorName() == QLatin1String("org.bluez.Error.NotConnected")) {
            QueueEndpointState(id, false);
            return true;
        }

        // A request that ran out of time can still bring the link up later; keep observing instead
        // of reporting a failure the device may still recover from.
        if (reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.NoReply")) {
            LOG(Warn, "Quick connect: Device1.{} did not reply in time for {}.",
                method.toStdString(), Details::MaskDeviceId(id).toStdString());
            return true;
        }

        LOG(Warn, "Quick connect: Device1.{} failed for {}: {}", method.toStdString(),
            Details::MaskDeviceId(id).toStdString(), reply.errorMessage().toStdString());
        StopObserving();
        return false;
    }

public slots:
    void OnPropertiesChanged(
        const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
    {
        Q_UNUSED(invalidated)

        if (interface != QLatin1String{Core::Bluez::kDeviceInterface} ||
            !changed.contains("Connected")) {
            return;
        }

        const auto objectPath = message().path();

        std::shared_ptr<const Observation> observation;
        {
            std::lock_guard<std::mutex> lock{_observationMutex};
            observation = _observation;
        }
        if (!observation || observation->path != objectPath) {
            return;
        }

        QueueEndpointState(observation->id, changed.value("Connected").toBool());
    }

private:
    struct Observation {
        QString id;
        QString path;
    };

    Details::Transport _transport;
    std::mutex _controllerMutex;
    Controller *_controller{};

    std::mutex _observationMutex;
    std::shared_ptr<const Observation> _observation;

    void QueueEndpointState(const QString &id, bool connected)
    {
        // Same contract as the Windows backend: the mutex is held only for the pointer copy and the
        // queued post, and the Controller clears itself before tearing down, so a post either lands
        // on a live Controller or finds nothing.
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
};

std::shared_ptr<Backend> CreateLinuxBackend()
{
    return CreateLinuxBackend(Details::Transport{
        [] { return Core::Bluez::GetManagedObjects(); },
        [](const QDBusMessage &message) {
            return QDBusConnection::systemBus().call(message, QDBus::Block, kConnectTimeoutMs);
        }});
}

std::shared_ptr<Backend> CreateLinuxBackend(Details::Transport transport)
{
    return std::make_shared<LinuxBackend>(std::move(transport));
}

} // namespace Core::QuickConnect

#endif

#include "QuickConnect_linux.moc"
