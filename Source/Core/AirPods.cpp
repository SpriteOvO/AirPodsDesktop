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

#include "AirPods.h"

#include <algorithm>
#include <utility>

#include "GlobalMedia.h"
#include "../Error.h"
#include "../Helper.h"
#include "../Logger.h"

namespace Core::AirPods {

Manager::Manager(QObject *parent) : QObject{parent}
{
    _stateMgr.SetOnDiscardState([this] {
        QMetaObject::invokeMethod(this, [this] { emit Disconnected(); }, Qt::QueuedConnection);
    });

    _adWatcher.CbReceived() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnAdvertisementReceived(std::forward<decltype(args)>(args)...);
    };

    _adWatcher.CbStateChanged() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnAdvWatcherStateChanged(std::forward<decltype(args)>(args)...);
    };
}

Manager::~Manager()
{
    StopScanner();

    _deviceLookupThread.request_stop();
    if (_deviceLookupThread.joinable()) {
        _deviceLookupThread.join();
    }
}

void Manager::StartScanner()
{
    if (!_adWatcher.Start()) {
        LOG(Warn, "Bluetooth AdvWatcher start failed.");
    }
    else {
        LOG(Info, "Bluetooth AdvWatcher start succeeded.");
    }
}

void Manager::StopScanner()
{
    if (!_adWatcher.Stop()) {
        LOG(Warn, "AsyncScanner::Stop() failed.");
    }
    else {
        LOG(Info, "AsyncScanner::Stop() succeeded.");
    }
}

void Manager::OnRssiMinChanged(int16_t rssiMin)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _stateMgr.OnRssiMinChanged(rssiMin);
}

void Manager::OnAutomaticEarDetectionChanged(bool enable)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _automaticEarDetection = enable;
}

void Manager::OnBoundDeviceAddressChanged(uint64_t address)
{
    _deviceLookupThread.request_stop();
    if (_deviceLookupThread.joinable()) {
        _deviceLookupThread.join();
    }

    std::lock_guard<std::mutex> lock{_mutex};

    _requestedDeviceAddress = address;
    _boundDevice.reset();
    _boundModel = Model::Unknown;
    _deviceConnected = false;
    _stateMgr.Disconnect();

    if (address == 0) {
        LOG(Info, "Unbind device.");
        return;
    }

    LOG(Info, "Bind a new device.");

    _deviceLookupThread = std::jthread{[this, address](std::stop_token stopToken) {
        OS::Windows::Winrt::Initialize();
        auto device = Bluetooth::DeviceManager::FindDevice(address);
        if (stopToken.stop_requested()) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, address, device = std::move(device)]() mutable {
                CompleteBoundDeviceLookup(address, std::move(device));
            },
            Qt::QueuedConnection);
    }};
}

void Manager::CompleteBoundDeviceLookup(uint64_t address, std::optional<Bluetooth::Device> device)
{
    std::lock_guard<std::mutex> lock{_mutex};

    if (address != _requestedDeviceAddress) {
        LOG(Info, "Ignore a stale bound-device lookup result.");
        return;
    }

    if (!device.has_value()) {
        LOG(Warn, "The bound device address is no longer available.");
        emit BoundDeviceUnavailable();
        return;
    }

    _boundDevice = std::move(device);
    _boundModel = AppleCP::AirPods::GetModel(_boundDevice->GetProductId());

    _deviceName = QString::fromStdString([&] {
        auto name = _boundDevice->GetName();
        return name.find("Bluetooth") != std::string::npos ? std::string{} : name;
    }());

    _boundDevice->CbConnectionStatusChanged() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnBoundDeviceConnectionStateChanged(std::forward<decltype(args)>(args)...);
    };

    OnBoundDeviceConnectionStateChanged(_boundDevice->GetConnectionState());
}

void Manager::OnBoundDeviceConnectionStateChanged(Bluetooth::DeviceState state)
{
    bool newDeviceConnected = state == Bluetooth::DeviceState::Connected;
    bool doDisconnect = _deviceConnected && !newDeviceConnected;
    _deviceConnected = newDeviceConnected;

    if (doDisconnect) {
        _stateMgr.Disconnect();
    }

    LOG(Info, "The device we bound is updated. current: {}, new: {}", _deviceConnected,
        newDeviceConnected);
}

