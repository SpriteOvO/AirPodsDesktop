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
        _lostTimer.Start(10s, [this] { DoLost(); });
        _stateResetLeftTimer.Start(10s, [this] { DoStateReset(Side::Left); });
        _stateResetRightTimer.Start(10s, [this] { DoStateReset(Side::Right); });
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
        DoLost();
    }

    bool TryTrack(Advertisement adv)
    {
        const auto &currentSettings = Settings::GetCurrent();
        const auto advRssi = adv.GetRssi();

        if (advRssi < currentSettings.rssi_min) {
            SPDLOG_WARN(
                "TryTrack returns false. Reason: RSSI is less than the limit. "
                "curr: '{}' min: '{}'",
                advRssi, currentSettings.rssi_min);
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
        std::lock_guard<std::mutex> lock{_mutex};

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
        std::lock_guard<std::mutex> lock{_mutex};

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

    std::optional<State> GetState() const
    {
        return _tracker.GetState();
    }

    void StartScanner()
    {
        if (_isScannerStarted) {
            SPDLOG_WARN("AsyncScanner::Start() return directly. Because it's already started.");
            return;
        }

        // TODO
        _isScannerStarted = true;
        _requireStartScanner = true;
        _scannerStartWorker.Start(5s, [this] { return ScannerStartWork(); });
    }

    Status StopScanner()
    {
        if (!_isScannerStarted) {
            SPDLOG_WARN("AsyncScanner::Stop() return directly. Because it's already stopped.");
            return Status::Success;
        }

        _scannerStartWorker.Stop();

        Status status = _adWatcher.Stop();
        if (status.IsFailed()) {
            SPDLOG_WARN("AsyncScanner::Stop() failed. Status: {}", status);
        }
        else {
            _isScannerStarted = false;
            UpdateUi(Action::Unavailable);
            SPDLOG_INFO("AsyncScanner::Stop() succeeded.");
        }

        return status;
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
            Helper::ToString(adv.GetDesensitizedData()),
            std::hash<decltype(data.address)>{}(data.address), data.rssi);

        if (!_tracker.TryTrack(adv)) {
            SPDLOG_WARN("It doesn't seem to be the device we desired.");
            return false;
        }
        return true;
    }

    void OnBoundDeviceAddressChanged(uint64_t address)
    {
        const auto &disconnect = [this]() {
            _boundDevice.reset();
            _deviceConnected = false;
            _tracker.Disconnect();
        };

        GlobalMedia::OnLimitedDeviceStateChanged();

        if (address == 0) {
            disconnect();
            UpdateUi(Action::WaitingForBinding);
            Utils::Qt::Dispatch(
                []() { App->GetInfoWindow()->ChangeButtonActionSafety(Gui::ButtonAction::Bind); });
            return;
        }
        UpdateUi(Action::Disconnected);

        auto optDevice = Bluetooth::DeviceManager::FindDevice(address);
        if (!optDevice.has_value()) {
            disconnect();
            return;
        }

        _deviceConnected = optDevice->GetConnectionState() == Bluetooth::DeviceState::Connected;

        _boundDevice = std::move(optDevice);

        _boundDevice->CbConnectionStatusChanged() += [this](Bluetooth::DeviceState state) {
            bool newDeviceConnected = state == Bluetooth::DeviceState::Connected;
            bool doDisconnet = _deviceConnected && !newDeviceConnected;
            _deviceConnected = newDeviceConnected;

            if (doDisconnet) {
                _tracker.Disconnect();
                UpdateUi(Action::Disconnected);
            }

            SPDLOG_INFO(
                "The device we bound is updated. current: {}, new: {}", _deviceConnected,
                newDeviceConnected);

            GlobalMedia::OnLimitedDeviceStateChanged();
        };
    }

    void OnQuit()
    {
        StopScanner();
    }

private:
    enum class Action : uint32_t {
        Unavailable,
        WaitingForBinding,
        Disconnected,
    };

    Tracker _tracker;

    using FnControlInfoWindow = std::function<void(const State &state, bool show)>;
    using FnBothInEar = std::function<void(const State &state, bool isBothInEar)>;

    Helper::Callback<FnControlInfoWindow> _cbControlInfoWindow;
    Helper::Callback<FnBothInEar> _cbBothInEar;

    Bluetooth::AdvertisementWatcher _adWatcher;
    std::optional<Bluetooth::Device> _boundDevice;

    std::atomic<Action> _lastAction{Action::Unavailable};
    std::atomic<bool> _isScannerStarted{false}, _requireStartScanner{false},
        _deviceConnected{false};
    Helper::ConWorker _scannerStartWorker;

    Manager()
    {
        _tracker.CbStateChanged() += [](auto, const State &newState) {
            App->GetInfoWindow()->UpdateStateSafety(newState);
            App->GetSysTray()->UpdateStateSafety(newState);
        };

        _cbControlInfoWindow += [](const State &state, bool show) {
            auto &infoWindow = App->GetInfoWindow();
            if (show) {
                infoWindow->ShowSafety();
            }
            else {
                infoWindow->HideSafety();
            }
        };

        _cbBothInEar += [](const State &state, bool isBothInEar) {
            if (!Settings::GetCurrent().automatic_ear_detection) {
                SPDLOG_TRACE(
                    "BothInEarCallbacks: Do nothing because the feature is disabled. ({})",
                    isBothInEar);
                return;
            }

            if (isBothInEar) {
                Core::GlobalMedia::Play();
            }
            else {
                Core::GlobalMedia::Pause();
            }
        };

        _tracker.CbStateChanged() +=
            [this](const std::optional<State> &oldState, const State &newState) {
                if (!_deviceConnected) {
                    SPDLOG_INFO("AirPods state changed, but device disconnected.");
                    return;
                }

                // Lid opened
                //
                bool newLidOpened =
                    newState.caseBox.isLidOpened && newState.caseBox.isBothPodsInCase;

                if (oldState.has_value()) {
                    bool cachedLidOpened =
                        oldState->caseBox.isLidOpened && oldState->caseBox.isBothPodsInCase;

                    if (cachedLidOpened != newLidOpened) {
                        _cbControlInfoWindow.Invoke(newState, newLidOpened);
                    }
                }
                else {
                    if (newLidOpened) {
                        _cbControlInfoWindow.Invoke(newState, newLidOpened);
                    }
                }

                // Both in ear
                //
                if (oldState.has_value()) {
                    bool cachedBothInEar =
                        oldState->pods.left.isInEar && oldState->pods.right.isInEar;
                    bool newBothInEar = newState.pods.left.isInEar && newState.pods.right.isInEar;
                    if (cachedBothInEar != newBothInEar) {
                        _cbBothInEar.Invoke(newState, newBothInEar);
                    }
                }
            };

        _tracker.CbLosted() += [this]() { UpdateUi(Action::Disconnected); };

        _adWatcher.ReceivedCallbacks() +=
            [this](const Bluetooth::AdvertisementWatcher::ReceivedData &receivedData) {
                OnAdvertisementReceived(receivedData);
            };

        _adWatcher.StoppedCallbacks() += [this](const std::optional<std::string> &optError) {
            UpdateUi(Action::Unavailable);
            SPDLOG_WARN(
                "Bluetooth AdvWatcher stopped. Error: '{}'.",
                optError.has_value() ? optError.value() : "nullopt");

            if (optError.has_value()) {
                SPDLOG_WARN("Try to restart.");
                _requireStartScanner = true;
                // _scannerStartWorker.Notify();
            }
        };
    }

    bool ScannerStartWork()
    {
        do {
            if (!_requireStartScanner) {
                break;
            }

            Status status = _adWatcher.Start();
            if (!status.IsSucceeded()) {
                UpdateUi(Action::Unavailable);
                SPDLOG_WARN("Bluetooth AdvWatcher start failed. status: {}", status);
                break;
            }

            _requireStartScanner = false;
            if (_lastAction == Action::Unavailable) {
                UpdateUi(Action::Disconnected);
            }
            SPDLOG_INFO("Bluetooth AdvWatcher start succeeded.");

        } while (false);

        return true;
    }

    void UpdateUi(Action action)
    {
        _lastAction = action;

        QString title;
        if (action == Action::Unavailable) {
            title = QDialog::tr("Unavailable");
        }
        else if (action == Action::WaitingForBinding) {
            title = QDialog::tr("Waiting for Binding");
        }
        else if (action == Action::Disconnected) {
            title = QDialog::tr("Disconnected");
        }

        Utils::Qt::Dispatch([=] {
            App->GetInfoWindow()->DisconnectSafety(title);
            App->GetSysTray()->DisconnectSafety(title);
        });
    }
};
} // namespace Details

std::optional<State> GetState()
{
    return Details::Manager::GetInstance().GetState();
}

void StartScanner()
{
    Details::Manager::GetInstance().StartScanner();
}

void OnBindDeviceChanged(uint64_t address)
{
    Details::Manager::GetInstance().OnBoundDeviceAddressChanged(address);
}

void OnQuit()
{
    Details::Manager::GetInstance().OnQuit();
}
} // namespace Core::AirPods
