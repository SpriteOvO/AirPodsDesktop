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

#include <cstring>

#include "../Assert.h"

namespace Core::AirPods::Details {

bool Advertisement::IsDesiredAdv(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    auto iter = data.manufacturerDataMap.find(AppleCP::VendorId);
    if (iter == data.manufacturerDataMap.end()) {
        return false;
    }
    return AppleCP::AirPods::IsValid(iter->second);
}

Advertisement::Advertisement(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    APD_ASSERT(IsDesiredAdv(data));
    _data = data;

    auto protocol = AppleCP::As<AppleCP::AirPods>(GetMfrData());
    APD_ASSERT(protocol.has_value());
    _protocol = std::move(protocol.value());

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

    if (_state.pods.left.battery.Available()) {
        _state.pods.left.battery = _state.pods.left.battery.Value() * 10;
    }
    if (_state.pods.right.battery.Available()) {
        _state.pods.right.battery = _state.pods.right.battery.Value() * 10;
    }
    if (_state.caseBox.battery.Available()) {
        _state.caseBox.battery = _state.caseBox.battery.Value() * 10;
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
    return iter->second;
}

} // namespace Core::AirPods::Details
