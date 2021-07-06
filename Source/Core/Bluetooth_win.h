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

#include <Config.h>

#if !defined APD_OS_WIN
#   error "This file shouldn't be compiled."
#endif

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>

#include "Bluetooth_abstract.h"


namespace Core::Bluetooth
{
    namespace WinrtBlutoothAdv = winrt::Windows::Devices::Bluetooth::Advertisement;

    bool Initialize();

    class AdvertisementWatcher final :
        public Details::AdvertisementWatcherAbstract<AdvertisementWatcher>
    {
    public:
        using Timestamp = winrt::Windows::Foundation::DateTime;

        explicit AdvertisementWatcher();

        Status Start() override;
        Status Stop() override;

    private:
        WinrtBlutoothAdv::BluetoothLEAdvertisementWatcher _bleWatcher;

        void OnReceived(
            const WinrtBlutoothAdv::BluetoothLEAdvertisementReceivedEventArgs &args
        );
        void OnStopped(
            const WinrtBlutoothAdv::BluetoothLEAdvertisementWatcherStoppedEventArgs &args
        );
    };

} // namespace Core::Bluetooth
