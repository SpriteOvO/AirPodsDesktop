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

#include <mutex>

#include "QuickConnect.h"

namespace Core::QuickConnect {

// Lists the paired Bluetooth audio devices BlueZ knows about and asks BlueZ to connect them.
// `Device::id` is the device's Bluetooth address ("AA:BB:CC:DD:EE:FF").
class LinuxBackend final : public Backend
{
public:
    void SetController(Controller *controller) override;
    std::vector<Device> ListDevices(std::stop_token stopToken) override;
    bool RequestReconnect(const QString &id, std::stop_token stopToken) override;

private:
    std::mutex _controllerMutex;
    Controller *_controller{};

    void QueueEndpointState(const QString &id, bool connected);
};

} // namespace Core::QuickConnect
