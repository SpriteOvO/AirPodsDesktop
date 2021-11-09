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

#pragma once

#include <functional>

#include "Bluetooth.h"
#include "AppleCP.h"

namespace Core::AirPods {

//
// Structures
//

namespace Details {

struct BasicState {
    Battery battery;
    bool isCharging{false};

    bool operator==(const BasicState &rhs) const = default;
};
} // namespace Details

struct PodState : Details::BasicState {
    bool isInEar{false};

    bool operator==(const PodState &rhs) const = default;
};

struct CaseState : Details::BasicState {
    bool isBothPodsInCase{false};
    bool isLidOpened{false};

    bool operator==(const CaseState &rhs) const = default;
};

struct PodsState {
    PodState left, right;

    bool operator==(const PodsState &rhs) const = default;
};

struct State {
    Model model{Model::Unknown};
    PodsState pods;
    CaseState caseBox;

    bool operator==(const State &rhs) const = default;
};

//
// Classes
//

namespace Details {

class Advertisement
{
public:
    using AddressType = decltype(Bluetooth::AdvertisementWatcher::ReceivedData::address);

    struct AdvState : AirPods::State {
        Side side;
    };

    static bool IsDesiredAdv(const Bluetooth::AdvertisementWatcher::ReceivedData &data);

    Advertisement(const Bluetooth::AdvertisementWatcher::ReceivedData &data);

    int16_t GetRssi() const;
    const auto &GetTimestamp() const;
    AddressType GetAddress() const;
    std::vector<uint8_t> GetDesensitizedData() const;
    const AdvState &GetAdvState() const;

private:
    Bluetooth::AdvertisementWatcher::ReceivedData _data;
    AppleCP::AirPods _protocol;
    AdvState _state;

    const std::vector<uint8_t> &GetMfrData() const;
};

// AirPods use Random Non-resolvable device addresses for privacy reasons. This means we
// can't "Remember" the user's AirPods by any device property. Here we track our desired
// devices in some non-elegant ways, but obviously it is sometimes unreliable.
//
class StateManager
{
public:
    struct UpdateEvent {
        std::optional<State> oldState;
        State newState;
    };

    StateManager();

    std::optional<State> GetCurrentState() const;

    std::optional<UpdateEvent> OnAdvReceived(Advertisement adv);
    void Disconnect();

    void OnRssiMinChanged(int16_t rssiMin);

private:
    using Clock = std::chrono::steady_clock;
    using Timestamp = std::chrono::time_point<Clock>;

    mutable std::mutex _mutex;

    Helper::Timer _lostTimer;
    Helper::Sides<Helper::Timer> _stateResetTimer;
    Helper::Sides<std::optional<std::pair<Advertisement, Timestamp>>> _adv;
    std::optional<State> _cachedState;
    int16_t _rssiMin{std::numeric_limits<int16_t>::max()};

    bool IsPossibleDesiredAdv(const Advertisement &adv) const;
    void UpdateAdv(Advertisement adv);
    std::optional<UpdateEvent> UpdateState();
    void ResetAll();

    void DoLost();
    void DoStateReset(Side side);
};

class Manager : public Helper::Singleton<Manager>
{
protected:
    Manager();
    friend Helper::Singleton<Manager>;

public:
    void StartScanner();
    void StopScanner();

    QString GetDisplayName();
    std::optional<State> GetCurrentState();

    void OnRssiMinChanged(int16_t rssiMin);
    void OnAutomaticEarDetectionChanged(bool enable);
    void OnBoundDeviceAddressChanged(uint64_t address);
    void OnQuit();

private:
    Bluetooth::AdvertisementWatcher _adWatcher;
    StateManager _stateMgr;
    std::optional<Bluetooth::Device> _boundDevice;
    QString _displayName;
    bool _deviceConnected{false};
    std::mutex _mutex;
    bool _automaticEarDetection{false};

    void OnBoundDeviceConnectionStateChanged(Bluetooth::DeviceState state);
    void OnStateChanged(StateManager::UpdateEvent updateEvent);
    void OnLidOpened(bool opened);
    void OnBothInEar(bool isBothInEar);
    bool OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data);
    void OnAdvWatcherStateChanged(
        Bluetooth::AdvertisementWatcher::State state, const std::optional<std::string> &optError);
};
} // namespace Details

SINGLETON_EXPOSE_FUNCTION(Details::Manager, StartScanner)
SINGLETON_EXPOSE_FUNCTION(Details::Manager, GetDisplayName)
SINGLETON_EXPOSE_FUNCTION(Details::Manager, GetCurrentState)
SINGLETON_EXPOSE_FUNCTION(Details::Manager, OnRssiMinChanged)
SINGLETON_EXPOSE_FUNCTION(Details::Manager, OnAutomaticEarDetectionChanged)
SINGLETON_EXPOSE_FUNCTION(Details::Manager, OnBoundDeviceAddressChanged)
SINGLETON_EXPOSE_FUNCTION(Details::Manager, OnQuit)

} // namespace Core::AirPods