void Manager::OnStateChanged(Details::StateManager::UpdateEvent updateEvent)
{
    const auto &oldState = updateEvent.oldState;
    auto &newState = updateEvent.newState;

    newState.displayName =
        _deviceName.isEmpty() ? Helper::ToString(newState.model) : _deviceName.remove(" - Find My");

    emit StateUpdated(newState);

    bool newLidOpened = newState.caseBox.isLidOpened && newState.caseBox.isBothPodsInCase;
    bool lidStateSwitched;
    if (!oldState.has_value()) {
        lidStateSwitched = newLidOpened;
    }
    else {
        bool oldLidOpened = oldState->caseBox.isLidOpened && oldState->caseBox.isBothPodsInCase;
        lidStateSwitched = oldLidOpened != newLidOpened;
    }
    if (lidStateSwitched) {
        OnLidOpened(newLidOpened);
    }

    if (oldState.has_value()) {
        bool oldBothInEar = oldState->pods.left.isInEar && oldState->pods.right.isInEar;
        bool newBothInEar = newState.pods.left.isInEar && newState.pods.right.isInEar;
        if (oldBothInEar != newBothInEar) {
            OnBothInEar(newBothInEar);
        }
    }
}

void Manager::OnLidOpened(bool opened)
{
    emit LidToggled(opened);
}

void Manager::OnBothInEar(bool isBothInEar)
{
    if (!_automaticEarDetection) {
        LOG(Info, "automatic_ear_detection: Do nothing because it is disabled. ({})", isBothInEar);
        return;
    }

    if (isBothInEar) {
        GlobalMedia::Play();
    }
    else {
        GlobalMedia::Pause();
    }
}

bool Manager::OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    if (!Details::Advertisement::IsDesiredAdv(data)) {
        return false;
    }

    Details::Advertisement adv{data};

    LOG(Trace, "AirPods advertisement received. Data: {}, Address Hash: {}, RSSI: {}",
        Helper::ToString(adv.GetDesensitizedData()), Helper::Hash(data.address), data.rssi);

    if (!_deviceConnected) {
        LOG(Info, "AirPods advertisement received, but device disconnected.");
        return false;
    }

    if (_boundModel != Model::Unknown && adv.GetAdvState().model != _boundModel) {
        LOG(Trace, "Ignore advertisement for a model other than the bound device.");
        return false;
    }

    auto optUpdateEvent = _stateMgr.OnAdvReceived(std::move(adv));
    if (optUpdateEvent.has_value()) {
        OnStateChanged(std::move(optUpdateEvent.value()));
    }
    return true;
}

void Manager::OnAdvWatcherStateChanged(
    Bluetooth::AdvertisementWatcher::State state, const std::optional<std::string> &optError)
{
    switch (state) {
    case Bluetooth::AdvertisementWatcher::State::Started:
        emit ScannerAvailabilityChanged(true);
        LOG(Info, "Bluetooth AdvWatcher started.");
        break;

    case Bluetooth::AdvertisementWatcher::State::Stopped:
        emit ScannerAvailabilityChanged(false);
        LOG(Warn, "Bluetooth AdvWatcher stopped. Error: '{}'.", optError.value_or("nullopt"));
        break;

    default:
        FatalError("Unhandled adv watcher state: '{}'", Helper::ToUnderlying(state));
    }
}

std::vector<Bluetooth::Device> GetDevices()
{
    std::vector<Bluetooth::Device> devices =
        Bluetooth::DeviceManager::GetDevicesByState(Bluetooth::DeviceState::Paired);

    LOG(Info, "Paired devices count: {}", devices.size());

    devices.erase(
        std::remove_if(
            devices.begin(), devices.end(),
            [](const auto &device) {
                const auto vendorId = device.GetVendorId();
                const auto productId = device.GetProductId();
                const auto doErase = vendorId != AppleCP::VendorId ||
                                     AppleCP::AirPods::GetModel(productId) == Model::Unknown;

                LOG(Trace, "Device VendorId: '{}', ProductId: '{}', doErase: {}", vendorId,
                    productId, doErase);
                return doErase;
            }),
        devices.end());

    LOG(Info, "AirPods devices count: {} (filtered)", devices.size());
    return devices;
}

} // namespace Core::AirPods
