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

#include <mutex>
#include <unordered_map>

#include <QCoreApplication>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QThread>
#include <QTimer>

#include "OS/BlueZ.h"
#include "../Assert.h"
#include "../Logger.h"

namespace Core::Bluetooth {

namespace {

namespace BlueZ = OS::BlueZ;

std::optional<uint64_t> AddressFromPath(const QString &path)
{
    // /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF
    const auto name = path.section('/', -1);
    if (!name.startsWith("dev_")) {
        return std::nullopt;
    }
    return BlueZ::ParseAddress(name.mid(4).replace('_', ':'));
}

} // namespace

//////////////////////////////////////////////////
// Device

Device::Device(const OS::BlueZ::DeviceInfo &info) : _path{info.path}
{
    const auto &props = info.properties;

    _address = BlueZ::ParseAddress(props.value("Address").toString())
                   .value_or(AddressFromPath(_path).value_or(0));

    auto name = props.value("Alias").toString();
    if (name.isEmpty()) {
        name = props.value("Name").toString();
    }
    _name = name.toStdString();

    if (const auto modalias = BlueZ::ParseModalias(props.value("Modalias").toString())) {
        _vendorId = modalias->vendorId;
        _productId = modalias->productId;
    }

    _connected = props.value("Connected").toBool();

    Subscribe();
}

Device::Device(const Device &rhs)
{
    CopyFrom(rhs);
    Subscribe();
}

Device::Device(Device &&rhs) noexcept
{
    // The subscription belongs to the moved-from object's identity; take a fresh one rather than
    // sharing a token between two objects.
    CopyFrom(rhs);
    Subscribe();
    rhs.Unsubscribe();
}

Device::~Device()
{
    Unsubscribe();
}

Device &Device::operator=(const Device &rhs)
{
    if (this != &rhs) {
        Unsubscribe();
        CopyFrom(rhs);
        Subscribe();
    }
    return *this;
}

Device &Device::operator=(Device &&rhs) noexcept
{
    if (this != &rhs) {
        Unsubscribe();
        CopyFrom(rhs);
        Subscribe();
        rhs.Unsubscribe();
    }
    return *this;
}

void Device::CopyFrom(const Device &rhs)
{
    _path = rhs._path;
    _address = rhs._address;
    _name = rhs._name;
    _vendorId = rhs._vendorId;
    _productId = rhs._productId;
    _connected = rhs._connected;
    _monitorToken = 0;
}

void Device::Subscribe()
{
    if (_path.isEmpty()) {
        return;
    }

    // The handler runs on the GUI thread. It never outlives the registration: `Unsubscribe` runs
    // before the object goes away, and `Monitor` dispatches on the same thread that destroys the
    // devices Manager owns.
    _monitorToken = BlueZ::Monitor::Instance().Subscribe(
        [this, path = _path](const QString &changedPath, const QString &interface,
                             const QVariantMap &changed) {
            if (changedPath != path || interface != BlueZ::kDeviceInterface) {
                return;
            }

            if (const auto iter = changed.find("Connected"); iter != changed.end()) {
                _connected = iter->toBool();
                CbConnectionStatusChanged().Invoke(
                    _connected ? DeviceState::Connected : DeviceState::Disconnected);
            }
            if (const auto iter = changed.find("Alias"); iter != changed.end()) {
                _name = iter->toString().toStdString();
                CbNameChanged().Invoke(_name);
            }
        });
}

void Device::Unsubscribe()
{
    if (_monitorToken != 0) {
        BlueZ::Monitor::Instance().Unsubscribe(_monitorToken);
        _monitorToken = 0;
    }
}

uint64_t Device::GetAddress() const
{
    return _address;
}

std::string Device::GetName() const
{
    return _name;
}

uint16_t Device::GetVendorId() const
{
    return _vendorId;
}

uint16_t Device::GetProductId() const
{
    return _productId;
}

DeviceState Device::GetConnectionState() const
{
    // Read live: the cached flag only changes when the GUI thread gets to process a signal.
    const auto props = BlueZ::GetAllProperties(_path, BlueZ::kDeviceInterface);
    const bool connected = props.isEmpty() ? _connected : props.value("Connected").toBool();
    return connected ? DeviceState::Connected : DeviceState::Disconnected;
}

void Device::Connect()
{
    // Async on purpose: Device1.Connect blocks until the link is up or times out, and this is
    // called from the advertisement path on the GUI thread.
    auto message =
        QDBusMessage::createMethodCall(BlueZ::kService, _path, BlueZ::kDeviceInterface, "Connect");
    auto *watcher = new QDBusPendingCallWatcher{
        BlueZ::Bus().asyncCall(message), QCoreApplication::instance()};
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, [](QDBusPendingCallWatcher *w) {
        if (w->isError()) {
            LOG(Warn, "BlueZ: Device1.Connect failed: '{}'", w->error().message().toStdString());
        }
        w->deleteLater();
    });
    LOG(Info, "BlueZ: requested Device1.Connect for '{}'", _path.toStdString());
}

