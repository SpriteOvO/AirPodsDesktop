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

#include "Application.h"
#include "Utils.h"


int main(int argc, char *argv[])
{
    if (!Utils::Process::SingleInstance(Config::ProgramName)) {
        Logger::DoWarn(QString{"%1 is already running."}.arg(Config::ProgramName));
        return 0;
    }

    Application::Initialize(argc, argv);
    return static_cast<int>(App->Run());
}
