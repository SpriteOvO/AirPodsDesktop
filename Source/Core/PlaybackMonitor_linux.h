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

#include <cstdint>
#include <optional>
#include <vector>

namespace Core::GlobalMedia::Details {

struct LinuxPlaybackStream {
    std::optional<int64_t> processId;
    bool corked{false};
};

// Returns true when any non-corked output stream belongs to another process.
bool HasExternalPlayback(const std::vector<LinuxPlaybackStream> &streams, int64_t selfProcessId);

} // namespace Core::GlobalMedia::Details
