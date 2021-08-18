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

#include "Bluetooth_win.h"

#include "../Logger.h"
#include "OS/Windows.h"

namespace Core::Bluetooth {

using namespace std::placeholders;

using namespace WinrtFoundation;
using namespace WinrtBlutooth;
using namespace WinrtBlutoothAdv;
using namespace WinrtDevicesEnumeration;

bool Initialize()
{
    return OS::Windows::Winrt::Initialize();
}

//////////////////////////////////////////////////
// Device
//

Device::Device(BluetoothDevice device) : _device{std::move(device)}
{
    RegisterHandlers();
}

Device::Device(const Device &rhs)
{
    CopyFrom(rhs);
}

Device::Device(Device &&rhs) noexcept
{
    MoveFrom(std::move(rhs));
}

Device::~Device()
{
    UnregisterHandlers();
}

Device &Device::operator=(const Device &rhs)
{
    CopyFrom(rhs);
    return *this;
}

Device &Device::operator=(Device &&rhs) noexcept
{
    MoveFrom(std::move(rhs));
    return *this;
}

uint64_t Device::GetAddress() const
{
    return _device->BluetoothAddress();
}

std::string Device::GetDisplayName() const
{
    return winrt::to_string(_device->Name());
}

uint16_t Device::GetVendorId() const
{
    return GetProperty<uint16_t>(kPropertyBluetoothVendorId, 0);
}

uint16_t Device::GetProductId() const
{
    return GetProperty<uint16_t>(kPropertyBluetoothProductId, 0);
}

DeviceState Device::GetConnectionState() const
{
    return _device->ConnectionStatus() == BluetoothConnectionStatus::Connected
               ? DeviceState::Connected
               : DeviceState::Disconnected;
}

winrt::hstring Device::GetAepId() const
{
    return GetProperty<winrt::hstring>(kPropertyAepContainerId, {});
}

void Device::RegisterHandlers()
{
    UnregisterHandlers();

    _tokenConnectionStatusChanged = _device->ConnectionStatusChanged(
        [this](const BluetoothDevice &sender, IInspectable) { OnConnectionStatusChanged(sender); });

    _tokenNameChanged = _device->NameChanged(
        [this](const BluetoothDevice &sender, IInspectable) { OnNameChanged(sender); });
}

void Device::UnregisterHandlers()
{
    if (_tokenConnectionStatusChanged) {
        _device->ConnectionStatusChanged(_tokenConnectionStatusChanged);
        _tokenConnectionStatusChanged = {};
    }

    if (_tokenNameChanged) {
        _device->NameChanged(_tokenNameChanged);
        _tokenNameChanged = {};
    }
}

void Device::CopyFrom(const Device &rhs)
{
    _device = rhs._device;
    _info = rhs._info;
    RegisterHandlers();
}

void Device::MoveFrom(Device &&rhs) noexcept
{
    rhs.UnregisterHandlers();
    _device = std::move(rhs._device);
    _info = std::move(rhs._info);
    RegisterHandlers();
}

const std::optional<DeviceInformation> &Device::GetInfo() const
{
    if (_info.has_value()) {
        return _info;
    }

    std::thread{[this]() {
        try {
            // clang-format off
            _info = DeviceInformation::CreateFromIdAsync(
                _device->DeviceInformation().Id(),
                {
                    kPropertyBluetoothProductId, // uint16
                    kPropertyBluetoothVendorId,  // uint16
                    kPropertyAepContainerId,     // hstring
                }
            ).get();
            // clang-format on
        }
        catch (const OS::Windows::Winrt::Exception &ex) {
            SPDLOG_WARN("DeviceInformation::CreateFromIdAsync() failed. {}", Helper::ToString(ex));
        }
    }}.join();

    return _info;
}

void Device::OnConnectionStatusChanged(const BluetoothDevice &sender)
{
    CbConnectionStatusChanged().Invoke(GetConnectionState());
}

void Device::OnNameChanged(const BluetoothDevice &sender)
{
    CbNameChanged().Invoke(GetDisplayName());
}

//////////////////////////////////////////////////
// DevicesManager
//

namespace Details {
class DeviceManager final : Details::DeviceManagerAbstract<Device>
{
public:
    static DeviceManager &GetInstance()
    {
        static DeviceManager i;
        return i;
    }

    std::vector<Device> GetDevicesByState(DeviceState state) const override
    {
        std::vector<Device> result;

        try {
            winrt::hstring aqsString;

            switch (state) {
            case Core::Bluetooth::DeviceState::Paired:
                aqsString = BluetoothDevice::GetDeviceSelectorFromPairingState(true);
                break;
            case Core::Bluetooth::DeviceState::Disconnected:
                aqsString = BluetoothDevice::GetDeviceSelectorFromConnectionStatus(
                    BluetoothConnectionStatus::Disconnected);
                break;
            case Core::Bluetooth::DeviceState::Connected:
                aqsString = BluetoothDevice::GetDeviceSelectorFromConnectionStatus(
                    BluetoothConnectionStatus::Connected);
                break;
            default:
                APD_ASSERT(false);
                break;
            }

            auto collection =
                WinrtDevicesEnumeration::DeviceInformation::FindAllAsync(aqsString).get();

            result.reserve(collection.Size());

            for (uint32_t i = 0; i < collection.Size(); ++i) {
                const auto &deviceInfo = collection.GetAt(i);

                try {
                    auto device = BluetoothDevice::FromIdAsync(deviceInfo.Id()).get();
                    result.emplace_back(std::move(device));
                }
                catch (const OS::Windows::Winrt::Exception &ex) {
                    SPDLOG_WARN("BluetoothDevice::FromIdAsync() failed. {}", Helper::ToString(ex));
                }
            }

            return result;
        }
        catch (const OS::Windows::Winrt::Exception &ex) {
            return result;
        }
    }