const QString &Device::GetPath() const
{
    return _path;
}

//////////////////////////////////////////////////
// DeviceManager

namespace DeviceManager {

std::vector<Device> GetDevicesByState(DeviceState state)
{
    std::vector<Device> result;

    for (const auto &info : BlueZ::GetDevices()) {
        const bool paired = info.properties.value("Paired").toBool();
        const bool connected = info.properties.value("Connected").toBool();

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
            result.emplace_back(info);
        }
    }
    return result;
}

std::optional<Device> FindDevice(uint64_t address)
{
    for (const auto &info : BlueZ::GetDevices()) {
        const auto parsed = BlueZ::ParseAddress(info.properties.value("Address").toString())
                                .value_or(AddressFromPath(info.path).value_or(0));
        if (parsed == address && info.properties.value("Paired").toBool()) {
            return Device{info};
        }
    }
    return std::nullopt;
}

} // namespace DeviceManager

//////////////////////////////////////////////////
// AdvertisementWatcher

struct AdvertisementWatcher::Impl {
    AdvertisementWatcher &owner;

    bool wanted{false};
    std::optional<State> reported; // what the owner was last told, so retries stay quiet
    uint64_t monitorToken{0};
    QString adapterPath;
    QTimer retryTimer;
    std::unordered_map<QString, int16_t> rssiCache;

    explicit Impl(AdvertisementWatcher &watcher) : owner{watcher}
    {
        retryTimer.setInterval(
            std::chrono::duration_cast<std::chrono::milliseconds>(kRetryInterval));
        retryTimer.callOnTimeout([this] { OnRetryTimer(); });
    }

    void Report(State state, const std::optional<std::string> &error = std::nullopt)
    {
        if (reported != state) {
            reported = state;
            owner.CbStateChanged().Invoke(state, error);
        }
    }

    // Returns an empty string on success.
    QString StartDiscovery()
    {
        adapterPath = BlueZ::FindAdapterPath();
        if (adapterPath.isEmpty()) {
            return QStringLiteral("no Bluetooth adapter found (is bluetoothd running?)");
        }

        const auto props = BlueZ::GetAllProperties(adapterPath, BlueZ::kAdapterInterface);
        if (!props.value("Powered").toBool()) {
            return QStringLiteral("the Bluetooth adapter is powered off");
        }

        // LE only: a classic inquiry would be both pointless and slow. Duplicates on: AirPods
        // advertise a new battery/ear state under the same address, BlueZ would otherwise only
        // report the first advertisement of every device.
        auto filter = QDBusMessage::createMethodCall(
            BlueZ::kService, adapterPath, BlueZ::kAdapterInterface, "SetDiscoveryFilter");
        filter << QVariantMap{{"Transport", "le"}, {"DuplicateData", true}};
        const auto filterReply = BlueZ::Bus().call(filter, QDBus::Block, 5000);
        if (filterReply.type() != QDBusMessage::ReplyMessage) {
            LOG(Warn, "BlueZ: SetDiscoveryFilter failed: '{}'", filterReply.errorMessage());
        }

        if (props.value("Discovering").toBool()) {
            // Another client (e.g. the desktop's Bluetooth panel) is already scanning; the filters
            // are merged by BlueZ, so just join in.
            return {};
        }

        auto error = BlueZ::CallMethod(
            adapterPath, BlueZ::kAdapterInterface, "StartDiscovery", 5000);
        if (error.contains("InProgress", Qt::CaseInsensitive)) {
            return {};
        }
        return error;
    }

