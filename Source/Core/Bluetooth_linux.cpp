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

#include "Bluetooth_linux.h"

#include "../Logger.h"
#include "../Error.h"

namespace Core::Bluetooth {

using namespace std::placeholders;

//////////////////////////////////////////////////
// Device
//

// Device::Device(BluetoothDevice device) : _device{std::move(device)}
// {
//     Unimplemented();
// }

Device::Device(const Device &rhs)
{
    Unimplemented();
}

Device::Device(Device &&rhs) noexcept
{
    Unimplemented();
}

Device::~Device()
{
    Unimplemented();
}

Device &Device::operator=(const Device &rhs)
{
    Unimplemented();
}

Device &Device::operator=(Device &&rhs) noexcept
{
    Unimplemented();
}

uint64_t Device::GetAddress() const
{
    Unimplemented();
}

std::string Device::GetName() const
{
    Unimplemented();
}

uint16_t Device::GetVendorId() const
{
    Unimplemented();
}

uint16_t Device::GetProductId() const
{
    Unimplemented();
}

DeviceState Device::GetConnectionState() const
{
    Unimplemented();
}

//////////////////////////////////////////////////
// DevicesManager
//

namespace Details {
class DeviceManager final : public Helper::Singleton<DeviceManager>,
                            Details::DeviceManagerAbstract<Device>
{
protected:
    DeviceManager() = default;
    friend Helper::Singleton<DeviceManager>;

public:
    std::vector<Device> GetDevicesByState(DeviceState state) const override
    {
        Unimplemented();
    }

    std::optional<Device> FindDevice(uint64_t address) const override
    {
        Unimplemented();
    }
};
} // namespace Details

namespace DeviceManager {

std::vector<Device> GetDevicesByState(DeviceState state)
{
    Unimplemented();
}

std::optional<Device> FindDevice(uint64_t address)
{
    Unimplemented();
}
} // namespace DeviceManager

//////////////////////////////////////////////////
// AdvertisementWatcher
//

AdvertisementWatcher::AdvertisementWatcher()
{
    Unimplemented();
}

AdvertisementWatcher::~AdvertisementWatcher()
{
    Unimplemented();
}

bool AdvertisementWatcher::Start()
{
    Unimplemented();
}

bool AdvertisementWatcher::Stop()
{
    Unimplemented();
}
} // namespace Core::Bluetooth
