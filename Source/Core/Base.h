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

#include <optional>

namespace Core::AirPods {

using Battery = std::optional<uint32_t>;

enum class Model : uint32_t {
    Unknown = 0,
    AirPods_1,
    AirPods_2,
    AirPods_3,
    AirPods_Pro,
    Powerbeats_3,
    Beats_X,
    Beats_Solo3,

    _Max = Beats_Solo3
};

enum class Side : uint32_t { Left, Right };

} // namespace Core::AirPods
