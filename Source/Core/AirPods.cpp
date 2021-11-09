//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021 SpriteOvO
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

#include <mutex>
#include <chrono>
#include <thread>
#include <QVector>
#include <QMetaObject>

#include "Bluetooth.h"
#include "GlobalMedia.h"
#include "../Helper.h"
#include "../Logger.h"
#include "../Assert.h"
#include "../Application.h"
#include "../Gui/MainWindow.h"

using namespace Core;
using namespace std::chrono_literals;

namespace Core::AirPods {
namespace Details {

//
// Advertisement
//

bool Advertisement::IsDesiredAdv(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    auto iter = data.manufacturerDataMap.find(AppleCP::VendorId);
    if (iter == data.manufacturerDataMap.end()) {
        return false;
    }

    const auto &manufacturerData = (*iter).second;
    if (!AppleCP::AirPods::IsValid(manufacturerData)) {
        return false;
    }

    return true;
}

Advertisement::Advertisement(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    APD_ASSERT(IsDesiredAdv(data));
    _data = data;

    auto protocol = AppleCP::As<AppleCP::AirPods>(GetMfrData());
    APD_ASSERT(protocol.has_value());
    _protocol = std::move(protocol.value());

    // Store state
    //

    _state.model = _protocol.GetModel();
    _state.side = _protocol.GetBroadcastedSide();

    _state.pods.left.battery = _protocol.GetLeftBattery();
    _state.pods.left.isCharging = _protocol.IsLeftCharging();
    _state.pods.left.isInEar = _protocol.IsLeftInEar();

    _state.pods.right.battery = _protocol.GetRightBattery();
    _state.pods.right.isCharging = _protocol.IsRightCharging();
    _state.pods.right.isInEar = _protocol.IsRightInEar();

    _state.caseBox.battery = _protocol.GetCaseBattery();
    _state.caseBox.isCharging = _protocol.IsCaseCharging();

    _state.caseBox.isBothPodsInCase = _protocol.IsBothPodsInCase();
    _state.caseBox.isLidOpened = _protocol.IsLidOpened();

    if (_state.pods.left.battery.has_value()) {
        _state.pods.left.battery = _state.pods.left.battery.value() * 10;
    }
    if (_state.pods.right.battery.has_value()) {
        _state.pods.right.battery = _state.pods.right.battery.value() * 10;
    }
    if (_state.caseBox.battery.has_value()) {
        _state.caseBox.battery = _state.caseBox.battery.value() * 10;
    }
}

int16_t Advertisement::GetRssi() const
{
    return _data.rssi;
}

const auto &Advertisement::GetTimestamp() const
{
    return _data.timestamp;
}

auto Advertisement::GetAddress() const -> AddressType
{
    return _data.address;
}

std::vector<uint8_t> Advertisement::GetDesensitizedData() const
{
    auto desensitizedData = _protocol.Desensitize();

    std::vector<uint8_t> result(sizeof(desensitizedData), 0);
    std::memcpy(result.data(), &desensitizedData, sizeof(desensitizedData));
    return result;
}

auto Advertisement::GetAdvState() const -> const AdvState &
{
    return _state;
}

const std::vector<uint8_t> &Advertisement::GetMfrData() const
{
    auto iter = _data.manufacturerDataMap.find(AppleCP::VendorId);
    APD_ASSERT(iter != _data.manufacturerDataMap.end());

    return (*iter).second;
}

//
// StateManager
//

StateManager::StateManager()
{
    _lostTimer.Start(10s, [this] {
        std::lock_guard<std::mutex> lock{_mutex};
        DoLost();
    });

    _stateResetTimer.left.Start(10s, [this] {
        std::lock_guard<std::mutex> lock{_mutex};
        DoStateReset(Side::Left);
    });

    _stateResetTimer.right.Start(10s, [this] {
        std::lock_guard<std::mutex> lock{_mutex};
        DoStateReset(Side::Right);
    });
}

std::optional<State> StateManager::GetCurrentState() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _cachedState;
}

auto StateManager::OnAdvReceived(Advertisement adv) -> std::optional<UpdateEvent>
{
    std::lock_guard<std::mutex> lock{_mutex};

    if (!IsPossibleDesiredAdv(adv)) {
        LOG(Warn, "This adv may not be broadcast from the device we desire.");
        return std::nullopt;
    }

    UpdateAdv(std::move(adv));
    return UpdateState();
}

void StateManager::Disconnect()
{
    std::lock_guard<std::mutex> lock{_mutex};

    LOG(Info, "StateManager: Disconnect.");
    ResetAll();
}

void StateManager::OnRssiMinChanged(int16_t rssiMin)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _rssiMin = rssiMin;
}