    std::optional<Device> FindDevice(uint64_t address) const override
    {
        auto devices = GetDevicesByState(Bluetooth::DeviceState::Paired);
        for (const auto &device : devices) {
            if (device.GetAddress() == address) {
                return device;
            }
        }
        return std::nullopt;
    }
};
} // namespace Details

namespace DeviceManager {

std::vector<Device> GetDevicesByState(DeviceState state)
{
    std::vector<Device> result;
    std::thread{[&]() {
        result = Details::DeviceManager::GetInstance().GetDevicesByState(state);
    }}.join();
    return result;
}

std::optional<Device> FindDevice(uint64_t address)
{
    std::optional<Device> result;
    std::thread{[&]() {
        result = Details::DeviceManager::GetInstance().FindDevice(address);
    }}.join();
    return result;
}
} // namespace DeviceManager

//////////////////////////////////////////////////
// AdvertisementWatcher
//

AdvertisementWatcher::AdvertisementWatcher()
{
    _bleWatcher.Received(std::bind(&AdvertisementWatcher::OnReceived, this, _2));
    _bleWatcher.Stopped(std::bind(&AdvertisementWatcher::OnStopped, this, _2));
}

AdvertisementWatcher::~AdvertisementWatcher()
{
    Stop();
}

bool AdvertisementWatcher::Start()
{
    try {
        std::lock_guard<std::mutex> lock{_mutex};
        _bleWatcher.Start();
        return true;
    }
    catch (const OS::Windows::Winrt::Exception &ex) {
        SPDLOG_WARN("Start adv watcher exception: {}", Helper::ToString(ex));
        return false;
    }
}

bool AdvertisementWatcher::Stop()
{
    try {
        std::lock_guard<std::mutex> lock{_mutex};
        _bleWatcher.Stop();
        return true;
    }
    catch (const OS::Windows::Winrt::Exception &ex) {
        SPDLOG_WARN("Stop adv watcher exception: {}", Helper::ToString(ex));
        return false;
    }
}

void AdvertisementWatcher::OnReceived(const BluetoothLEAdvertisementReceivedEventArgs &args)
{
    ReceivedData receivedData;

    receivedData.rssi = args.RawSignalStrengthInDBm();
    receivedData.timestamp = args.Timestamp();
    receivedData.address = args.BluetoothAddress();

    const auto &manufacturerDataArray = args.Advertisement().ManufacturerData();
    for (uint32_t i = 0; i < manufacturerDataArray.Size(); ++i) {
        const auto &manufacturerData = manufacturerDataArray.GetAt(i);
        const auto companyId = manufacturerData.CompanyId();
        const auto &data = manufacturerData.Data();

        std::vector<uint8_t> stdData(data.data(), data.data() + data.Length());

        receivedData.manufacturerDataMap.try_emplace(companyId, std::move(stdData));
    }

    std::lock_guard<std::mutex> lock{_mutex};
    CbReceived().Invoke(receivedData);
}

void AdvertisementWatcher::OnStopped(const BluetoothLEAdvertisementWatcherStoppedEventArgs &args)
{
    static std::unordered_map<BluetoothError, std::string> errorReasons = {
        {BluetoothError::Success, "Success"},
        {BluetoothError::RadioNotAvailable, "RadioNotAvailable"},
        {BluetoothError::ResourceInUse, "ResourceInUse"},
        {BluetoothError::DeviceNotConnected, "DeviceNotConnected"},
        {BluetoothError::OtherError, "OtherError"},
        {BluetoothError::DisabledByPolicy, "DisabledByPolicy"},
        {BluetoothError::NotSupported, "NotSupported"},
        {BluetoothError::DisabledByUser, "DisabledByUser"},
        {BluetoothError::ConsentRequired, "ConsentRequired"},
        {BluetoothError::TransportNotSupported, "TransportNotSupported"},
    };

    auto status = _bleWatcher.Status();
    auto errorCode = args.Error();
    std::optional<std::string> optError;

    if (errorCode == BluetoothError::Success &&
        status != BluetoothLEAdvertisementWatcherStatus::Aborted)
    {
        optError = std::nullopt;
    }
    else {
        std::string info;

        if (status == BluetoothLEAdvertisementWatcherStatus::Aborted) {
            info = "Aborted";
        }
        else {
            auto reasonIter = errorReasons.find(errorCode);
            if (reasonIter != errorReasons.end()) {
                info = (*reasonIter).second;
            }
            else {
                info = "Unknown error (" + std::to_string((uint32_t)errorCode) + ")";
            }
        }
        optError = std::move(info);
    }

    std::lock_guard<std::mutex> lock{_mutex};
    CbStopped().Invoke(optError);
}
} // namespace Core::Bluetooth
