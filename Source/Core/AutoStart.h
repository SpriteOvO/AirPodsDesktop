//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#pragma once

#include <memory>

namespace Core::AutoStart {

class Service
{
public:
    virtual ~Service() = default;
    virtual void SetEnabled(bool enabled) = 0;
};

std::unique_ptr<Service> CreateAutoStartService();

} // namespace Core::AutoStart
