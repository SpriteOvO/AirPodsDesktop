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

#include "Debug.h"

#include <random>

namespace Core::Debug {

DebugConfig &DebugConfig::GetInstance()
{
    static DebugConfig i;
    return i;
}

void DebugConfig::UpdateAdvOverride(bool enabled, std::vector<std::vector<uint8_t>> advs)
{
    std::lock_guard<std::mutex> lock{_mutex};

    _fields.enabled = enabled;
    _fields.advs = std::move(advs);
}

std::optional<std::vector<uint8_t>> DebugConfig::GetOverrideAdv() const
{
    std::optional<std::vector<uint8_t>> result;

    std::lock_guard<std::mutex> lock{_mutex};

    do {
        if (!_fields.enabled || _fields.advs.empty()) {
            break;
        }

        std::default_random_engine re{std::random_device{}()};
        std::uniform_int_distribution<size_t> dist{0, _fields.advs.size() - 1};

        result = _fields.advs.at(dist(re));
    } while (false);

    return result;
}

} // namespace Core::Debug
