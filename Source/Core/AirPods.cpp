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

namespace Core::AirPods
{
    namespace Details
    {
        class Advertisement
        {
        public:
            using AddressType = decltype(Bluetooth::AdvertisementWatcher::ReceivedData::address);

            struct AdvState : AirPods::State {
                Side side;
            };

            static bool IsDesiredAdv(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
            {
                auto iter = data.manufacturerDataMap.find(76);
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

            const auto& GetTimestamp() const
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

            const AdvState& GetAdvState() const {
                return _state;
            }

        private:
            Bluetooth::AdvertisementWatcher::ReceivedData _data;
            AppleCP::AirPods _protocol;
            AdvState _state;

            const std::vector<uint8_t>& GetMfrData() const
            {
                auto iter = _data.manufacturerDataMap.find(76);
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
            using FnStateChanged = std::function<
                void(const std::optional<State> &oldState, const State &newState)
            >;
            using FnLosted = std::function<void()>;

            Tracker() {
                _lostTimer.Start(10s, [this] { DoLost(); });
                _stateResetLeftTimer.Start(10s, [this] { DoStateReset(Side::Left); });
                _stateResetRightTimer.Start(10s, [this] { DoStateReset(Side::Right); });
            }

            auto& CbStateChanged()   { return _cbStateChanged; }
            auto& CbLosted()         { return _cbLosted; }

            bool TryTrack(Advertisement adv)
            {
                const auto &currentSettings = Settings::GetCurrent();
                const auto advRssi = adv.GetRssi();

                if (advRssi < currentSettings.rssi_min) {
                    spdlog::warn(
                        "TryTrack returns false. Reason: RSSI is less than the limit. "
                        "curr: '{}' min: '{}'",
                        advRssi, currentSettings.rssi_min
                    );
                    return false;
                }

                const auto &advState = adv.GetAdvState();

                std::lock_guard<std::mutex> lock{_mutex};

                auto &lastAdv = advState.side == Side::Left ? _leftAdv : _rightAdv;
                auto &lastAnotherAdv = advState.side == Side::Left ? _rightAdv : _leftAdv;

                // If the Random Non-resolvable Address of our devices is changed
                // or the packet is sent from another device that it isn't ours
                //
                if (lastAdv.has_value() && lastAdv->GetAddress() != adv.GetAddress())
                {
                    const auto &lastAdvState = lastAdv->GetAdvState();

                    if (advState.model != lastAdvState.model) {
                        spdlog::warn(
                            "TryTrack returns false. Reason: model new='{}' old='{}'",
                            Helper::ToString(advState.model), Helper::ToString(lastAdvState.model)
                        );
                        return false;
                    }

                    Battery::value_type leftBatteryDiff = 0, rightBatteryDiff = 0,
                        caseBatteryDiff = 0;

                    if (advState.pods.left.battery.has_value() &&
                        lastAdvState.pods.left.battery.has_value())
                    {
                        leftBatteryDiff = std::abs(
                            (int32_t)advState.pods.left.battery.value() -
                            (int32_t)lastAdvState.pods.left.battery.value()
                        );
                    }
                    if (advState.pods.right.battery.has_value() &&
                        lastAdvState.pods.right.battery.has_value())
                    {
                        rightBatteryDiff = std::abs(
                            (int32_t)advState.pods.right.battery.value() -
                            (int32_t)lastAdvState.pods.right.battery.value()
                        );
                    }
                    if (advState.caseBox.battery.has_value() &&
                        lastAdvState.caseBox.battery.has_value())
                    {
                        caseBatteryDiff = std::abs(
                            (int32_t)advState.caseBox.battery.value() -
                            (int32_t)lastAdvState.caseBox.battery.value()
                        );
                    }

                    // The battery changes in steps of 1, so the data of two packets in a short time
                    // can not exceed 1, otherwise it is not our device
                    //
                    if (leftBatteryDiff > 1 || rightBatteryDiff > 1 || caseBatteryDiff > 1) {
                        spdlog::warn(
                            "TryTrack returns false. Reason: BatteryDiff l='{}' r='{}' c='{}'",
                            leftBatteryDiff, rightBatteryDiff, caseBatteryDiff
                        );
                        return false;
                    }

                    int16_t rssiDiff = std::abs(adv.GetRssi() - lastAdv->GetRssi());
                    if (rssiDiff > 50) {
                        spdlog::warn(
                            "TryTrack returns false. Reason: Current side rssiDiff '{}'",
                            rssiDiff
                        );
                        return false;
                    }

                    spdlog::warn("Address changed, but it might still be the same device.");
                }
                if (lastAnotherAdv.has_value())
                {
                    int16_t rssiDiff = std::abs(adv.GetRssi() - lastAnotherAdv->GetRssi());
                    if (rssiDiff > 50) {
                        spdlog::warn(
                            "TryTrack returns false. Reason: Another side rssiDiff '{}'",
                            rssiDiff
                        );
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
                    spdlog::info("Tracker: DoLost called.");
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
                    spdlog::info("Tracker: DoStateReset called. Side: {}", Helper::ToString(side));
                    adv.reset();
                }
            }
        };

        class Manager : Helper::NonCopyable
        {
        public:
            static Manager& GetInstance() {
                static Manager i;
                return i;
            }

            bool OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
            {
                if (!Advertisement::IsDesiredAdv(data)) {
                    return false;
                }

                Advertisement adv{data};

                spdlog::trace(
                    "AirPods advertisement received. Data: {}, Address Hash: {}, RSSI: {}",
                    Helper::ToString(adv.GetDesensitizedData()),
                    std::hash<decltype(data.address)>{}(data.address),
                    data.rssi
                );

                if (!_tracker.TryTrack(adv)) {
                    spdlog::warn("It doesn't seem to be the device we desired.");
                    return false;
                }
                return true;
            }

            std::optional<State> GetState() const {
                return _tracker.GetState();
            }

            auto& CbControlInfoWindow()  { return _cbControlInfoWindow; }
            auto& CbBothInEar()          { return _cbBothInEar; }
            auto& CbStateChanged()       { return _tracker.CbStateChanged(); }
            auto& CbLosted()             { return _tracker.CbLosted(); }

        private:
            Tracker _tracker;

            using FnControlInfoWindow = std::function<void(const State &state, bool show)>;
            using FnBothInEar = std::function<void(const State &state, bool isBothInEar)>;

            Helper::Callback<FnControlInfoWindow> _cbControlInfoWindow;
            Helper::Callback<FnBothInEar> _cbBothInEar;


            Manager()
            {
                _tracker.CbStateChanged() += [this](
                    const std::optional<State> &oldState, const State &newState
                ) {
                    if (!oldState.has_value()) {
                        return;
                    }

                    if (oldState->caseBox.isLidOpened != newState.caseBox.isLidOpened) {
                        _cbControlInfoWindow.Invoke(
                            newState,
                            newState.caseBox.isLidOpened && newState.caseBox.isBothPodsInCase
                        );
                    }

                    bool cachedBothInEar =
                        oldState->pods.left.isInEar && oldState->pods.right.isInEar;
                    bool newBothInEar =
                        newState.pods.left.isInEar && newState.pods.right.isInEar;
                    if (cachedBothInEar != newBothInEar) {
                        _cbBothInEar.Invoke(newState, newBothInEar);
                    }
                };
            }
        };

    } // namespace Details

    AsyncScanner::AsyncScanner()
    {
        auto &manager = Details::Manager::GetInstance();

        manager.CbStateChanged() += [](auto, const State &newState) {
            App->GetInfoWindow()->UpdateStateSafety(newState);
            App->GetSysTray()->UpdateStateSafety(newState);
        };

        manager.CbControlInfoWindow() += [](const State &state, bool show)
        {
            auto &infoWindow = App->GetInfoWindow();
            if (show) {
                infoWindow->ShowSafety();
            }
            else {
                infoWindow->HideSafety();
            }
        };

        manager.CbBothInEar() += [](const State &state, bool isBothInEar)
        {
            if (!Settings::GetCurrent().automatic_ear_detection) {
                spdlog::trace(
                    "BothInEarCallbacks: Do nothing because the feature is disabled. ({})",
                    isBothInEar
                );
                return;
            }

            if (isBothInEar) {
                Core::GlobalMedia::Play();
            }
            else {
                Core::GlobalMedia::Pause();
            }
        };

        manager.CbLosted() += [this]() {
            UpdateUi(Action::Disconnected);
        };

        _adWatcher.ReceivedCallbacks() +=
            [](const Bluetooth::AdvertisementWatcher::ReceivedData &receivedData)
        {
            Details::Manager::GetInstance().OnAdvertisementReceived(receivedData);
        };

        _adWatcher.StoppedCallbacks() += [this]() {
            UpdateUi(Action::BluetoothUnavailable);
            spdlog::warn("Bluetooth AdvWatcher stopped. Try to restart.");
            _needToStart = true;
        };

        _adWatcher.ErrorCallbacks() += [this](const std::string &info) {
            UpdateUi(Action::BluetoothUnavailable);
            spdlog::warn("Bluetooth AdvWatcher occurred a error. Try to restart. Info: {}", info);
            _needToStart = true;
        };
    }

    AsyncScanner::~AsyncScanner()
    {
        Stop();
    }

    void AsyncScanner::Start()
    {
        if (!_isStopped) {
            spdlog::warn("AsyncScanner::Start() return directly. Because it's already started.");
            return;
        }

        _isStopped = false;
        _destroyStartThread = false;
        _needToStart = true;
        _startThread = std::thread{&AsyncScanner::StartThread, this};
    }

    Status AsyncScanner::Stop()
    {
        if (_isStopped) {
            spdlog::warn("AsyncScanner::Stop() return directly. Because it's already stopped.");
            return Status::Success;
        }

        _needToStart = false;
        _destroyStartThread = true;
        if (_startThread.joinable()) {
            _startThread.join();
        }

        Status status = _adWatcher.Stop();
        if (status.IsFailed()) {
            spdlog::warn("AsyncScanner::Stop() failed. Status: {}", status);
        }
        else {
            _isStopped = true;
            UpdateUi(Action::BluetoothUnavailable);
            spdlog::info("AsyncScanner::Stop() succeeded.");
        }

        return status;
    }

    void AsyncScanner::StartThread()
    {
        while (!_destroyStartThread)
        {
            if (!_needToStart) {
                std::this_thread::sleep_for(500ms);
            }
            else {
                Status status = _adWatcher.Start();
                if (status.IsSucceeded()) {
                    UpdateUi(Action::Disconnected);
                    _needToStart = false;
                    spdlog::info("Bluetooth AdvWatcher start succeeded.");
                }
                else {
                    UpdateUi(Action::BluetoothUnavailable);
                    spdlog::warn("Bluetooth AdvWatcher start failed. status: {}", status);
                }
                std::this_thread::sleep_for(5s);
            }
        }
    }

    void AsyncScanner::UpdateUi(Action action)
    {
        if (_lastAction == Action::BluetoothUnavailable && action == Action::Disconnected) {
            return;
        }
        _lastAction = action;

        QString title;
        if (action == Action::Disconnected) {
            title = QDialog::tr("Disconnected");
        }
        else if (action == Action::BluetoothUnavailable) {
            title = QDialog::tr("Unavailable");
        }

        auto &infoWindow = App->GetInfoWindow();
        infoWindow->HideSafety();
        infoWindow->DisconnectSafety(title);
        App->GetSysTray()->DisconnectSafety(title);
    }


    std::optional<State> GetState()
    {
        return Details::Manager::GetInstance().GetState();
    }

} // namespace Core::AirPods
