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

#include "Assert.h"

#include "Logger.h"


namespace Assert
{
    void Trigger(const QString &condition, const QString &fileName, uint32_t line)
    {
        QString content = QString{
            "Assertion triggered.\n"
            "\n"
            "Condition: %1\n"
            "File: %2\n"
            "Line: %3\n"}
            .arg(condition)
            .arg(fileName)
            .arg(line);

        Logger::DoError(content, true);
    }

} // namespace Assert
