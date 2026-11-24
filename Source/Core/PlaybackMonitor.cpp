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

#include "PlaybackMonitor.h"

#include <condition_variable>
#include <mutex>
#include <utility>

#include <QMetaObject>

namespace Core::GlobalMedia {

PlaybackMonitor::PlaybackMonitor(
    PlaybackProbe probe, std::chrono::milliseconds pollInterval,
    std::chrono::milliseconds silenceInterval)
    : _probe{std::move(probe)}, _pollInterval{pollInterval}, _silenceInterval{silenceInterval}
{
}

PlaybackMonitor::~PlaybackMonitor()
{
    SetEnabled(false);
}

void PlaybackMonitor::SetEnabled(bool enabled)
{
    if (enabled == _worker.joinable()) {
        return;
    }
    ++_generation;
    if (!enabled) {
        _worker.request_stop();
        _worker.join();
        return;
    }

    const auto generation = _generation;
    _worker = std::jthread{[this, generation](std::stop_token stopToken) {
        bool playing = false;
        auto lastSound = std::chrono::steady_clock::now();
        std::mutex mutex;
        std::condition_variable_any wake;
        std::unique_lock lock{mutex};
        while (!stopToken.stop_requested()) {
            const auto activity = _probe();
            const auto now = std::chrono::steady_clock::now();
            if (activity.has_value()) {
                if (*activity) {
                    lastSound = now;
                }
                const bool next = *activity || (playing && now - lastSound < _silenceInterval);
                if (playing != next) {
                    playing = next;
                    QMetaObject::invokeMethod(this, [this, generation, playing] {
                        if (_generation == generation) {
                            emit PlaybackChanged(playing);
                        }
                    });
                }
            }
            wake.wait_for(lock, stopToken, _pollInterval, [] { return false; });
        }
    }};
}

#if !defined APD_OS_WIN
PlaybackProbe CreatePlaybackProbe()
{
    return [] { return std::optional<bool>{false}; };
}
#endif

} // namespace Core::GlobalMedia
