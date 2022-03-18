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

#pragma once

#include <map>
#include <optional>
#include <functional>

#include "../Helper.h"

namespace Core::Bluetooth {

enum class DeviceState : uint32_t {
    Paired,
    Disconnected,
    Connected,
};

namespace Details {

template <class ConcreteAddressT>
class DeviceAbstract
{
public:
    using FnConnectionStatusChanged = std::function<void(DeviceState)>;
    using FnNameChanged = std::function<void(const std::string &)>;

    virtual inline ~DeviceAbstract() {}

    virtual ConcreteAddressT GetAddress() const = 0;
    virtual std::string GetName() const = 0;
    virtual uint16_t GetProductId() const = 0;
    virtual uint16_t GetVendorId() const = 0;
    virtual DeviceState GetConnectionState() const = 0;

    inline auto &CbConnectionStatusChanged()
    {
        return _cbConnectionStatusChanged;
    }
    inline auto &CbNameChanged()
    {
        return _cbNameChanged;
    }

private:
    Helper::Callback<FnConnectionStatusChanged> _cbConnectionStatusChanged;
    Helper::Callback<FnNameChanged> _cbNameChanged;
};

template <class ConcreteDeviceT>
class DeviceManagerAbstract
{
public:
    virtual inline ~DeviceManagerAbstract() {}

    virtual std::vector<ConcreteDeviceT> GetDevicesByState(DeviceState state) const = 0;
    virtual std::optional<ConcreteDeviceT> FindDevice(uint64_t address) const = 0;
};

template <class Derived>
class AdvertisementWatcherAbstract
{
public:
    enum class State { Started, Stopped };

    struct ReceivedData {
        int16_t rssi{};
        typename Derived::Timestamp timestamp;
        uint64_t address{};
        std::map<uint16_t, std::vector<uint8_t>> manufacturerDataMap;
    };
    using FnReceived = std::function<void(const ReceivedData &)>;
    using FnStateChanged = std::function<void(State, const std::optional<std::string> &)>;

    virtual inline ~AdvertisementWatcherAbstract() {}

    inline auto &CbReceived()
    {
        return _cbReceived;
    }
    inline auto &CbStateChanged()
    {
        return _cbStateChanged;
    }

    virtual bool Start() = 0;
    virtual bool Stop() = 0;

private:
    Helper::Callback<FnReceived> _cbReceived;
    Helper::Callback<FnStateChanged> _cbStateChanged;
};
} // namespace Details
} // namespace Core::Bluetooth