    bool TryStart()
    {
        const auto error = StartDiscovery();
        if (!error.isEmpty()) {
            LOG(Warn, "BlueZ: cannot start discovery: '{}'", error);
            Report(State::Stopped, error.toStdString());
            return false;
        }

        Report(State::Started);
        return true;
    }

    void OnRetryTimer()
    {
        if (!wanted) {
            return;
        }

        bool discovering = false;
        if (!adapterPath.isEmpty()) {
            discovering =
                BlueZ::GetAllProperties(adapterPath, BlueZ::kAdapterInterface)
                    .value("Discovering")
                    .toBool();
        }

        if (!discovering) {
            Report(State::Stopped, std::string{"discovery stopped"});
            TryStart();
        }
    }

    void OnPropertiesChanged(
        const QString &path, const QString &interface, const QVariantMap &changed)
    {
        if (!wanted || interface != BlueZ::kDeviceInterface) {
            return;
        }

        if (const auto iter = changed.find("RSSI"); iter != changed.end()) {
            if (rssiCache.size() > 2048) {
                rssiCache.clear();
            }
            rssiCache[path] = static_cast<int16_t>(iter->toInt());
        }

        const auto iter = changed.find("ManufacturerData");
        if (iter == changed.end()) {
            return;
        }

        ReceivedData data;
        data.manufacturerDataMap = BlueZ::ParseManufacturerData(*iter);
        if (data.manufacturerDataMap.empty()) {
            return;
        }

        data.address = BlueZ::ParseAddress(changed.value("Address").toString())
                           .value_or(AddressFromPath(path).value_or(0));
        data.timestamp = std::chrono::system_clock::now();

        if (const auto rssi = rssiCache.find(path); rssi != rssiCache.end()) {
            data.rssi = rssi->second;
        }
        else {
            data.rssi = static_cast<int16_t>(changed.value("RSSI", -127).toInt());
        }

        owner.CbReceived().Invoke(data);
    }
};

AdvertisementWatcher::AdvertisementWatcher() : _impl{std::make_unique<Impl>(*this)} {}

AdvertisementWatcher::~AdvertisementWatcher()
{
    Stop();
}

bool AdvertisementWatcher::Start()
{
    APD_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    auto &impl = *_impl;
    if (impl.wanted) {
        return true;
    }
    impl.wanted = true;

    impl.monitorToken = BlueZ::Monitor::Instance().Subscribe(
        [&impl](const QString &path, const QString &interface, const QVariantMap &changed) {
            impl.OnPropertiesChanged(path, interface, changed);
        });
    impl.retryTimer.start();

    // A failure to start is reported through the state callback and retried by the timer, exactly
    // like the Windows watcher, so Start() itself only fails for a programming error.
    impl.TryStart();
    return true;
}

bool AdvertisementWatcher::Stop()
{
    auto &impl = *_impl;
    if (!impl.wanted) {
        return true;
    }
    impl.wanted = false;

    impl.retryTimer.stop();
    if (impl.monitorToken != 0) {
        BlueZ::Monitor::Instance().Unsubscribe(impl.monitorToken);
        impl.monitorToken = 0;
    }

    if (!impl.adapterPath.isEmpty()) {
        const auto error =
            BlueZ::CallMethod(impl.adapterPath, BlueZ::kAdapterInterface, "StopDiscovery", 5000);
        if (!error.isEmpty()) {
            LOG(Info, "BlueZ: StopDiscovery: '{}'", error);
        }
    }

    if (impl.reported == State::Started) {
        impl.Report(State::Stopped);
    }
    impl.reported.reset();
    return true;
}

} // namespace Core::Bluetooth
