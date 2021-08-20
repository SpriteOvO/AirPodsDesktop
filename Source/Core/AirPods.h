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
#include <functional>

#include "../Helper.h"
#include "Bluetooth.h"

namespace Core::AirPods {

using Battery = std::optional<uint32_t>;

namespace Details {

struct BasicState {
    Battery battery;
    bool isCharging{false};

    bool operator==(const BasicState &rhs) const = default;
};
} // namespace Details

enum class Model : uint32_t {
    Unknown = 0,
    AirPods_1,
    AirPods_2,
    AirPods_Pro,
    Powerbeats_3,
    Beats_X,
    Beats_Solo3,
};

enum class Side : uint32_t { Left, Right };

struct PodState : Details::BasicState {
    bool isInEar{false};

    bool operator==(const PodState &rhs) const = default;
};

struct CaseState : Details::BasicState {
    bool isBothPodsInCase{false};
    bool isLidOpened{false};

    bool operator==(const CaseState &rhs) const = default;
};

struct PodsState {
    PodState left, right;

    bool operator==(const PodsState &rhs) const = default;
};

struct State {
    Model model{Model::Unknown};
    PodsState pods;
    CaseState caseBox;

    bool operator==(const State &rhs) const = default;
};

std::optional<State> GetState();
void StartScanner();
QString GetDisplayName();
void OnBindDeviceChanged(uint64_t address);
void OnQuit();

} // namespace Core::AirPods

template <>
inline QString Helper::ToString<Core::AirPods::Model>(const Core::AirPods::Model &value)
{
    switch (value) {
    case Core::AirPods::Model::AirPods_1:
        return "AirPods 1";
    case Core::AirPods::Model::AirPods_2:
        return "AirPods 2";
    case Core::AirPods::Model::AirPods_Pro:
        return "AirPods Pro";
    case Core::AirPods::Model::Powerbeats_3:
        return "Powerbeats 3";
    case Core::AirPods::Model::Beats_X:
        return "BeatsX";
    case Core::AirPods::Model::Beats_Solo3:
        return "BeatsSolo3";
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
