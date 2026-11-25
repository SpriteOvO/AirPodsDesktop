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

#include <chrono>
#include <functional>
#include <optional>
#include <thread>

#include <QObject>

namespace Core::GlobalMedia {

// Reads output activity only; no audio samples are captured or retained.
using PlaybackProbe = std::function<std::optional<bool>()>;
PlaybackProbe CreatePlaybackProbe();

class PlaybackMonitor final : public QObject
{
    Q_OBJECT

public:
    explicit PlaybackMonitor(
        PlaybackProbe probe = CreatePlaybackProbe(),
        std::chrono::milliseconds pollInterval = std::chrono::milliseconds{250},
        std::chrono::milliseconds silenceInterval = std::chrono::milliseconds{1500});
    ~PlaybackMonitor() override;

    void SetEnabled(bool enabled);

signals:
    void PlaybackChanged(bool playing);

private:
    PlaybackProbe _probe;
    std::chrono::milliseconds _pollInterval;
    std::chrono::milliseconds _silenceInterval;
    std::jthread _worker;
    unsigned _generation{};
};

} // namespace Core::GlobalMedia
