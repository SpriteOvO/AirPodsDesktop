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

namespace Core::Settings {

class Data
{
public:
    // Increase this value when the current ABI cannot be backward compatible
    // For example, the name or type of an old key has changed
    //
    constexpr static inline uint32_t abi_version = 1;

    bool auto_run{false};
    bool low_audio_latency{false};
    bool automatic_ear_detection{true};
    QString skipped_version;
    int16_t rssi_min{-80};
    bool reduce_loud_sounds{false};
    uint32_t loud_volume_level{40};
    uint64_t device_address{0};

private:
    Status LoadFromQSettings(const QSettings &settings);
    Status SaveToQSettings(QSettings &settings) const;

    void HandleFields(const Data &other);

    static void OnAutoRunChanged(const Data &current, bool value);
    static void OnLowAudioLatencyChanged(const Data &current, bool value);
    static void OnReduceLoudSoundsChanged(const Data &current, bool value);
    static void OnLoudVolumeLevelChanged(const Data &current, uint32_t value);
    static void OnDeviceAddressChanged(const Data &current, uint64_t value);

    friend class Manager;
};

Data GetDefault();
Data GetCurrent();
void LoadDefault();
Status LoadFromLocal();
Status SaveToCurrentAndLocal(Data data);

} // namespace Core::Settings
