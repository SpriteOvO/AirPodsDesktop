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

#if !defined APD_OS_LINUX
    #error "This file shouldn't be compiled."
#endif

#include "Bluetooth_abstract.h"

namespace Core::Bluetooth {

using namespace std::chrono_literals;

class Device final : public Details::DeviceAbstract<uint64_t>
{
public:
    // Device(WinrtBluetooth::BluetoothDevice device);
    Device(const Device &rhs);
    Device(Device &&rhs) noexcept;
    ~Device();

    Device &operator=(const Device &rhs);
    Device &operator=(Device &&rhs) noexcept;

    uint64_t GetAddress() const override;
    std::string GetName() const override;
    uint16_t GetVendorId() const override;
    uint16_t GetProductId() const override;
    DeviceState GetConnectionState() const override;
};

namespace DeviceManager {

std::vector<Device> GetDevicesByState(DeviceState state);
std::optional<Device> FindDevice(uint64_t address);

} // namespace DeviceManager

class AdvertisementWatcher final
    : public Details::AdvertisementWatcherAbstract<AdvertisementWatcher>
{
public:
    using Timestamp = uint8_t; // Unimplemented();

    explicit AdvertisementWatcher();
    ~AdvertisementWatcher();

    bool Start() override;
    bool Stop() override;
};
} // namespace Core::Bluetooth