bool StateManager::IsPossibleDesiredAdv(const Advertisement &adv) const
{
    const auto advRssi = adv.GetRssi();
    if (advRssi < _rssiMin) {
        LOG(Warn,
            "IsPossibleDesiredAdv returns false. Reason: RSSI is less than the limit. "
            "curr: '{}' min: '{}'",
            advRssi, _rssiMin);
        return false;
    }

    const auto &advState = adv.GetAdvState();

    auto &lastAdv = advState.side == Side::Left ? _adv.left : _adv.right;
    auto &lastAnotherAdv = advState.side == Side::Left ? _adv.right : _adv.left;

    // If the Random Non-resolvable Address of our devices is changed
    // or the packet is sent from another device that it isn't ours
    //
    if (lastAdv.has_value() && lastAdv->first.GetAddress() != adv.GetAddress()) {
        const auto &lastAdvState = lastAdv->first.GetAdvState();

        if (advState.model != lastAdvState.model) {
            LOG(Warn, "IsPossibleDesiredAdv returns false. Reason: model new='{}' old='{}'",
                Helper::ToString(advState.model), Helper::ToString(lastAdvState.model));
            return false;
        }

        Battery::value_type leftBatteryDiff = 0, rightBatteryDiff = 0, caseBatteryDiff = 0;

        if (advState.pods.left.battery.has_value() && lastAdvState.pods.left.battery.has_value()) {
            leftBatteryDiff = std::abs(
                (int32_t)advState.pods.left.battery.value() -
                (int32_t)lastAdvState.pods.left.battery.value());
        }
        if (advState.pods.right.battery.has_value() && lastAdvState.pods.right.battery.has_value())
        {
            rightBatteryDiff = std::abs(
                (int32_t)advState.pods.right.battery.value() -
                (int32_t)lastAdvState.pods.right.battery.value());
        }
        if (advState.caseBox.battery.has_value() && lastAdvState.caseBox.battery.has_value()) {
            caseBatteryDiff = std::abs(
                (int32_t)advState.caseBox.battery.value() -
                (int32_t)lastAdvState.caseBox.battery.value());
        }

        // The battery changes in steps of 1, so the data of two packets in a short time
        // can not exceed 1, otherwise it is not our device
        //
        if (leftBatteryDiff > 1 || rightBatteryDiff > 1 || caseBatteryDiff > 1) {
            LOG(Warn,
                "IsPossibleDesiredAdv returns false. Reason: BatteryDiff l='{}' r='{}' c='{}'",
                leftBatteryDiff, rightBatteryDiff, caseBatteryDiff);
            return false;
        }

        int16_t rssiDiff = std::abs(advRssi - lastAdv->first.GetRssi());
        if (rssiDiff > 50) {
            LOG(Warn, "IsPossibleDesiredAdv returns false. Reason: Current side rssiDiff '{}'",
                rssiDiff);
            return false;
        }

        LOG(Warn, "Address changed, but it might still be the same device.");
    }

    if (lastAnotherAdv.has_value()) {
        int16_t rssiDiff = std::abs(advRssi - lastAnotherAdv->first.GetRssi());
        if (rssiDiff > 50) {
            LOG(Warn, "IsPossibleDesiredAdv returns false. Reason: Another side rssiDiff '{}'",
                rssiDiff);
            return false;
        }
    }

    return true;
}

void StateManager::UpdateAdv(Advertisement adv)
{
    _lostTimer.Reset();

    const auto &advState = adv.GetAdvState();

    if (advState.side == Side::Left) {
        _stateResetTimer.left.Reset();
        _adv.left = std::make_pair(std::move(adv), Clock::now());
    }
    else if (advState.side == Side::Right) {
        _stateResetTimer.right.Reset();
        _adv.right = std::make_pair(std::move(adv), Clock::now());
    }
}

auto StateManager::UpdateState() -> std::optional<UpdateEvent>
{
    Helper::Sides<std::pair<Advertisement::AdvState, Timestamp>> cachedAdvState;

    if (_adv.left.has_value()) {
        cachedAdvState.left = std::make_pair(_adv.left->first.GetAdvState(), _adv.left->second);
    }
    if (_adv.right.has_value()) {
        cachedAdvState.right = std::make_pair(_adv.right->first.GetAdvState(), _adv.right->second);
    }

    State newState;

#define PICK_SIDE(available_condition_field)                                                       \
    [&]() -> decltype(auto) {                                                                      \
        const Helper::Sides<bool> available = {                                                    \
            .left = cachedAdvState.left.first.available_condition_field,                           \
            .right = cachedAdvState.right.first.available_condition_field,                         \
        };                                                                                         \
        if (available.left && available.right) {                                                   \
            return cachedAdvState.left.second > cachedAdvState.right.second                        \
                       ? cachedAdvState.left.first                                                 \
                       : cachedAdvState.right.first;                                               \
        }                                                                                          \
        else {                                                                                     \
            return available.left ? cachedAdvState.left.first : cachedAdvState.right.first;        \
        }                                                                                          \
    }()

    newState.model = PICK_SIDE(model != Model::Unknown).model;

    newState.pods.left = std::move(PICK_SIDE(pods.left.battery.has_value()).pods.left);
    newState.pods.right = std::move(PICK_SIDE(pods.right.battery.has_value()).pods.right);
    newState.caseBox = std::move(PICK_SIDE(caseBox.battery.has_value()).caseBox);

#undef PICK_SIDE

    if (newState == _cachedState) {
        return std::nullopt;
    }

    auto oldState = std::move(_cachedState);
    _cachedState = std::move(newState);

    return UpdateEvent{.oldState = std::move(oldState), .newState = _cachedState.value()};
}

