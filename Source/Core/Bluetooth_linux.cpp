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

#include "Bluetooth_linux.h"

#if defined APD_OS_LINUX

#include <functional>
#include <mutex>
#include <utility>

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QHash>
#include <QRegularExpression>
#include <QThread>
#include <QVariantMap>

#include "BluezObjectManager.h"
#include "../Logger.h"

namespace Core::Bluetooth {

namespace {

constexpr auto kBluezService = "org.bluez";
constexpr auto kDeviceInterface = "org.bluez.Device1";
constexpr auto kAdapterInterface = "org.bluez.Adapter1";

using Core::Bluez::GetManagedObjects;

QVariant ReadDeviceProperty(const QString &objectPath, const char *name)
{
    QDBusInterface device{kBluezService, objectPath, kDeviceInterface, QDBusConnection::systemBus()};
    return device.property(name);
}

uint64_t ParseAddress(const QString &address)
{
    auto normalized = address;
    normalized.remove(':');
    bool ok = false;
    const auto value = normalized.toULongLong(&ok, 16);
    return ok ? value : 0;
}

QVariantMap ReadDeviceProperties(const QString &objectPath)
{
    auto message = QDBusMessage::createMethodCall(
        kBluezService, objectPath, "org.freedesktop.DBus.Properties", "GetAll");
    message.setArguments({QLatin1String{kDeviceInterface}});

    const auto reply = QDBusConnection::systemBus().call(message, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }

    const auto arg = reply.arguments().at(0).value<QDBusArgument>();
    QVariantMap properties;
    arg >> properties;
    return properties;
}

std::map<uint16_t, std::vector<uint8_t>> ParseManufacturerData(const QVariant &value)
{
    std::map<uint16_t, std::vector<uint8_t>> result;

    if (value.canConvert<QDBusArgument>()) {
        const auto arg = value.value<QDBusArgument>();
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            ushort companyId = 0;
            // BlueZ declares this map as a{qv}: the payload is a variant holding an array of
            // bytes, so reading it straight into a QByteArray would leave it empty.
            QVariant data;
            arg >> companyId >> data;
            arg.endMapEntry();
            const auto bytes = data.toByteArray();
            result[companyId] = std::vector<uint8_t>{bytes.begin(), bytes.end()};
        }
        arg.endMap();
        return result;
    }

    const auto map = value.toMap();
    for (auto iter = map.begin(); iter != map.end(); ++iter) {
        bool ok = false;
        const auto companyId = iter.key().toUShort(&ok, 0);
        if (!ok) {
            continue;
        }
        const auto data = iter.value().toByteArray();
        result[companyId] = std::vector<uint8_t>{data.begin(), data.end()};
    }
    return result;
}

void MoveToApplicationThread(QObject *object)
{
    auto *app = QCoreApplication::instance();
    if (app != nullptr && QThread::currentThread() != app->thread()) {
        object->moveToThread(app->thread());
    }
}

std::shared_ptr<QObject> MakeSharedNotifier(QObject *object)
{
    MoveToApplicationThread(object);
    return std::shared_ptr<QObject>{object, [](QObject *obj) { obj->deleteLater(); }};
}

} // namespace

//////////////////////////////////////////////////
// Device
//

class Device::Notifier final : public QObject, protected QDBusContext
{
    Q_OBJECT

public:
    struct Sink {
        uint64_t id{};
        std::function<void(DeviceState)> onState;
        std::function<void(const std::string &)> onName;
    };

    explicit Notifier(QString objectPath) : _objectPath{std::move(objectPath)}
    {
        auto bus = QDBusConnection::systemBus();
        bus.connect(
            kBluezService, _objectPath, "org.freedesktop.DBus.Properties", "PropertiesChanged",
            this, SLOT(OnPropertiesChanged(QString, QVariantMap, QStringList)));
    }

    void AddSink(Sink sink)
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _sinks.push_back(std::move(sink));
    }

    void RemoveSink(uint64_t id)
    {
        std::lock_guard<std::mutex> lock{_mutex};
        std::erase_if(_sinks, [id](const Sink &sink) { return sink.id == id; });
    }

