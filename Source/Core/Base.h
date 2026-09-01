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

#include <optional>

#include "../Helper.h"
#include "../Logger.h"

namespace Core::AirPods {

class Battery
{
public:
    using ValueType = uint32_t;

    Battery() = default;
    inline Battery(ValueType value) : _optValue{value} {}

    bool operator==(const Battery &rhs) const = default;

    inline bool Available() const
    {
        return _optValue.has_value();
    }

    inline ValueType Value() const
    {
        if (!_optValue.has_value()) {
            LOG(Warn, "Trying to get the battery value but unavailable.");
            return 0;
        }
        return _optValue.value();
    }

    inline bool IsLowBattery() const
    {
        if (!_optValue.has_value()) {
            LOG(Warn, "Trying to determine that the battery is low but unavailable.");
            return false;
        }
        return _optValue.value() <= 20;
    }

private:
    std::optional<ValueType> _optValue;
};

enum class Model : uint32_t {
    Unknown = 0,
    AirPods_1,
    AirPods_2,
    AirPods_3,
    AirPods_4,
    AirPods_4_ANC,
    AirPods_Pro,
    AirPods_Pro_2,
    AirPods_Pro_2_USB_C,
    AirPods_Pro_3,
    AirPods_Max,
    AirPods_Max_USB_C,
    Powerbeats_3,
    Beats_X,
    Beats_Solo3,
    Beats_Fit_Pro,

    _Max
};

enum class Side : uint32_t { Left, Right };

} // namespace Core::AirPods

template <>
inline QString Helper::ToString<Core::AirPods::Model>(const Core::AirPods::Model &value)
{
    switch (value) {
    case Core::AirPods::Model::AirPods_1:
        return "AirPods 1";
    case Core::AirPods::Model::AirPods_2:
        return "AirPods 2";
    case Core::AirPods::Model::AirPods_3:
        return "AirPods 3";
    case Core::AirPods::Model::AirPods_4:
        return "AirPods 4";
    case Core::AirPods::Model::AirPods_4_ANC:
        return "AirPods 4 (ANC)";
    case Core::AirPods::Model::AirPods_Pro:
        return "AirPods Pro";
    case Core::AirPods::Model::AirPods_Pro_2:
        return "AirPods Pro 2";
    case Core::AirPods::Model::AirPods_Pro_2_USB_C:
        return "AirPods Pro 2 (USB-C)";
    case Core::AirPods::Model::AirPods_Pro_3:
        return "AirPods Pro 3";
    case Core::AirPods::Model::AirPods_Max:
        return "AirPods Max";
    case Core::AirPods::Model::AirPods_Max_USB_C:
        return "AirPods Max (USB-C)";
    case Core::AirPods::Model::Powerbeats_3:
        return "Powerbeats 3";
    case Core::AirPods::Model::Beats_X:
        return "BeatsX";
    case Core::AirPods::Model::Beats_Solo3:
        return "BeatsSolo3";
    case Core::AirPods::Model::Beats_Fit_Pro:
        return "Beats Fit Pro";
    default:
        return "Unknown";
    }
}

template <>
inline QString Helper::ToString<Core::AirPods::Side>(const Core::AirPods::Side &value)
{
    switch (value) {
    case Core::AirPods::Side::Left:
        return "Left";
    case Core::AirPods::Side::Right:
        return "Right";
    default:
        return "Unknown";
    }
}
