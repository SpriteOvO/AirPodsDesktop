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

#include "../Helper.h"
#include "../Status.h"


namespace Core::Bluetooth::Details
{
    template <class Derived>
    class AdvertisementWatcherAbstract
    {
    public:
        struct ReceivedData {
            int16_t rssi{};
            typename Derived::Timestamp timestamp;
            uint64_t address{};
            std::map<uint16_t, std::vector<uint8_t>> manufacturerDataMap;
        };
        using FnReceived = std::function<void(const ReceivedData &)>;
        using FnStopped = std::function<void()>;
        using FnError = std::function<void(const std::string &)>;

        virtual ~AdvertisementWatcherAbstract() = 0;

        inline auto& ReceivedCallbacks() {
            return _receivedCallbacks;
        }
        inline auto& StoppedCallbacks() {
            return _stoppedCallbacks;
        }
        inline auto& ErrorCallbacks() {
            return _errorCallbacks;
        }

        virtual Status Start() = 0;
        virtual Status Stop() = 0;

    private:
        Helper::Callback<FnReceived> _receivedCallbacks;
        Helper::Callback<FnStopped> _stoppedCallbacks;
        Helper::Callback<FnError> _errorCallbacks;
    };

    template <class Derived>
    inline AdvertisementWatcherAbstract<Derived>::~AdvertisementWatcherAbstract() {}

} // namespace Core::Bluetooth::Details