void StateManager::ResetAll()
{
    if (_cachedState.has_value()) {
        ApdApp->GetMainWindow()->DisconnectSafety();
    }

    _adv.left.reset();
    _adv.right.reset();
    _cachedState.reset();
}

void StateManager::DoLost()
{
    LOG(Info, "StateManager: Device is lost.");
    ResetAll();
}

void StateManager::DoStateReset(Side side)
{
    auto &adv = side == Side::Left ? _adv.left : _adv.right;
    if (adv.has_value()) {
        LOG(Info, "StateManager: DoStateReset called. Side: {}", Helper::ToString(side));
        adv.reset();
        UpdateState();
    }
}

//
// Manager
//

Manager::Manager()
{
    _adWatcher.CbReceived() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnAdvertisementReceived(std::forward<decltype(args)>(args)...);
    };

    _adWatcher.CbStateChanged() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnAdvWatcherStateChanged(std::forward<decltype(args)>(args)...);
    };
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

QString Manager::GetDisplayName()
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _displayName;
}

std::optional<State> Manager::GetCurrentState()
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _stateMgr.GetCurrentState();
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
    std::unique_lock<std::mutex> lock{_mutex};

    _boundDevice.reset();
    _deviceConnected = false;
    _stateMgr.Disconnect();
    GlobalMedia::OnLimitedDeviceStateChanged({});

    // Unbind device
    //
    if (address == 0) {
        LOG(Info, "Unbind device.");
        return;
    }

    // Bind to a new device
    //
    LOG(Info, "Bind a new device.");

    auto optDevice = Bluetooth::DeviceManager::FindDevice(address);
    if (!optDevice.has_value()) {
        LOG(Error, "Find device by address failed.");
        return;
    }

    _boundDevice = std::move(optDevice);

    auto currentState = _boundDevice->GetConnectionState();
    _displayName = QString::fromStdString(_boundDevice->GetDisplayName());
    _boundDevice->CbConnectionStatusChanged() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnBoundDeviceConnectionStateChanged(std::forward<decltype(args)>(args)...);
    };

    OnBoundDeviceConnectionStateChanged(currentState);
}

void Manager::OnQuit()
{
    StopScanner();
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

    GlobalMedia::OnLimitedDeviceStateChanged((_displayName + " Stereo").toStdString());
}

void Manager::OnStateChanged(StateManager::UpdateEvent updateEvent)
{
    const auto &oldState = updateEvent.oldState;
    const auto &newState = updateEvent.newState;

    ApdApp->GetMainWindow()->UpdateStateSafety(newState);

    // Lid opened
    //
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

    // Both in ear
    //
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
    auto &mainWindow = ApdApp->GetMainWindow();
    if (opened) {
        mainWindow->ShowSafety();
    }
    else {
        mainWindow->HideSafety();
    }
}

void Manager::OnBothInEar(bool isBothInEar)
{
    if (!_automaticEarDetection) {
        LOG(Info, "automatic_ear_detection: Do nothing because it is disabled. ({})", isBothInEar);
        return;
    }

    if (isBothInEar) {
        Core::GlobalMedia::Play();
    }
    else {
        Core::GlobalMedia::Pause();
    }
}

bool Manager::OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    if (!Advertisement::IsDesiredAdv(data)) {
        return false;
    }

    if (!_deviceConnected) {
        LOG(Info, "AirPods advertisement received, but device disconnected.");
        return false;
    }

    Advertisement adv{data};

    LOG(Trace, "AirPods advertisement received. Data: {}, Address Hash: {}, RSSI: {}",
        Helper::ToString(adv.GetDesensitizedData()), Helper::Hash(data.address), data.rssi);

    auto optUpdateEvent = _stateMgr.OnAdvReceived(Advertisement{data});
    if (optUpdateEvent.has_value()) {
        OnStateChanged(std::move(optUpdateEvent.value()));
    }
    return true;
}

void Manager::OnAdvWatcherStateChanged(
    Bluetooth::AdvertisementWatcher::State state, const std::optional<std::string> &optError)
{
    switch (state) {
    case Core::Bluetooth::AdvertisementWatcher::State::Started:
        ApdApp->GetMainWindow()->AvailableSafety();
        LOG(Info, "Bluetooth AdvWatcher started.");
        break;

    case Core::Bluetooth::AdvertisementWatcher::State::Stopped:
        ApdApp->GetMainWindow()->UnavailableSafety();
        LOG(Warn, "Bluetooth AdvWatcher stopped. Error: '{}'.", optError.value_or("nullopt"));
        break;

    default:
        FatalError("Unhandled adv watcher state: '{}'", Helper::ToUnderlying(state));
    }
}

} // namespace Details

} // namespace Core::AirPods
