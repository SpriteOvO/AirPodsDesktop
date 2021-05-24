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

#include <spdlog/spdlog.h>

#include "OS/Windows.h"


namespace Core::Bluetooth
{
    using namespace std::placeholders;
    using namespace WinrtBlutoothAdv;
    using namespace winrt::Windows::Devices::Bluetooth;

    bool Initialize()
    {
        return OS::Windows::Winrt::Initialize();
    }

    AdvertisementWatcher::AdvertisementWatcher()
    {
        _bleWatcher.Received(std::bind(&AdvertisementWatcher::OnReceived, this, _2));
        _bleWatcher.Stopped(std::bind(&AdvertisementWatcher::OnStopped, this, _2));
    }

    Status AdvertisementWatcher::Start()
    {
        WINRT_TRY {
            _bleWatcher.Start();
            return Status::Success;
        }
        WINRT_CATCH_RETURN_STATUS(Status::BluetoothAdvWatcherStartFailed)
    }

    Status AdvertisementWatcher::Stop()
    {
        WINRT_TRY {
            _bleWatcher.Stop();
            return Status::Success;
        }
        WINRT_CATCH_RETURN_STATUS(Status::BluetoothAdvWatcherStopFailed)
    }

    void AdvertisementWatcher::OnReceived(const BluetoothLEAdvertisementReceivedEventArgs &args)
    {
        ReceivedData receivedData;

        receivedData.rssi = args.RawSignalStrengthInDBm();
        receivedData.timestamp = args.Timestamp();
        receivedData.address = args.BluetoothAddress();

        const auto &manufacturerDataArray = args.Advertisement().ManufacturerData();
        for (uint32_t i = 0; i < manufacturerDataArray.Size(); ++i)
        {
            const auto &manufacturerData = manufacturerDataArray.GetAt(i);
            const auto companyId = manufacturerData.CompanyId();
            const auto &data = manufacturerData.Data();

            std::vector<uint8_t> stdData(data.data(), data.data() + data.Length());

            receivedData.manufacturerDataMap.try_emplace(companyId, std::move(stdData));
        }

        ReceivedCallbacks().Invoke(receivedData);
    }

    void AdvertisementWatcher::OnStopped(
        const BluetoothLEAdvertisementWatcherStoppedEventArgs &args
    ) {
        static std::unordered_map<BluetoothError, std::string> errorReasons = {
            { BluetoothError::Success,                  "Success" },
            { BluetoothError::RadioNotAvailable,        "RadioNotAvailable" },
            { BluetoothError::ResourceInUse,            "ResourceInUse" },
            { BluetoothError::DeviceNotConnected,       "DeviceNotConnected" },
            { BluetoothError::OtherError,               "OtherError" },
            { BluetoothError::DisabledByPolicy,         "DisabledByPolicy" },
            { BluetoothError::NotSupported,             "NotSupported" },
            { BluetoothError::DisabledByUser,           "DisabledByUser" },
            { BluetoothError::ConsentRequired,          "ConsentRequired" },
            { BluetoothError::TransportNotSupported,    "TransportNotSupported" },
        };

        auto status = _bleWatcher.Status();
        auto errorCode = args.Error();

        if (errorCode == BluetoothError::Success &&
            status != BluetoothLEAdvertisementWatcherStatus::Aborted)
        {
            StoppedCallbacks().Invoke();
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

            ErrorCallbacks().Invoke(info);
        }
    }

} // namespace Core::Bluetooth