public slots:
    void OnPropertiesChanged(
        const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
    {
        Q_UNUSED(invalidated)

        if (interface != QLatin1String{kDeviceInterface}) {
            return;
        }

        const auto stateRelevant = changed.contains("Connected") || changed.contains("Paired");
        const auto nameRelevant = changed.contains("Name") || changed.contains("Alias");
        if (!stateRelevant && !nameRelevant) {
            return;
        }

        std::optional<DeviceState> state;
        if (stateRelevant) {
            const auto connected = ReadDeviceProperty(_objectPath, "Connected").toBool();
            const auto paired = ReadDeviceProperty(_objectPath, "Paired").toBool();
            state = connected ? DeviceState::Connected
                              : (paired ? DeviceState::Paired : DeviceState::Disconnected);
        }

        std::optional<std::string> name;
        if (nameRelevant) {
            auto value = ReadDeviceProperty(_objectPath, "Alias").toString();
            if (value.isEmpty()) {
                value = ReadDeviceProperty(_objectPath, "Name").toString();
            }
            name = value.toStdString();
        }

        std::lock_guard<std::mutex> lock{_mutex};
        // Every live Device owns exactly one sink; more means a sink outlived its device.
        LOG(Debug, "Notifier: dispatching PropertiesChanged to {} sink(s)", _sinks.size());
        for (const auto &sink : _sinks) {
            if (state.has_value() && sink.onState) {
                sink.onState(state.value());
            }
            if (name.has_value() && sink.onName) {
                sink.onName(name.value());
            }
        }
    }

private:
    QString _objectPath;
    std::mutex _mutex;
    std::vector<Sink> _sinks;
};

namespace {
std::atomic<uint64_t> g_nextSinkId{1};
}

Device::Device(QString objectPath) : _objectPath{std::move(objectPath)}
{
    if (!_objectPath.isEmpty()) {
        _notifier = std::static_pointer_cast<Notifier>(
            MakeSharedNotifier(new Notifier{_objectPath}));
        Attach();
    }
}

Device::Device(const Device &rhs)
    : _objectPath{rhs._objectPath}, _notifier{rhs._notifier}, _nextSinkId{rhs._nextSinkId}
{
    Attach();
}

Device::Device(Device &&rhs) noexcept
{
    MoveFrom(std::move(rhs));
}

Device &Device::operator=(const Device &rhs)
{
    if (this != &rhs) {
        Detach();
        CopyFrom(rhs);
    }
    return *this;
}

Device &Device::operator=(Device &&rhs) noexcept
{
    if (this != &rhs) {
        Detach();
        MoveFrom(std::move(rhs));
    }
    return *this;
}

Device::~Device()
{
    Detach();
}

void Device::Attach()
{
    if (!_notifier) {
        return;
    }

    _sinkId = g_nextSinkId++;
    _notifier->AddSink(
        {.id = _sinkId,
         .onState = [this](DeviceState state) { CbConnectionStatusChanged().Invoke(state); },
         .onName = [this](const std::string &name) { CbNameChanged().Invoke(name); }});
}

void Device::Detach()
{
    if (_notifier && _sinkId != 0) {
        _notifier->RemoveSink(_sinkId);
    }
    _sinkId = 0;
}

void Device::CopyFrom(const Device &rhs)
{
    _objectPath = rhs._objectPath;
    _notifier = rhs._notifier;
    _nextSinkId = rhs._nextSinkId;
    Attach();
}

void Device::MoveFrom(Device &&rhs) noexcept
{
    // Detach the source before its notifier changes hands. A moved-from device no longer has a
    // notifier, so its destructor would skip RemoveSink() and leave a sink registered in the
    // notifier that still refers to the freed source object. The next PropertiesChanged would
    // then invoke that dead object - in practice a std::system_error from its invalid mutex.
    rhs.Detach();
    _objectPath = std::move(rhs._objectPath);
    _notifier = std::move(rhs._notifier);
    _nextSinkId = rhs._nextSinkId;
    Attach();
}

uint64_t Device::GetAddress() const
{
    return ParseAddress(ReadDeviceProperty(_objectPath, "Address").toString());
}

std::string Device::GetName() const
{
    auto name = ReadDeviceProperty(_objectPath, "Alias").toString();
    if (name.isEmpty()) {
        name = ReadDeviceProperty(_objectPath, "Name").toString();
    }
    return name.toStdString();
}

