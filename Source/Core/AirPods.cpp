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

            struct DevState : AirPods::State {
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

            DevState GetState() const
            {
                DevState state;

                state.model = _protocol.GetModel();
                state.side = _protocol.GetBroadcastedSide();

                state.pods.left.battery = _protocol.GetLeftBattery();
                state.pods.left.isCharging = _protocol.IsLeftCharging();
                state.pods.left.isInEar = _protocol.IsLeftInEar();

                state.pods.right.battery = _protocol.GetRightBattery();
                state.pods.right.isCharging = _protocol.IsRightCharging();
                state.pods.right.isInEar = _protocol.IsRightInEar();

                state.caseBox.battery = _protocol.GetCaseBattery();
                state.caseBox.isCharging = _protocol.IsCaseCharging();

                state.caseBox.isBothPodsInCase = _protocol.IsBothPodsInCase();
                state.caseBox.isLidOpened = _protocol.IsLidOpened();

                if (state.pods.left.battery.has_value()) {
                    state.pods.left.battery = state.pods.left.battery.value() * 10;
                }
                if (state.pods.right.battery.has_value()) {
                    state.pods.right.battery = state.pods.right.battery.value() * 10;
                }
                if (state.caseBox.battery.has_value()) {
                    state.caseBox.battery = state.caseBox.battery.value() * 10;
                }

                return state;
            }

        private:
            Bluetooth::AdvertisementWatcher::ReceivedData _data;
            AppleCP::AirPods _protocol;

            const std::vector<uint8_t>& GetMfrData() const
            {
                auto iter = _data.manufacturerDataMap.find(76);
                APD_ASSERT(iter != _data.manufacturerDataMap.end());

                return (*iter).second;
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
                auto state = adv.GetState();

                spdlog::trace(
                    "AirPods advertisement received. Data: {}",
                    Helper::ToString(adv.GetDesensitizedData())
                );

                std::lock_guard<std::mutex> lock{_mutex};

                if (_leftAdv.has_value() && Clock::now() - _leftAdv->first >= Timeout) {
                    _leftAdv.reset();
                }
                if (_rightAdv.has_value() && Clock::now() - _rightAdv->first >= Timeout) {
                    _rightAdv.reset();
                }

                auto &lastAdv = state.side == Side::Left ? _leftAdv : _rightAdv;

                // If the Random Non-resolvable Address of our devices is changed
                // or the packet is sent from another device that it isn't ours
                //
                if (_unreliableAddress != adv.GetAddress())
                {
                    if (lastAdv.has_value())
                    {
                        auto lastState = lastAdv->second.GetState();

                        Battery::value_type leftBatteryDiff = 0, rightBatteryDiff = 0,
                            caseBatteryDiff = 0;

                        if (state.pods.left.battery.has_value() &&
                            lastState.pods.left.battery.has_value())
                        {
                            leftBatteryDiff = std::abs(
                                (int32_t)state.pods.left.battery.value() -
                                (int32_t)lastState.pods.left.battery.value()
                            );
                        }
                        if (state.pods.right.battery.has_value() &&
                            lastState.pods.right.battery.has_value())
                        {
                            rightBatteryDiff = std::abs(
                                (int32_t)state.pods.right.battery.value() -
                                (int32_t)lastState.pods.right.battery.value()
                            );
                        }
                        if (state.caseBox.battery.has_value() &&
                            lastState.caseBox.battery.has_value())
                        {
                            caseBatteryDiff = std::abs(
                                (int32_t)state.caseBox.battery.value() -
                                (int32_t)lastState.caseBox.battery.value()
                            );
                        }

                        // The battery changes in steps of 1,
                        // so the data of two packets in a short time can not exceed 1,
                        // otherwise it is not our device
                        //
                        if (leftBatteryDiff > 1 || rightBatteryDiff > 1 || caseBatteryDiff > 1)
                        {
                            auto now = Clock::now();

                            if (_lostDuration == Timestamp{}) {
                                _lostDuration = now;
                            }
                            if (now - _lostDuration < Timeout) {
                                return false;
                            }
                        }
                    }
                    _lostDuration = Timestamp{};
                    _unreliableAddress = adv.GetAddress();
                }

                lastAdv = std::make_pair(Clock::now(), adv);


                //////////////////////////////////////////////////
                // Update states
                //

                Advertisement::DevState leftState, rightState;

                if (_leftAdv.has_value()) {
                    leftState = _leftAdv->second.GetState();
                }
                if (_rightAdv.has_value()) {
                    rightState = _rightAdv->second.GetState();
                }

                State newState;

                const auto &ll = leftState.pods.left;
                const auto &rl = rightState.pods.left;
                const auto &lr = leftState.pods.right;
                const auto &rr = rightState.pods.right;
                const auto &lc = leftState.caseBox;
                const auto &rc = rightState.caseBox;

                newState.model =
                    rightState.model != Model::Unknown ? rightState.model : leftState.model;

                newState.pods.left = ll.battery.has_value() ? std::move(ll) : std::move(rl);
                newState.pods.right = rr.battery.has_value() ? std::move(rr) : std::move(lr);
                newState.caseBox = rc.battery.has_value() ? std::move(rc) : std::move(lc);

                UpdateState(std::move(newState));

                _lastUpdateTimestamp = Clock::now();
                return true;
            }

            PodState GetPodState(Side side) const
            {
                std::lock_guard<std::mutex> lock{_mutex};

                return side == Side::Left ? _state.pods.left : _state.pods.right;
            }

            PodsState GetPodsState() const
            {
                std::lock_guard<std::mutex> lock{_mutex};

                PodsState result;
                result.left = _state.pods.left;
                result.right = _state.pods.right;
                return result;
            }

            CaseState GetCaseState() const
            {
                std::lock_guard<std::mutex> lock{_mutex};

                return _state.caseBox;
            }

            State GetState() const
            {
                std::lock_guard<std::mutex> lock{_mutex};

                return _state;
            }

            auto& StateChangedCallbacks() {
                return _stateChangedCallbacks;
            }

            auto& ControlInfoWindowCallbacks() {
                return _controlInfoWindowCallbacks;
            }

            auto& BothInEarCallbacks() {
                return _bothInEarCallbacks;
            }

            auto& LostCallbacks() {
                return _lostCallbacks;
            }

        private:
            using Clock = std::chrono::system_clock;
            using Timestamp = std::chrono::time_point<Clock>;

            constexpr static inline auto Timeout = 10s;

            mutable std::mutex _mutex;
            std::optional<std::pair<Timestamp, Advertisement>> _leftAdv, _rightAdv;
            Advertisement::AddressType _unreliableAddress{};
            Timestamp _lostDuration;
            Timestamp _lastUpdateTimestamp;
            std::thread _lostWatcherThread;
            std::atomic<bool> _destroy{false};
            bool _lastBothInEar{true};

            State _state;

            using FnStateChanged = std::function<void(const State &state)>;
            using FnControlInfoWindow = std::function<void(const State &state, bool show)>;
            using FnBothInEar = std::function<void(const State &state, bool isBothInEar)>;
            using FnLost = std::function<void()>;

            Helper::Callback<FnStateChanged> _stateChangedCallbacks;
            Helper::Callback<FnControlInfoWindow> _controlInfoWindowCallbacks;
            Helper::Callback<FnBothInEar> _bothInEarCallbacks;
            Helper::Callback<FnLost> _lostCallbacks;


            explicit Manager()
            {
                _lostWatcherThread = std::thread{&Manager::LostWatcherThread, this};
            }

            ~Manager()
            {
                _destroy = true;
                if (_lostWatcherThread.joinable()) {
                    _lostWatcherThread.join();
                }
            }

            void LostWatcherThread()
            {
                while (!_destroy)
                {
                    std::this_thread::sleep_for(1s);

                    std::lock_guard<std::mutex> lock{_mutex};

                    if (_lastUpdateTimestamp != Timestamp{} &&
                        Clock::now() - _lastUpdateTimestamp > Timeout)
                    {
                        OnLost();
                        _lastUpdateTimestamp = Timestamp{};
                    }
                }
            }

            void OnLost()
            {
                _leftAdv.reset();
                _rightAdv.reset();

                // _lastBothInEar = true;

                _state = State{};

                _lostCallbacks.Invoke();
            }

            void UpdateState(State &&newState)
            {
                if (_state != newState) {
                    _stateChangedCallbacks.Invoke(newState);
                }

                if (_state.caseBox.isLidOpened != newState.caseBox.isLidOpened) {
                    _controlInfoWindowCallbacks.Invoke(
                        newState,
                        newState.caseBox.isLidOpened && newState.caseBox.isBothPodsInCase
                    );
                }

                if (_state.pods.left.isInEar != newState.pods.left.isInEar ||
                    _state.pods.right.isInEar != newState.pods.right.isInEar)
                {
                    bool thisBothInEar = newState.pods.left.isInEar && newState.pods.right.isInEar;

                    if (_lastBothInEar != thisBothInEar)
                    {
                        _lastBothInEar = thisBothInEar;

                        _bothInEarCallbacks.Invoke(newState, thisBothInEar);
                    }
                }

                _state = std::move(newState);
            }
        };

    } // namespace Details

    AsyncScanner::AsyncScanner()
    {
        auto &manager = Details::Manager::GetInstance();

        manager.StateChangedCallbacks() += [](const State &state) {
            App->GetInfoWindow().UpdateStateSafety(state);
            App->GetSysTray().UpdateStateSafety(state);
        };

        manager.ControlInfoWindowCallbacks() += [](const State &state, bool show)
        {
            auto &infoWindow = App->GetInfoWindow();
            if (show) {
                infoWindow.ShowSafety();
            }
            else {
                infoWindow.HideSafety();
            }
        };

        manager.BothInEarCallbacks() += [](const State &state, bool isBothInEar)
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

        manager.LostCallbacks() += [this]() {
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
        infoWindow.HideSafety();
        infoWindow.DisconnectSafety(title);
        App->GetSysTray().DisconnectSafety(title);
    }


    PodState GetPodState(Side side)
    {
        return Details::Manager::GetInstance().GetPodState(side);
    }

    PodsState GetPodsState()
    {
        return Details::Manager::GetInstance().GetPodsState();
    }

    CaseState GetCaseState()
    {
        return Details::Manager::GetInstance().GetCaseState();
    }

} // namespace Core::AirPods
