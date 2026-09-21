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

#include <cmath>
#include <type_traits>

#include "../Logger.h"

using namespace std::chrono_literals;

namespace Core::AirPods::Details {

// Windows reports this RSSI when the radio has no measurement for the packet.
constexpr int16_t kRssiUnavailable = -127;

StateManager::StateManager(Intervals intervals)
{
    _lostTimer.Start(intervals.lost, [this] {
        std::function<void()> onDiscardState;
        {
            std::lock_guard<std::mutex> lock{_mutex};
            onDiscardState = DoLost();
        }
        if (onDiscardState) {
            onDiscardState();
        }
    });

    _stateResetTimer.left.Start(intervals.stateReset, [this] {
        std::lock_guard<std::mutex> lock{_mutex};
        DoStateReset(Side::Left);
    });

    _stateResetTimer.right.Start(intervals.stateReset, [this] {
        std::lock_guard<std::mutex> lock{_mutex};
        DoStateReset(Side::Right);
    });
}

StateManager::~StateManager()
{
    _lostTimer.Stop();
    _stateResetTimer.left.Stop();
    _stateResetTimer.right.Stop();
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
    std::function<void()> onDiscardState;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        LOG(Info, "StateManager: Disconnect.");
        onDiscardState = ResetAll();
        _knownAddress.left.reset();
        _knownAddress.right.reset();
    }
    if (onDiscardState) {
        onDiscardState();
    }
}

void StateManager::OnRssiMinChanged(int16_t rssiMin)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _rssiMin = rssiMin;
}

void StateManager::SetOnDiscardState(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _onDiscardState = std::move(callback);
}

bool StateManager::IsPossibleDesiredAdv(const Advertisement &adv) const
{
    const auto &advState = adv.GetAdvState();
    auto &lastAdv = advState.side == Side::Left ? _adv.left : _adv.right;
    auto &lastAnotherAdv = advState.side == Side::Left ? _adv.right : _adv.left;

    // The RSSI floor keeps us from locking onto a stranger's AirPods. Once we are tracking an
    // address, weak advertisements from that same address are still ours; rejecting them would
    // drop in-ear changes and starve the lost timer while the pods are merely far from the PC.
    const auto advRssi = adv.GetRssi();
    if (advRssi < _rssiMin) {
        if (!IsKnownAddress(adv.GetAddress())) {
            if (advRssi == kRssiUnavailable) {
                LOG(Warn, "IsPossibleDesiredAdv returns false. Reason: RSSI unavailable (-127)");
            }
            else {
                LOG(Warn,
                    "IsPossibleDesiredAdv returns false. Reason: RSSI is less than the limit. "
                    "curr: '{}' min: '{}'",
                    advRssi, _rssiMin);
            }
            return false;
        }
        LOG(Trace,
            "RSSI below limit but address matches tracked device, accepting. "
            "curr: '{}' min: '{}'",
            advRssi, _rssiMin);
    }

    // An unavailable RSSI on either side says nothing about the distance between two packets.
    const auto isPlausibleRssiDiff = [&](int16_t cachedRssi) {
        if (advRssi == kRssiUnavailable || cachedRssi == kRssiUnavailable) {
            return true;
        }
        return std::abs(advRssi - cachedRssi) <= 50;
    };

    const auto hasDifferentModel = [&](const auto &cachedAdv) {
        if (!cachedAdv.has_value()) {
            return false;
        }
        const auto cachedModel = cachedAdv->first.GetAdvState().model;
        return cachedModel != Model::Unknown && advState.model != Model::Unknown &&
               cachedModel != advState.model;
    };
    if (hasDifferentModel(lastAdv) || hasDifferentModel(lastAnotherAdv)) {
        LOG(Warn, "IsPossibleDesiredAdv returns false. Reason: model does not match cached state");
        return false;
    }

    if (lastAdv.has_value() && lastAdv->first.GetAddress() != adv.GetAddress()) {
        const auto &lastAdvState = lastAdv->first.GetAdvState();

        Battery::ValueType leftBatteryDiff = 0, rightBatteryDiff = 0, caseBatteryDiff = 0;
        using SignedBatteryValue = std::make_signed_t<Battery::ValueType>;

        if (advState.pods.left.battery.Available() && lastAdvState.pods.left.battery.Available()) {
            leftBatteryDiff = std::abs(
                static_cast<SignedBatteryValue>(advState.pods.left.battery.Value()) -
                static_cast<SignedBatteryValue>(lastAdvState.pods.left.battery.Value()));
        }
        if (advState.pods.right.battery.Available() && lastAdvState.pods.right.battery.Available())
        {
            rightBatteryDiff = std::abs(
                static_cast<SignedBatteryValue>(advState.pods.right.battery.Value()) -
                static_cast<SignedBatteryValue>(lastAdvState.pods.right.battery.Value()));
        }
        if (advState.caseBox.battery.Available() && lastAdvState.caseBox.battery.Available()) {
            caseBatteryDiff = std::abs(
                static_cast<SignedBatteryValue>(advState.caseBox.battery.Value()) -
                static_cast<SignedBatteryValue>(lastAdvState.caseBox.battery.Value()));
        }

        if (leftBatteryDiff > 1 || rightBatteryDiff > 1 || caseBatteryDiff > 1) {
            LOG(Warn,
                "IsPossibleDesiredAdv returns false. Reason: BatteryDiff l='{}' r='{}' c='{}'",
                leftBatteryDiff, rightBatteryDiff, caseBatteryDiff);
            return false;
        }

        if (!isPlausibleRssiDiff(lastAdv->first.GetRssi())) {
            LOG(Warn, "IsPossibleDesiredAdv returns false. Reason: Current side rssiDiff '{}'",
                std::abs(advRssi - lastAdv->first.GetRssi()));
            return false;
        }

        LOG(Warn, "Address changed, but it might still be the same device.");
    }

    if (lastAnotherAdv.has_value() && !isPlausibleRssiDiff(lastAnotherAdv->first.GetRssi())) {
        LOG(Warn, "IsPossibleDesiredAdv returns false. Reason: Another side rssiDiff '{}'",
            std::abs(advRssi - lastAnotherAdv->first.GetRssi()));
        return false;
    }

    return true;
}