uint16_t Device::GetVendorId() const
{
    // Modalias looks like "bluetooth:v004Cp2002d0000".
    static const QRegularExpression re{QStringLiteral("v([0-9A-Fa-f]{4})p([0-9A-Fa-f]{4})")};
    const auto modalias = ReadDeviceProperty(_objectPath, "Modalias").toString();
    const auto match = re.match(modalias);
    if (!match.hasMatch()) {
        return 0;
    }
    return static_cast<uint16_t>(match.captured(1).toUShort(nullptr, 16));
}

uint16_t Device::GetProductId() const
{
    static const QRegularExpression re{QStringLiteral("v([0-9A-Fa-f]{4})p([0-9A-Fa-f]{4})")};
    const auto modalias = ReadDeviceProperty(_objectPath, "Modalias").toString();
    const auto match = re.match(modalias);
    if (!match.hasMatch()) {
        return 0;
    }
    return static_cast<uint16_t>(match.captured(2).toUShort(nullptr, 16));
}

DeviceState Device::GetConnectionState() const
{
    if (ReadDeviceProperty(_objectPath, "Connected").toBool()) {
        return DeviceState::Connected;
    }
    if (ReadDeviceProperty(_objectPath, "Paired").toBool()) {
        return DeviceState::Paired;
    }
    return DeviceState::Disconnected;
}

//////////////////////////////////////////////////
// DeviceManager
//

namespace DeviceManager {

std::vector<Device> GetDevicesByState(DeviceState state)
{
    std::vector<Device> result;

    const auto objects = GetManagedObjects();
    for (auto iter = objects.begin(); iter != objects.end(); ++iter) {
        const auto interfaces = iter.value().toMap();
        if (!interfaces.contains(QLatin1String{kDeviceInterface})) {
            continue;
        }

        const auto props = interfaces.value(QLatin1String{kDeviceInterface}).toMap();
        const auto paired = props.value("Paired").toBool();
        const auto connected = props.value("Connected").toBool();

        bool match = false;
        switch (state) {
        case DeviceState::Paired:
            match = paired;
            break;
        case DeviceState::Connected:
            match = connected;
            break;
        case DeviceState::Disconnected:
            match = !connected;
            break;
        }

        if (match) {
            result.emplace_back(iter.key());
        }
    }

    return result;
}

std::optional<Device> FindDevice(uint64_t address)
{
    auto devices = GetDevicesByState(DeviceState::Paired);
    for (auto &device : devices) {
        if (device.GetAddress() == address) {
            return device;
        }
    }
    return std::nullopt;
}

} // namespace DeviceManager

//////////////////////////////////////////////////
// AdvertisementWatcher
//

