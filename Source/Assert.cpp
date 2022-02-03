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

#include "Assert.h"

#include <format>

#include "Error.h"

namespace Assert {

void Trigger(const std::string &condition, const std::source_location &srcloc)
{
    auto content = std::format(
        "Assertion triggered.\n"
        "\n"
        "Condition: {}\n"
        "File: {}\n"
        "Line: {}",
        condition, srcloc.file_name(), srcloc.line());

    FatalError(content, true);
}
} // namespace Assert
