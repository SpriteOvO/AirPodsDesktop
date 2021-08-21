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

#include <spdlog/spdlog.h>

#include "Bluetooth.h"
#include "AppleCP.h"
#include "GlobalMedia.h"
#include "../Helper.h"
#include "../Assert.h"
#include "../Application.h"
#include "../Gui/InfoWindow.h"

using namespace Core;
using namespace std::chrono_literals;

namespace Core::AirPods {
namespace Details {

class Advertisement
{
public:
    using AddressType = decltype(Bluetooth::AdvertisementWatcher::ReceivedData::address);

    struct AdvState : AirPods::State {
        Side side;
    };

    static bool IsDesiredAdv(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
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

    Advertisement(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
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

    int16_t GetRssi() const
    {
        return _data.rssi;
    }

    const auto &GetTimestamp() const
    {
        return _data.timestamp;
    }

    AddressType GetAddress() const
    {
        return _data.address;
    }

    std::vector<uint8_t> GetDesensitizedData() const
    {
        auto desensitizedData = _protocol.Desensitize();

        std::vector<uint8_t> result(sizeof(desensitizedData), 0);
        std::memcpy(result.data(), &desensitizedData, sizeof(desensitizedData));
        return result;
    }

    const AdvState &GetAdvState() const
    {
        return _state;
    }

private:
    Bluetooth::AdvertisementWatcher::ReceivedData _data;
    AppleCP::AirPods _protocol;
    AdvState _state;

    const std::vector<uint8_t> &GetMfrData() const
    {
        auto iter = _data.manufacturerDataMap.find(AppleCP::VendorId);
        APD_ASSERT(iter != _data.manufacturerDataMap.end());

        return (*iter).second;
    }
};

// AirPods use Random Non-resolvable device addresses for privacy reasons. This means we
// can't "Remember" the user's AirPods by any device property. Here we track our desired
// devices in some non-elegant ways, but obviously it is sometimes unreliable.
//
class Tracker
{
public:
    using FnStateChanged =
        std::function<void(const std::optional<State> &oldState, const State &newState)>;
    using FnLosted = std::function<void()>;

    Tracker()
    {
        _lostTimer.Start(10s, [this] {
            std::lock_guard<std::mutex> lock{_mutex};
            DoLost();
        });

        _stateResetLeftTimer.Start(10s, [this] {
            std::lock_guard<std::mutex> lock{_mutex};
            DoStateReset(Side::Left);
        });
        _stateResetRightTimer.Start(10s, [this] {
            std::lock_guard<std::mutex> lock{_mutex};
            DoStateReset(Side::Right);
        });
    }

    auto &CbStateChanged()
    {
        return _cbStateChanged;
    }
    auto &CbLosted()
    {
        return _cbLosted;
    }

    void Disconnect()
    {
        std::lock_guard<std::mutex> lock{_mutex};
        DoLost();
    }

    bool TryTrack(Advertisement adv)
    {
        const auto rssiMin = Settings::ConstAccess()->rssi_min;
        const auto advRssi = adv.GetRssi();

        if (advRssi < rssiMin) {
            SPDLOG_WARN(
                "TryTrack returns false. Reason: RSSI is less than the limit. "
                "curr: '{}' min: '{}'",
                advRssi, rssiMin);
            return false;
        }

        const auto &advState = adv.GetAdvState();

        std::lock_guard<std::mutex> lock{_mutex};

        auto &lastAdv = advState.side == Side::Left ? _leftAdv : _rightAdv;
        auto &lastAnotherAdv = advState.side == Side::Left ? _rightAdv : _leftAdv;

        // If the Random Non-resolvable Address of our devices is changed
        // or the packet is sent from another device that it isn't ours
        //
        if (lastAdv.has_value() && lastAdv->GetAddress() != adv.GetAddress()) {
            const auto &lastAdvState = lastAdv->GetAdvState();

            if (advState.model != lastAdvState.model) {
                SPDLOG_WARN(
                    "TryTrack returns false. Reason: model new='{}' old='{}'",
                    Helper::ToString(advState.model), Helper::ToString(lastAdvState.model));
                return false;
            }

            Battery::value_type leftBatteryDiff = 0, rightBatteryDiff = 0, caseBatteryDiff = 0;

            if (advState.pods.left.battery.has_value() &&
                lastAdvState.pods.left.battery.has_value()) {
                leftBatteryDiff = std::abs(
                    (int32_t)advState.pods.left.battery.value() -
                    (int32_t)lastAdvState.pods.left.battery.value());
            }
            if (advState.pods.right.battery.has_value() &&
                lastAdvState.pods.right.battery.has_value()) {
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
                SPDLOG_WARN(
                    "TryTrack returns false. Reason: BatteryDiff l='{}' r='{}' c='{}'",
                    leftBatteryDiff, rightBatteryDiff, caseBatteryDiff);
                return false;
            }

            int16_t rssiDiff = std::abs(adv.GetRssi() - lastAdv->GetRssi());
            if (rssiDiff > 50) {
                SPDLOG_WARN("TryTrack returns false. Reason: Current side rssiDiff '{}'", rssiDiff);
                return false;
            }

            SPDLOG_WARN("Address changed, but it might still be the same device.");
        }
        if (lastAnotherAdv.has_value()) {
            int16_t rssiDiff = std::abs(adv.GetRssi() - lastAnotherAdv->GetRssi());
            if (rssiDiff > 50) {
                SPDLOG_WARN("TryTrack returns false. Reason: Another side rssiDiff '{}'", rssiDiff);
                return false;
            }
        }

        _lostTimer.Reset();
        if (advState.side == Side::Left) {
            _stateResetLeftTimer.Reset();
        }
        else if (advState.side == Side::Right) {
            _stateResetRightTimer.Reset();
        }

        lastAdv = std::move(adv);

        //////////////////////////////////////////////////
        // Update states
        //

        Advertisement::AdvState leftAdvState, rightAdvState;

        if (_leftAdv.has_value()) {
            leftAdvState = _leftAdv->GetAdvState();
        }
        if (_rightAdv.has_value()) {
            rightAdvState = _rightAdv->GetAdvState();
        }

        State newState;

        auto &ll = leftAdvState.pods.left;
        auto &rl = rightAdvState.pods.left;
        auto &lr = leftAdvState.pods.right;
        auto &rr = rightAdvState.pods.right;
        auto &lc = leftAdvState.caseBox;
        auto &rc = rightAdvState.caseBox;

        newState.model =
            leftAdvState.model != Model::Unknown ? leftAdvState.model : rightAdvState.model;

        newState.pods.left = ll.battery.has_value() ? std::move(ll) : std::move(rl);
        newState.pods.right = rr.battery.has_value() ? std::move(rr) : std::move(lr);
        newState.caseBox = rc.battery.has_value() ? std::move(rc) : std::move(lc);

        if (newState != _cachedState) {
            _cbStateChanged.Invoke(_cachedState, newState);
            _cachedState = std::move(newState);
        }
        return true;
    }

    std::optional<State> GetState() const
    {
        std::lock_guard<std::mutex> lock{_mutex};
        return _cachedState;
    }

private:
    using Clock = std::chrono::system_clock;
    using Timestamp = std::chrono::time_point<Clock>;

    mutable std::mutex _mutex;
    std::optional<Advertisement> _leftAdv, _rightAdv;
    std::optional<State> _cachedState;

    Helper::Timer _lostTimer, _stateResetLeftTimer, _stateResetRightTimer;
    Helper::Callback<FnStateChanged> _cbStateChanged;
    Helper::Callback<FnLosted> _cbLosted;

    void DoLost()
    {
        if (_leftAdv.has_value() || _rightAdv.has_value() || _cachedState.has_value()) {
            SPDLOG_INFO("Tracker: DoLost called.");
            _cbLosted.Invoke();
        }

        _leftAdv.reset();
        _rightAdv.reset();
        _cachedState.reset();
    }

    void DoStateReset(Side side)
    {
        auto &adv = side == Side::Left ? _leftAdv : _rightAdv;
        if (adv.has_value()) {
            SPDLOG_INFO("Tracker: DoStateReset called. Side: {}", Helper::ToString(side));
            adv.reset();
        }
    }
};

class Manager : Helper::NonCopyable
{
public:
    static Manager &GetInstance()
    {
        static Manager i;
        return i;
    }

    Manager()
    {
        _tracker.CbStateChanged() += [this](auto &&...args) {
            std::lock_guard<std::recursive_mutex> lock{_mutex};
            OnStateChanged(std::forward<decltype(args)>(args)...);
        };
        _tracker.CbLosted() += [this](auto &&...args) {
            std::lock_guard<std::recursive_mutex> lock{_mutex};
            OnLost(std::forward<decltype(args)>(args)...);
        };

        _adWatcher.CbReceived() += [this](auto &&...args) {
            std::lock_guard<std::recursive_mutex> lock{_mutex};
            OnAdvertisementReceived(std::forward<decltype(args)>(args)...);
        };
        _adWatcher.CbStateChanged() += [this](auto &&...args) {
            std::lock_guard<std::recursive_mutex> lock{_mutex};
            OnAdvWatcherStateChanged(std::forward<decltype(args)>(args)...);
        };
    }

    void StartScanner()
    {
        if (!_adWatcher.Start()) {
            SPDLOG_WARN("Bluetooth AdvWatcher start failed.");
        }
        else {
            SPDLOG_INFO("Bluetooth AdvWatcher start succeeded.");
        }
    }

    void StopScanner()
    {
        if (!_adWatcher.Stop()) {
            SPDLOG_WARN("AsyncScanner::Stop() failed.");
        }
        else {
            SPDLOG_INFO("AsyncScanner::Stop() succeeded.");
        }
    }

    QString GetDisplayName()
    {
        std::lock_guard<std::recursive_mutex> lock{_mutex};
        return _displayName;
    }

    void OnBoundDeviceAddressChanged(uint64_t address)
    {
        std::unique_lock<std::recursive_mutex> lock{_mutex};

        _boundDevice.reset();
        _deviceConnected = false;
        _tracker.Disconnect();
        GlobalMedia::OnLimitedDeviceStateChanged({});

        // Unbind device
        //
        if (address == 0) {
            SPDLOG_INFO("Unbind device.");
            return;
        }

        // Bind to a new device
        //
        SPDLOG_INFO("Bind a new device.");

        auto optDevice = Bluetooth::DeviceManager::FindDevice(address);
        if (!optDevice.has_value()) {
            SPDLOG_ERROR("Find device by address failed.");
            return;
        }

        _boundDevice = std::move(optDevice);

        auto currentState = _boundDevice->GetConnectionState();
        _displayName = QString::fromStdString(_boundDevice->GetDisplayName());
        _boundDevice->CbConnectionStatusChanged() += [this](auto &&...args) {
            std::lock_guard<std::recursive_mutex> lock{_mutex};
            OnBoundDeviceConnectionStateChanged(std::forward<decltype(args)>(args)...);
        };

        OnBoundDeviceConnectionStateChanged(currentState);
    }

    void OnQuit()
    {
        StopScanner();
    }

private:
    Tracker _tracker;
    Bluetooth::AdvertisementWatcher _adWatcher;
    std::optional<Bluetooth::Device> _boundDevice;
    QString _displayName;
    bool _deviceConnected{false};
    std::recursive_mutex _mutex;

    void OnBoundDeviceConnectionStateChanged(Bluetooth::DeviceState state)
    {
        bool newDeviceConnected = state == Bluetooth::DeviceState::Connected;
        bool doDisconnect = _deviceConnected && !newDeviceConnected;
        _deviceConnected = newDeviceConnected;

        if (doDisconnect) {
            _tracker.Disconnect();
            ApdApp->GetInfoWindow()->DisconnectSafety();
        }

        SPDLOG_INFO(
            "The device we bound is updated. current: {}, new: {}", _deviceConnected,
            newDeviceConnected);

        GlobalMedia::OnLimitedDeviceStateChanged((_displayName + " Stereo").toStdString());
    }

    void OnStateChanged(const std::optional<State> &oldState, const State &newState)
    {
        if (!_deviceConnected) {
            SPDLOG_INFO("AirPods state changed, but device disconnected.");
            return;
        }

        ApdApp->GetInfoWindow()->UpdateStateSafety(newState);

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

    void OnLost()
    {
        ApdApp->GetInfoWindow()->DisconnectSafety();
    }

    void OnLidOpened(bool opened)
    {
        auto &infoWindow = ApdApp->GetInfoWindow();
        if (opened) {
            infoWindow->ShowSafety();
        }
        else {
            infoWindow->HideSafety();
        }
    }

    void OnBothInEar(bool isBothInEar)
    {
        if (!Settings::ConstAccess()->automatic_ear_detection) {
            SPDLOG_INFO(
                "automatic_ear_detection: Do nothing because it is disabled. ({})", isBothInEar);
            return;
        }

        if (isBothInEar) {
            Core::GlobalMedia::Play();
        }
        else {
            Core::GlobalMedia::Pause();
        }
    }

    bool OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
    {
        if (!Advertisement::IsDesiredAdv(data)) {
            return false;
        }

        if (!_deviceConnected) {
            SPDLOG_INFO("AirPods advertisement received, but device disconnected.");
            return false;
        }

        Advertisement adv{data};

        SPDLOG_TRACE(
            "AirPods advertisement received. Data: {}, Address Hash: {}, RSSI: {}",
            Helper::ToString(adv.GetDesensitizedData()), Helper::Hash(data.address), data.rssi);

        if (!_tracker.TryTrack(adv)) {
            SPDLOG_WARN("It doesn't seem to be the device we desired.");
            return false;
        }
        return true;
    }

    void OnAdvWatcherStateChanged(
        Bluetooth::AdvertisementWatcher::State state, const std::optional<std::string> &optError)
    {
        switch (state) {
        case Core::Bluetooth::AdvertisementWatcher::State::Started:
            ApdApp->GetInfoWindow()->DisconnectSafety();
            SPDLOG_INFO("Bluetooth AdvWatcher started.");
            break;

        case Core::Bluetooth::AdvertisementWatcher::State::Stopped:
            ApdApp->GetInfoWindow()->UnavailableSafety();
            SPDLOG_WARN("Bluetooth AdvWatcher stopped. Error: '{}'.", optError.value_or("nullopt"));
            break;

        default:
            FatalError("Unhandled adv watcher state: '{}'", Helper::ToUnderlying(state));
        }
    }
};
} // namespace Details

void StartScanner()
{
    Details::Manager::GetInstance().StartScanner();
}

QString GetDisplayName()
{
    return Details::Manager::GetInstance().GetDisplayName();
}

void OnBoundDeviceAddressChanged(uint64_t address)
{
    Details::Manager::GetInstance().OnBoundDeviceAddressChanged(address);
}

void OnQuit()
{
    Details::Manager::GetInstance().OnQuit();
}
} // namespace Core::AirPods