class AdvertisementWatcher::Notifier final : public QObject, protected QDBusContext
{
    Q_OBJECT

public:
    explicit Notifier(AdvertisementWatcher *owner) : _owner{owner}
    {
        auto bus = QDBusConnection::systemBus();
        bus.connect(
            kBluezService, QString(), "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
            SLOT(OnPropertiesChanged(QString, QVariantMap, QStringList)));
        bus.connect(
            kBluezService, QString(), "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", this,
            SLOT(OnInterfacesAdded(QDBusObjectPath, QVariantMap)));
    }

public slots:
    void OnPropertiesChanged(
        const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
    {
        Q_UNUSED(invalidated)

        if (interface != QLatin1String{kDeviceInterface} || !changed.contains("ManufacturerData")) {
            return;
        }

        ProcessDevice(message().path(), changed);
    }

    void OnInterfacesAdded(const QDBusObjectPath &objectPath, const QVariantMap &interfaces)
    {
        if (!interfaces.contains(QLatin1String{kDeviceInterface})) {
            return;
        }

        // The properties nested in this signal are demarshalled into opaque raw types, so the
        // device is queried again to obtain usable values.
        const auto props = ReadDeviceProperties(objectPath.path());
        if (props.isEmpty() || !props.contains("ManufacturerData")) {
            return;
        }

        ProcessDevice(objectPath.path(), props);
    }

private:
    AdvertisementWatcher *_owner;
    QHash<QString, uint64_t> _addresses;

    uint64_t AddressFor(const QString &objectPath)
    {
        const auto cached = _addresses.constFind(objectPath);
        if (cached != _addresses.constEnd()) {
            return cached.value();
        }

        const auto address =
            ParseAddress(ReadDeviceProperty(objectPath, "Address").toString());
        _addresses.insert(objectPath, address);
        return address;
    }

    void ProcessDevice(const QString &objectPath, const QVariantMap &properties)
    {
        const auto manufacturerData =
            ParseManufacturerData(properties.value("ManufacturerData"));
        if (manufacturerData.empty()) {
            return;
        }

        int16_t rssi = static_cast<int16_t>(properties.value("RSSI").toInt());
        if (!properties.contains("RSSI")) {
            rssi = static_cast<int16_t>(
                ReadDeviceProperty(objectPath, "RSSI").toInt());
        }

        _owner->EmitAdvertisement(objectPath, AddressFor(objectPath), rssi, manufacturerData);
    }
};

AdvertisementWatcher::AdvertisementWatcher()
    : _notifier{std::static_pointer_cast<Notifier>(
          MakeSharedNotifier(new Notifier{this}))}
{
}

AdvertisementWatcher::~AdvertisementWatcher()
{
    if (_running.exchange(false)) {
        StopDiscovery();
    }
}

bool AdvertisementWatcher::FindAdapter()
{
    if (!_adapterPath.isEmpty()) {
        return true;
    }

    const auto objects = GetManagedObjects();
    for (auto iter = objects.begin(); iter != objects.end(); ++iter) {
        if (iter.value().toMap().contains(QLatin1String{kAdapterInterface})) {
            _adapterPath = iter.key();
            return true;
        }
    }

    LOG(Warn, "No BlueZ adapter found. Is BlueZ running and is the adapter powered?");
    return false;
}

bool AdvertisementWatcher::StartDiscovery()
{
    QDBusInterface adapter{
        kBluezService, _adapterPath, kAdapterInterface, QDBusConnection::systemBus()};
    if (!adapter.isValid()) {
        LOG(Warn, "BlueZ adapter interface is not available at '{}'.", _adapterPath.toStdString());
        return false;
    }

    const QVariantMap filter{
        {"Transport", "le"},
        {"DuplicateData", true},
    };
    const auto filterReply = adapter.call("SetDiscoveryFilter", filter);
    if (filterReply.type() == QDBusMessage::ErrorMessage) {
        LOG(Warn, "SetDiscoveryFilter failed: {}", filterReply.errorMessage().toStdString());
    }

    const auto reply = adapter.call("StartDiscovery");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        LOG(Warn, "StartDiscovery failed: {}", reply.errorMessage().toStdString());
        return false;
    }

    return true;
}

bool AdvertisementWatcher::StopDiscovery()
{
    if (_adapterPath.isEmpty()) {
        return true;
    }

    QDBusInterface adapter{
        kBluezService, _adapterPath, kAdapterInterface, QDBusConnection::systemBus()};
    const auto reply = adapter.call("StopDiscovery");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        LOG(Warn, "StopDiscovery failed: {}", reply.errorMessage().toStdString());
        return false;
    }
    return true;
}

bool AdvertisementWatcher::Start()
{
    if (_running) {
        return true;
    }

    if (!FindAdapter() || !StartDiscovery()) {
        return false;
    }

    _running = true;
    CbStateChanged().Invoke(State::Started, std::nullopt);
    return true;
}

bool AdvertisementWatcher::Stop()
{
    if (!_running.exchange(false)) {
        return true;
    }

    StopDiscovery();
    CbStateChanged().Invoke(State::Stopped, std::nullopt);
    return true;
}

void AdvertisementWatcher::EmitAdvertisement(
    const QString &objectPath, uint64_t address, int16_t rssi,
    const std::map<uint16_t, std::vector<uint8_t>> &manufacturerData)
{
    Q_UNUSED(objectPath)

    ReceivedData data;
    data.rssi = rssi;
    data.timestamp = Timestamp{std::chrono::steady_clock::now()};
    data.address = address;
    data.manufacturerDataMap = manufacturerData;

    CbReceived().Invoke(data);
}

} // namespace Core::Bluetooth

#endif

#include "Bluetooth_linux.moc"
