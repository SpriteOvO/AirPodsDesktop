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
#include <string>
#include <vector>

#include "GlobalMedia_abstract.h"
#include "../Helper.h"

namespace Core::GlobalMedia {

// Controls every MPRIS2 media player on the session bus (Spotify, Firefox, VLC, mpv, Rhythmbox...).
// `Pause()` pauses what is playing and remembers it, `Play()` resumes only what we paused.
class Controller final : public Helper::Singleton<Controller>, public Details::ControllerAbstract
{
protected:
    Controller() = default;
    friend Helper::Singleton<Controller>;

public:
    void Play() override;
    void Pause() override;

private:
    std::mutex _mutex;
    std::vector<std::string> _pausedPlayers; // D-Bus bus names
};
} // namespace Core::GlobalMedia
