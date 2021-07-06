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

#include <Config.h>

#if !defined APD_OS_WIN
#   error "This file shouldn't be compiled."
#endif

#include <mutex>
#include <string>
#include <vector>
#include <memory>

#include "GlobalMedia_abstract.h"
#include "../Status.h"


namespace Core::GlobalMedia
{
    namespace Details
    {
        enum class ActionId : uint32_t {
            Play,
            Pause
        };

        class MediaProgramAbstract
        {
        public:
            enum class Priority : uint32_t {
                Max = 0,

                SystemSession = 1,
                MusicPlayer = 2,

                Min = std::numeric_limits<uint32_t>::max()
            };

            virtual inline ~MediaProgramAbstract() {};

            virtual bool IsAvailable() = 0;
            virtual bool IsPlaying() const = 0;
            virtual Status Play() = 0;
            virtual Status Pause() = 0;

            virtual std::wstring GetProgramName() const = 0;
            virtual Priority GetPriority() const = 0;
        };

    } // namespace Details

    bool Initialize();

    class Controller final : public Details::ControllerAbstract<Controller>
    {
    public:
        Controller() = default;

        void Play() override;
        void Pause() override;

    private:
        std::mutex _mutex;
        std::vector<std::unique_ptr<Details::MediaProgramAbstract>> _pausedPrograms;
    };

} // namespace Core::GlobalMedia::Details
