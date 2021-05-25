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

#include <QSettings>

#include "../Status.h"


namespace Core::Settings
{
    class Data
    {
    public:
        // Increase this value when the current ABI cannot be backward compatible
        // For example, the name or type of an old key has changed
        //
        constexpr static inline uint32_t abi_version = 1;

        bool auto_run{false};
        bool low_audio_latency{false};
        QString skipped_version;

    private:
        Status LoadFromQSettings(const QSettings &settings);
        Status SaveToQSettings(QSettings &settings) const;

        void HandleDiff(const Data &other);

        static void OnAutoRunChanged(bool value);
        static void OnLowAudioLatencyChanged(bool value);

        friend class Manager;
    };

    Data GetDefault();
    Data GetCurrent();
    void LoadDefault();
    Status LoadFromLocal();
    Status SaveToCurrentAndLocal(Data data);

} // namespace Core::Settings