bool StateManager::IsKnownAddress(Advertisement::AddressType address) const
{
    return _knownAddress.left == address || _knownAddress.right == address;
}

void StateManager::UpdateAdv(Advertisement adv)
{
    _lostTimer.Reset();

    const auto &advState = adv.GetAdvState();
    if (advState.side == Side::Left) {
        _stateResetTimer.left.Reset();
        _knownAddress.left = adv.GetAddress();
        _adv.left = std::make_pair(std::move(adv), Clock::now());
    }
    else if (advState.side == Side::Right) {
        _stateResetTimer.right.Reset();
        _knownAddress.right = adv.GetAddress();
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

#define PICK_SIDE(available_condition_with_field)                                                  \
    [&]() -> decltype(auto) {                                                                      \
        const Helper::Sides<bool> available = {                                                    \
            .left = cachedAdvState.left.first.available_condition_with_field,                      \
            .right = cachedAdvState.right.first.available_condition_with_field,                    \
        };                                                                                         \
        if (available.left && available.right) {                                                   \
            return cachedAdvState.left.second > cachedAdvState.right.second                        \
                       ? cachedAdvState.left.first                                                 \
                       : cachedAdvState.right.first;                                               \
        }                                                                                          \
        return available.left ? cachedAdvState.left.first : cachedAdvState.right.first;            \
    }()

    newState.model = PICK_SIDE(model != Model::Unknown).model;
    newState.pods.left = std::move(PICK_SIDE(pods.left.battery.Available()).pods.left);
    newState.pods.right = std::move(PICK_SIDE(pods.right.battery.Available()).pods.right);
    newState.caseBox = std::move(PICK_SIDE(caseBox.battery.Available()).caseBox);

#undef PICK_SIDE

    if (newState == _cachedState) {
        return std::nullopt;
    }

    auto oldState = std::move(_cachedState);
    _cachedState = std::move(newState);
    return UpdateEvent{.oldState = std::move(oldState), .newState = _cachedState.value()};
}

std::function<void()> StateManager::ResetAll()
{
    auto onDiscardState = _cachedState.has_value() ? _onDiscardState : nullptr;

    _adv.left.reset();
    _adv.right.reset();
    _cachedState.reset();
    return onDiscardState;
}

std::function<void()> StateManager::DoLost()
{
    if (_cachedState.has_value()) {
        LOG(Info, "StateManager: Device is lost.");
    }
    return ResetAll();
}

void StateManager::DoStateReset(Side side)
{
    auto &adv = side == Side::Left ? _adv.left : _adv.right;
    if (adv.has_value()) {
        LOG(Info, "StateManager: DoStateReset called. Side: {}", Helper::ToString(side));
        adv.reset();
    }
}

} // namespace Core::AirPods::Details
